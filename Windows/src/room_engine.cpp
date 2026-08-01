#include "room_engine.hpp"

#include "persistence.hpp"
#include "room_protocol.hpp"
#include "util.hpp"

#include <veilknit/veilknit.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <set>
#include <stdexcept>

namespace vkrooms {
namespace {

constexpr const char* app_id = "veilknit.rooms";
constexpr const char* legacy_demo_identity = "VLD0:DEMO-LOCAL-IDENTITY";
constexpr const char* app_name = "VeilKnit Rooms";
constexpr std::uint16_t room_store_subkeys = 64;
constexpr std::uint32_t room_messages_per_page = 4;
constexpr std::size_t fallback_room_value_limit = 15'872;

veilknit::Bytes bytes_of(const std::string& value) { return veilknit::Bytes(value.begin(), value.end()); }
std::string string_of(const veilknit::Bytes& value) { return std::string(value.begin(), value.end()); }

std::string value_string(const veilknit::json::Value& value, const char* key, std::string fallback = {}) {
    if (!value.contains(key) || value.at(key).is_null()) return fallback;
    return value.at(key).as_string();
}

std::uint64_t value_u64(const veilknit::json::Value& value, const char* key, std::uint64_t fallback = 0) {
    if (!value.contains(key) || value.at(key).is_null()) return fallback;
    return value.at(key).as_u64();
}

bool value_bool(const veilknit::json::Value& value, const char* key, bool fallback = false) {
    if (!value.contains(key) || value.at(key).is_null()) return fallback;
    return value.at(key).as_bool();
}

std::string safe_room_store_name(const std::string& name, const std::string& id, bool replica) {
    std::string result = replica ? "replica-" : "room-";
    for (const unsigned char c : name) {
        if (std::isalnum(c)) result.push_back(static_cast<char>(std::tolower(c)));
        else if (c == ' ' || c == '-' || c == '_') result.push_back('-');
        if (result.size() >= 38) break;
    }
    result += "-" + id.substr(0, 8);
    return result;
}

} // namespace

RoomEngine::RoomEngine(Notify notify, Log log)
    : notify_(std::move(notify)),
      log_(std::move(log)),
      database_path_(app_data_directory() / "rooms-v1.json"),
      credential_path_(app_data_directory() / "credential-v1.json") {
    try {
        state_.rooms = load_rooms(database_path_);
        if (!state_.rooms.empty()) state_.selected_room = 0;
    } catch (const std::exception& error) {
        state_.status = std::string("Room database warning: ") + error.what();
    }
}

RoomEngine::~RoomEngine() { stop(); }

void RoomEngine::start() {
    std::lock_guard lock(mutex_);
    if (started_) return;
    started_ = true;
    operation_thread_ = std::thread([this] { operation_loop(); });
}

void RoomEngine::stop() {
    {
        std::lock_guard lock(mutex_);
        if (!started_ || stopping_) return;
        stopping_ = true;
    }
    operation_cv_.notify_all();
    if (subscription_) subscription_->close();
    if (operation_thread_.joinable()) operation_thread_.join();
    if (subscription_thread_.joinable()) subscription_thread_.join();
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
    std::lock_guard lock(mutex_);
    try { save_locked(); } catch (...) {}
    started_ = false;
}

AppSnapshot RoomEngine::snapshot() const {
    std::lock_guard lock(mutex_);
    return state_;
}

void RoomEngine::notify() { if (notify_) notify_(); }

void RoomEngine::log(std::string text) const {
    if (log_) log_(text);
}

void RoomEngine::set_status(ConnectionState state, std::string text) {
    log("Status: " + text);
    {
        std::lock_guard lock(mutex_);
        state_.connection = state;
        state_.status = std::move(text);
    }
    notify();
}

void RoomEngine::save_locked() { save_rooms(database_path_, state_.rooms); }

void RoomEngine::enqueue(std::function<void()> operation) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) return;
        operations_.push_back(std::move(operation));
    }
    operation_cv_.notify_one();
}

void RoomEngine::operation_loop() {
    for (;;) {
        std::function<void()> operation;
        {
            std::unique_lock lock(mutex_);
            operation_cv_.wait(lock, [this] { return stopping_ || !operations_.empty(); });
            if (stopping_ && operations_.empty()) break;
            operation = std::move(operations_.front());
            operations_.pop_front();
        }
        try { operation(); }
        catch (const veilknit::Error& error) { set_status(ConnectionState::error, "VeilKnit error [" + error.code() + "]: " + error.what()); }
        catch (const std::exception& error) { set_status(ConnectionState::error, error.what()); }
    }
}

void RoomEngine::connect_async() {
    reconnect_daemon(false);
}

void RoomEngine::reconnect_daemon(bool reset_credential) {
    set_status(ConnectionState::connecting,
               reset_credential ? "Resetting daemon authorization..." : "Reconnecting to the VeilKnit daemon...");
    enqueue([this, reset_credential] { reconnect_impl(reset_credential); });
}

void RoomEngine::reconnect_impl(bool reset_credential) {
    {
        std::lock_guard lock(mutex_);
        reconnecting_ = true;
        if (subscription_) subscription_->close();
    }
    if (subscription_thread_.joinable()) subscription_thread_.join();
    {
        std::lock_guard lock(mutex_);
        subscription_.reset();
        client_.reset();
    }

    if (reset_credential) {
        std::error_code error;
        const bool removed = std::filesystem::remove(credential_path_, error);
        if (error) {
            std::lock_guard lock(mutex_);
            reconnecting_ = false;
            throw std::runtime_error("Could not remove saved daemon credential: " + error.message());
        }
        set_status(ConnectionState::connecting,
                   removed ? "Saved credential removed; requesting fresh authorization..."
                           : "No saved credential found; requesting authorization...");
    }

    try {
        connect_impl();
        std::lock_guard lock(mutex_);
        reconnecting_ = false;
    } catch (...) {
        std::lock_guard lock(mutex_);
        reconnecting_ = false;
        throw;
    }
}

void RoomEngine::connect_impl() {
    const auto endpoint = veilknit::Client::discover_endpoint();
    veilknit::Client::ping(endpoint);
    veilknit::Credential credential;
    std::error_code error;
    if (std::filesystem::is_regular_file(credential_path_, error)) {
        credential = veilknit::Credential::load(credential_path_);
        // The daemon endpoint is username-specific. Always follow the current
        // discovery file so an upgraded daemon or a freshly selected account
        // does not leave Rooms trying to open an obsolete named pipe.
        credential.endpoint = endpoint;
        credential.protocol_version = veilknit::protocol_version;
        credential.save(credential_path_);
    } else {
        set_status(ConnectionState::authorizing, "Approve ‘VeilKnit Rooms’ in the daemon console...");
        credential = veilknit::Client::register_app(endpoint, app_id, app_name);
        credential.save(credential_path_);
    }
    auto client = veilknit::Client::authenticate(credential);
    const auto identity = client.identity();
    const auto signing = client.signing_identity();
    const auto owned_stores = client.list_stores();
    auto subscription = client.subscribe_messages();

    {
        std::lock_guard lock(mutex_);
        client_ = std::make_unique<veilknit::Client>(std::move(client));
        subscription_ = std::make_unique<veilknit::MessageSubscription>(std::move(subscription));
        state_.username = identity.username;
        state_.main_dht = identity.main_dht;
        state_.signing_key = signing.public_key_hex;
        state_.connection = ConnectionState::connected;
        state_.demo_mode = false;
        state_.status = "Connected as " + identity.username;
        for (auto& room : state_.rooms) {
            if (!room.owned_store_id.empty()) {
                const auto found = std::find_if(owned_stores.begin(), owned_stores.end(), [&](const veilknit::StoreDescriptor& store) {
                    return store.store_id == room.owned_store_id;
                });
                if (found != owned_stores.end()) {
                    room.owned_store_subkeys = found->subkey_count;
                    room.owned_store_value_limit = found->max_value_bytes;
                    room.owned_store_generation = found->generation;
                }
            }
            // Early demo builds persisted a fake member into real rooms. It is
            // not a valid DHT record key and caused a pointless handshake every
            // heartbeat. Preserve demo-only rooms, but remove the placeholder
            // from every real room.
            if (room.owner_main_dht != legacy_demo_identity) {
                room.members.erase(legacy_demo_identity);
                room.replicas.erase(
                    std::remove_if(room.replicas.begin(), room.replicas.end(), [](const ReplicaInfo& item) {
                        return item.main_dht == legacy_demo_identity;
                    }),
                    room.replicas.end());
            }
            auto& member = room.members[identity.main_dht];
            member.main_dht = identity.main_dht;
            if (member.display_name.empty()) member.display_name = identity.username;
            member.signing_key = signing.public_key_hex;
            member.online = true;
            member.last_seen = unix_time();
        }
        save_locked();
    }
    notify();

    if (!subscription_thread_.joinable()) subscription_thread_ = std::thread([this] { subscription_loop(); });
    if (!heartbeat_thread_.joinable()) heartbeat_thread_ = std::thread([this] { heartbeat_loop(); });
    try { client_->trigger_message_retrieval(); } catch (...) {}

    std::vector<std::string> room_ids;
    {
        std::lock_guard lock(mutex_);
        for (const auto& room : state_.rooms) {
            if (!room.suspended) room_ids.push_back(room.room_id);
        }
    }
    for (const auto& room_id : room_ids) sync_room_impl(room_id);

    // A previous join request may have been queued under an older platform-
    // specific application id and never reached the room owner. Re-announce
    // membership whenever Rooms reconnects; owners deduplicate known members.
    for (const auto& room_id : room_ids) {
        Room room;
        std::string username;
        std::string signing_key;
        std::string main_dht;
        {
            std::lock_guard lock(mutex_);
            auto* current = find_room_locked(room_id);
            if (!current || current->suspended || current->owner_main_dht == state_.main_dht) continue;
            room = *current;
            username = state_.username;
            signing_key = state_.signing_key;
            main_dht = state_.main_dht;
        }
        veilknit::json::Value body = veilknit::json::Value::make_object();
        body["display_name"] = username;
        body["signing_key"] = signing_key;
        try {
            send_control(room, "join_request", body, room.owner_main_dht);
            log("Re-sent room join request to " + short_identity(room.owner_main_dht));
        } catch (const std::exception& error) {
            log("Could not re-send room join request: " + std::string(error.what()));
        }
    }
}

void RoomEngine::start_demo_mode() {
    {
        std::lock_guard lock(mutex_);
        state_.demo_mode = true;
        state_.connection = ConnectionState::connected;
        state_.username = "Demo Pilot";
        state_.main_dht = "VLD0:DEMO-LOCAL-IDENTITY";
        state_.signing_key = "demo-signing-key";
        state_.status = "Demo mode — no daemon traffic";
        if (state_.rooms.empty()) {
            Room room;
            room.room_id = random_hex(16);
            room.name = "Model Airplanes";
            room.owner_main_dht = state_.main_dht;
            room.owner_signing_key = state_.signing_key;
            room.access_secret_hex = random_hex(32);
            room.created_at = unix_time();
            Member owner;
            owner.main_dht = state_.main_dht; owner.display_name = state_.username; owner.signing_key = state_.signing_key;
            owner.role = Role::owner; owner.online = true; owner.last_seen = unix_time(); owner.replica = true;
            room.members[owner.main_dht] = owner;
            add_system_message(room, "Welcome to the VeilKnit Rooms demo. Connect to the daemon to exchange real messages.");
            state_.rooms.push_back(std::move(room));
            state_.selected_room = 0;
        }
        save_locked();
    }
    notify();
}

void RoomEngine::subscription_loop() {
    for (;;) {
        {
            std::lock_guard lock(mutex_);
            if (stopping_ || !subscription_) break;
        }
        try {
            const auto incoming = subscription_->next();
            log("Received daemon application message from " + short_identity(incoming.sender_main_dht) +
                " (" + std::to_string(incoming.payload.size()) + " bytes)");
            handle_wire_message(string_of(incoming.payload));
        } catch (const std::exception& error) {
            bool should_notify = false;
            {
                std::lock_guard lock(mutex_);
                if (!stopping_ && !reconnecting_) {
                    state_.connection = ConnectionState::error;
                    state_.status = std::string("Message subscription stopped: ") + error.what();
                    should_notify = true;
                }
            }
            if (should_notify) notify();
            break;
        }
    }
}

void RoomEngine::heartbeat_loop() {
    for (;;) {
        for (int i = 0; i < 30; ++i) {
            {
                std::lock_guard lock(mutex_);
                if (stopping_) return;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        enqueue([this] {
            std::vector<std::string> room_ids;
            {
                std::lock_guard lock(mutex_);
                if (!client_) return;
                for (auto& room : state_.rooms) {
                    for (auto& [_, member] : room.members) {
                        if (member.main_dht != state_.main_dht && member.last_seen + 120 < unix_time()) member.online = false;
                    }
                    if (!room.suspended) room_ids.push_back(room.room_id);
                }
            }
            for (const auto& room_id : room_ids) {
                Room room_copy;
                {
                    std::lock_guard lock(mutex_);
                    auto* room = find_room_locked(room_id); if (!room) continue; room_copy = *room;
                }
                if (room_copy.suspended || room_copy.owner_main_dht == legacy_demo_identity) continue;
                veilknit::json::Value body = veilknit::json::Value::make_object();
                body["replica"] = room_copy.local_replica;
                body["record_key"] = room_copy.local_replica ? room_copy.canonical_record_key : "";
                try {
                    send_control(room_copy, "presence", body);
                } catch (const std::exception& error) {
                    log("Room heartbeat failed for " + room_copy.room_id.substr(0, 8) + ": " + error.what());
                }
            }
            notify();
        });
    }
}

Room* RoomEngine::find_room_locked(const std::string& room_id) {
    for (auto& room : state_.rooms) if (room.room_id == room_id) return &room;
    return nullptr;
}

const Room* RoomEngine::selected_room_locked() const {
    if (state_.selected_room < 0 || state_.selected_room >= static_cast<int>(state_.rooms.size())) return nullptr;
    return &state_.rooms[static_cast<std::size_t>(state_.selected_room)];
}
Room* RoomEngine::selected_room_locked() {
    if (state_.selected_room < 0 || state_.selected_room >= static_cast<int>(state_.rooms.size())) return nullptr;
    return &state_.rooms[static_cast<std::size_t>(state_.selected_room)];
}

void RoomEngine::select_room(int index) {
    {
        std::lock_guard lock(mutex_);
        if (index < 0 || index >= static_cast<int>(state_.rooms.size())) return;
        state_.selected_room = index;
    }
    notify();
}

void RoomEngine::remove_selected_room() {
    enqueue([this] {
        Room removed;
        std::string local_main_dht;
        {
            std::lock_guard lock(mutex_);
            const auto* room = selected_room_locked();
            if (!room) return;
            removed = *room;
            local_main_dht = state_.main_dht;
        }

        // A leave event is best-effort. Local removal must still work while a
        // room is unreachable or after its owner has disappeared.
        if (!removed.suspended && removed.owner_main_dht != local_main_dht && client_) {
            try {
                veilknit::json::Value body = veilknit::json::Value::make_object();
                body["subject"] = local_main_dht;
                send_control(removed, "leave", body);
            } catch (const std::exception& error) {
                log("Could not announce room departure: " + std::string(error.what()));
            }
        }

        {
            std::lock_guard lock(mutex_);
            const auto iterator = std::find_if(state_.rooms.begin(), state_.rooms.end(), [&](const Room& room) {
                return room.room_id == removed.room_id;
            });
            if (iterator == state_.rooms.end()) return;
            const auto removed_index = static_cast<int>(std::distance(state_.rooms.begin(), iterator));
            state_.rooms.erase(iterator);
            if (state_.rooms.empty()) state_.selected_room = -1;
            else if (state_.selected_room > removed_index) --state_.selected_room;
            else if (state_.selected_room >= static_cast<int>(state_.rooms.size())) state_.selected_room = static_cast<int>(state_.rooms.size()) - 1;
            state_.status = "Removed room ‘" + removed.name + "’ from this device";
            save_locked();
        }
        log("Removed local room " + removed.room_id.substr(0, 8));
        notify();
    });
}

void RoomEngine::retry_selected_room() {
    std::string room_id;
    {
        std::lock_guard lock(mutex_);
        auto* room = selected_room_locked();
        if (!room) return;
        room->suspended = false;
        room->reachability_failures = 0;
        room->last_reachability_error.clear();
        room_id = room->room_id;
        state_.status = "Retrying room ‘" + room->name + "’...";
        save_locked();
    }
    notify();
    enqueue([this, room_id] {
        sync_room_impl(room_id);
        Room room;
        std::string username;
        std::string signing_key;
        std::string local_main_dht;
        {
            std::lock_guard lock(mutex_);
            auto* current = find_room_locked(room_id);
            if (!current || current->suspended || !client_) return;
            room = *current;
            username = state_.username;
            signing_key = state_.signing_key;
            local_main_dht = state_.main_dht;
        }
        try {
            if (room.owner_main_dht != local_main_dht) {
                veilknit::json::Value body = veilknit::json::Value::make_object();
                body["display_name"] = username;
                body["signing_key"] = signing_key;
                send_control(room, "join_request", body, room.owner_main_dht);
            } else {
                veilknit::json::Value body = veilknit::json::Value::make_object();
                body["replica"] = room.local_replica;
                body["record_key"] = room.local_replica ? room.canonical_record_key : "";
                send_control(room, "presence", body);
            }
        } catch (const std::exception& error) {
            log("Room retry failed for " + room_id.substr(0, 8) + ": " + error.what());
        }
    });
}

void RoomEngine::create_room(std::string name) {
    name = trim(std::move(name));
    if (name.empty()) return;
    {
        std::lock_guard lock(mutex_);
        if (state_.room_creation_in_progress) return;
        state_.room_creation_in_progress = true;
        state_.status = "Creating room ‘" + name + "’...";
    }
    notify();
    enqueue([this, name = std::move(name)] {
        try {
            create_room_impl(name);
            {
                std::lock_guard lock(mutex_);
                state_.room_creation_in_progress = false;
            }
            notify();
        } catch (...) {
            {
                std::lock_guard lock(mutex_);
                state_.room_creation_in_progress = false;
            }
            notify();
            throw;
        }
    });
}

void RoomEngine::create_room_impl(const std::string& name) {
    std::string username, main_dht, signing_key;
    bool demo = false;
    {
        std::lock_guard lock(mutex_);
        username = state_.username; main_dht = state_.main_dht; signing_key = state_.signing_key; demo = state_.demo_mode;
    }
    if (main_dht.empty()) throw std::runtime_error("Connect to the daemon before creating a room");

    Room room;
    room.room_id = random_hex(16);
    room.name = name;
    room.owner_main_dht = main_dht;
    room.owner_signing_key = signing_key;
    room.access_secret_hex = random_hex(32);
    room.created_at = unix_time();
    room.owner_last_seen = unix_time();
    room.messages_per_page = room_messages_per_page;
    room.local_replica = true;
    Member owner;
    owner.main_dht = main_dht; owner.display_name = username; owner.signing_key = signing_key;
    owner.role = Role::owner; owner.online = true; owner.replica = true; owner.last_seen = unix_time(); owner.max_helpers = 8;
    room.members[main_dht] = owner;
    add_system_message(room, "Room created. Use ‘Share Invite’ to show a QR code or copy the invitation.");

    if (!demo) {
        auto store = client_->create_store(safe_room_store_name(name, room.room_id, false), room_store_subkeys, true);
        room.owned_store_id = store.store_id;
        room.owned_store_subkeys = store.subkey_count;
        room.owned_store_value_limit = store.max_value_bytes;
        room.owned_store_generation = store.generation;
        room.canonical_record_key = store.record_key;
        room = persist_room(std::move(room));
    }
    {
        std::lock_guard lock(mutex_);
        state_.rooms.push_back(std::move(room));
        state_.selected_room = static_cast<int>(state_.rooms.size()) - 1;
        state_.status = "Room created";
        save_locked();
    }
    notify();
}

void RoomEngine::join_room(std::string invite_code) {
    enqueue([this, code = std::move(invite_code)] { join_room_impl(code); });
}

void RoomEngine::join_room_impl(const std::string& invite_code) {
    const auto invite = parse_invite_code(invite_code);
    log("Joining room " + invite.room_id.substr(0, 8) + " owned by " + short_identity(invite.owner_main_dht));
    std::string username, main_dht, signing_key;
    {
        std::lock_guard lock(mutex_);
        username = state_.username; main_dht = state_.main_dht; signing_key = state_.signing_key;
        if (main_dht.empty()) throw std::runtime_error("Connect to the daemon before joining a room");
        if (find_room_locked(invite.room_id)) throw std::runtime_error("This room is already in your room list");
    }
    Room room;
    room.room_id = invite.room_id;
    room.name = invite.room_name;
    room.owner_main_dht = invite.owner_main_dht;
    room.owner_signing_key = invite.owner_public_key;
    room.access_secret_hex = invite.access_secret_hex;
    room.canonical_record_key = invite.record_key;
    room.authority_epoch = invite.authority_epoch;
    room.created_at = unix_time();
    Member owner; owner.main_dht = invite.owner_main_dht; owner.display_name = "Room Owner"; owner.signing_key = invite.owner_public_key; owner.role = Role::owner;
    room.members[owner.main_dht] = owner;
    Member self; self.main_dht = main_dht; self.display_name = username; self.signing_key = signing_key; self.role = Role::member; self.online = true; self.last_seen = unix_time();
    room.members[self.main_dht] = self;
    add_system_message(room, "Room joined. Synchronizing available history...");
    {
        std::lock_guard lock(mutex_);
        state_.rooms.push_back(room);
        state_.selected_room = static_cast<int>(state_.rooms.size()) - 1;
        save_locked();
    }
    notify();
    sync_room_impl(invite.room_id);

    Room room_copy;
    {
        std::lock_guard lock(mutex_); auto* current = find_room_locked(invite.room_id); if (!current) return; room_copy = *current;
    }
    veilknit::json::Value body = veilknit::json::Value::make_object();
    body["display_name"] = username;
    body["signing_key"] = signing_key;
    send_control(room_copy, "join_request", body, invite.owner_main_dht);
    log("Join request sent to " + short_identity(invite.owner_main_dht));
}

void RoomEngine::submit_text(std::string text) {
    text = trim(std::move(text));
    if (text.empty()) return;

    std::string command = text;
    std::transform(command.begin(), command.end(), command.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (command == "/reconnect" || command == "/reconnect-daemon") {
        reconnect_daemon(false);
        return;
    }
    if (command == "/reauthorize" || command == "/reset-daemon" || command == "/reset-credential") {
        reconnect_daemon(true);
        return;
    }
    if (command == "/commands" || command == "/help") {
        {
            std::lock_guard lock(mutex_);
            if (auto* room = selected_room_locked()) {
                add_system_message(*room,
                    "Local commands: /reconnect reconnects using the saved daemon credential; "
                    "/reauthorize removes credential-v1.json and requests fresh approval; "
                    "Shift+Enter inserts a new line.");
                save_locked();
            }
        }
        notify();
        return;
    }
    if (!command.empty() && command.front() == '/') {
        set_status(ConnectionState::error,
                   "Unknown local command. Type /commands to see available commands.");
        return;
    }
    send_chat(std::move(text));
}

void RoomEngine::send_chat(std::string text) {
    text = trim(std::move(text));
    if (text.empty()) return;
    if (text.size() > 2000) { set_status(ConnectionState::error, "Messages are currently limited to 2,000 UTF-8 bytes"); return; }
    std::string room_id;
    {
        std::lock_guard lock(mutex_);
        const auto* room = selected_room_locked(); if (!room) return; room_id = room->room_id;
    }
    enqueue([this, room_id, text = std::move(text)] { send_chat_impl(room_id, text); });
}

void RoomEngine::send_chat_impl(const std::string& room_id, const std::string& text) {
    Room room;
    std::string username, main_dht, signing_key;
    bool demo = false;
    {
        std::lock_guard lock(mutex_);
        auto* current = find_room_locked(room_id); if (!current) return;
        if (current->suspended) throw std::runtime_error("This room is paused. Right-click it and choose Retry.");
        const auto me = current->members.find(state_.main_dht);
        if (me != current->members.end() && me->second.banned) throw std::runtime_error("You are banned from this room");
        const auto lower = lowercase_ascii(text);
        for (const auto& phrase : current->banned_phrases) if (!phrase.empty() && lower.find(lowercase_ascii(phrase)) != std::string::npos)
            throw std::runtime_error("Message contains a phrase blocked by this room");
        room = *current; username = state_.username; main_dht = state_.main_dht; signing_key = state_.signing_key; demo = state_.demo_mode;
    }
    if (demo) {
        ChatMessage message;
        message.event_id = random_hex(16); message.room_id = room_id; message.sender_main_dht = main_dht;
        message.sender_name = username; message.sender_signing_key = signing_key; message.text = text;
        message.created_at = unix_time(); message.verified = true;
        {
            std::lock_guard lock(mutex_); auto* current = find_room_locked(room_id); if (!current) return; current->messages.push_back(std::move(message)); save_locked();
        }
        notify(); return;
    }
    veilknit::json::Value body = veilknit::json::Value::make_object();
    body["text"] = text;
    body["recovery"] = room.owner_last_seen != 0 && room.owner_last_seen + 180 < unix_time();
    const auto wire = signed_envelope(*client_, room, "chat", main_dht, username, signing_key, body);
    log("Sending chat event " + room.room_id.substr(0, 8) + " to " +
        std::to_string(room.members.size() > 0 ? room.members.size() - 1 : 0) + " possible peer(s)");
    handle_wire_message(wire, false);
    send_envelope_to_room(room, wire);
}

void RoomEngine::handle_wire_message(const std::string& wire, bool allow_relay) {
    std::string room_id;
    try {
        const auto root = veilknit::json::Value::parse(wire);
        if (!root.contains("protocol") || root.at("protocol").as_string() != "veilknit.rooms") return;
        room_id = root.at("room_id").as_string();
    } catch (...) { return; }
    {
        std::lock_guard lock(mutex_);
        const auto* room = find_room_locked(room_id);
        if (!room || room->suspended) return;
    }
    try {
        std::lock_guard lock(mutex_);
        auto* room = find_room_locked(room_id); if (!room || !client_) return;
        apply_decoded_message(*room, wire, allow_relay);
        note_room_success(room_id);
        save_locked();
    } catch (const std::exception& error) {
        std::lock_guard lock(mutex_);
        auto* room = find_room_locked(room_id);
        if (room) add_system_message(*room, std::string("Rejected room event: ") + error.what());
    }
    notify();
}

bool RoomEngine::event_seen(const Room& room, const std::string& event_id) const {
    return std::any_of(room.messages.begin(), room.messages.end(), [&](const ChatMessage& message) { return message.event_id == event_id; });
}

void RoomEngine::apply_decoded_message(Room& room, const std::string& wire, bool allow_relay) {
    auto decoded = decode_envelope(*client_, room, wire);
    log("Applying room event kind=" + decoded.kind + " room=" + room.room_id.substr(0, 8) +
        " sender=" + short_identity(decoded.sender_main_dht));
    const bool sender_was_known = room.members.find(decoded.sender_main_dht) != room.members.end();
    auto& sender = room.members[decoded.sender_main_dht];
    if (sender.main_dht.empty()) sender.main_dht = decoded.sender_main_dht;
    if (sender.display_name.empty() || sender.display_name == "Unknown") sender.display_name = decoded.sender_name;
    if (sender.signing_key.empty()) sender.signing_key = decoded.sender_public_key;
    if (!sender.signing_key.empty() && sender.signing_key != decoded.sender_public_key) throw std::runtime_error("sender signing key changed without a room grant");
    sender.online = true; sender.last_seen = unix_time();
    if (decoded.sender_main_dht == room.owner_main_dht) room.owner_last_seen = unix_time();

    if (decoded.kind == "chat") {
        if (event_seen(room, decoded.event_id)) return;
        if (sender.banned) return;
        const auto text = decoded.body.at("text").as_string();
        const auto lower = lowercase_ascii(text);
        for (const auto& phrase : room.banned_phrases) if (!phrase.empty() && lower.find(lowercase_ascii(phrase)) != std::string::npos) return;
        ChatMessage message;
        message.event_id = decoded.event_id; message.room_id = room.room_id; message.sender_main_dht = decoded.sender_main_dht;
        message.sender_name = decoded.sender_name; message.sender_signing_key = decoded.sender_public_key;
        message.text = text; message.created_at = decoded.created_at; message.verified = decoded.verified;
        message.recovery = value_bool(decoded.body, "recovery"); message.wire_json = wire;
        if (std::find(room.deleted_message_ids.begin(), room.deleted_message_ids.end(), message.event_id) != room.deleted_message_ids.end()) message.deleted = true;
        room.messages.push_back(message);
        if ((room.owner_main_dht == state_.main_dht || room.local_replica) && !room.owned_store_id.empty()) {
            enqueue([this, id = room.room_id] {
                Room work;
                {
                    std::lock_guard lock(mutex_);
                    auto* current = find_room_locked(id);
                    if (!current || !client_) return;
                    work = *current;
                }
                work = persist_room(std::move(work));
                {
                    std::lock_guard lock(mutex_);
                    auto* current = find_room_locked(id);
                    if (!current) return;
                    current->owned_store_generation = work.owned_store_generation;
                    current->manifest_generation = work.manifest_generation;
                    current->latest_page = work.latest_page;
                    save_locked();
                }
                notify();
            });
        }
        if (allow_relay && decoded.sender_main_dht != state_.main_dht) {
            Room copy = room;
            enqueue([this, copy, wire, sender_id = decoded.sender_main_dht]() mutable { send_envelope_to_room(copy, wire, sender_id); });
        }
        return;
    }

    if (decoded.kind == "presence") {
        sender.replica = value_bool(decoded.body, "replica");
        sender.replica_record_key = value_string(decoded.body, "record_key");
        return;
    }

    if (decoded.kind == "join_request") {
        sender.display_name = value_string(decoded.body, "display_name", decoded.sender_name);
        sender.signing_key = decoded.sender_public_key;
        if (sender.role == Role::owner) sender.role = Role::member;
        if (!sender_was_known) add_system_message(room, sender.display_name + " joined the room.");
        if (room.owner_main_dht == state_.main_dht) {
            // Persist without holding the UI/state mutex. Once the canonical
            // manifest contains the new member, send the acceptance reply.
            enqueue([this, id = room.room_id, recipient = decoded.sender_main_dht] {
                Room work;
                {
                    std::lock_guard lock(mutex_);
                    auto* current = find_room_locked(id);
                    if (!current || !client_) return;
                    work = *current;
                }
                work = persist_room(std::move(work));
                {
                    std::lock_guard lock(mutex_);
                    auto* current = find_room_locked(id);
                    if (!current) return;
                    current->owned_store_generation = work.owned_store_generation;
                    current->manifest_generation = work.manifest_generation;
                    current->latest_page = work.latest_page;
                    work = *current;
                    save_locked();
                }
                auto body = room_manifest_body(work);
                send_control(work, "join_accepted", body, recipient);
                log("Join accepted for " + short_identity(recipient));
                notify();
            });
        }
        return;
    }

    if (decoded.kind == "join_accepted" || decoded.kind == "manifest") {
        const bool authoritative = decoded.sender_main_dht == room.owner_main_dht && decoded.sender_public_key == room.owner_signing_key;
        if (!authoritative) throw std::runtime_error("manifest was not signed by the room owner");
        apply_manifest(room, decoded.body, true);
        if (decoded.kind == "join_accepted") {
            // The owner has just committed the new member-bearing manifest.
            // Give the DHT a short propagation window, then retry a few times
            // rather than leaving the joiner on the initial stale read.
            enqueue([this, id = room.room_id] {
                for (int attempt = 0; attempt < 3; ++attempt) {
                    if (attempt > 0) std::this_thread::sleep_for(std::chrono::seconds(3));
                    sync_room_impl(id);
                    std::lock_guard lock(mutex_);
                    if (state_.status == "Room history synchronized") break;
                }
            });
        }
        return;
    }

    if (decoded.kind == "replica_manifest") {
        return;
    }

    if (decoded.kind == "role_grant") {
        const auto subject = decoded.body.at("subject").as_string();
        const auto role = role_from_string(decoded.body.at("role").as_string());
        if (role == Role::moderator && decoded.sender_main_dht != room.owner_main_dht) throw std::runtime_error("only the owner may appoint moderators");
        if (role == Role::helper) {
            const auto issuer = room.members.find(decoded.sender_main_dht);
            if (issuer == room.members.end() || issuer->second.role != Role::moderator)
                throw std::runtime_error("only a true moderator may appoint helpers");
        }
        if (role == Role::member && !can_moderate(room, decoded.sender_main_dht, "roles")) throw std::runtime_error("issuer may not revoke roles");
        auto& member = room.members[subject]; member.main_dht = subject; member.role = role;
        member.max_helpers = static_cast<std::uint32_t>(value_u64(decoded.body, "max_helpers"));
        add_system_message(room, member.display_name + " is now a " + role_name(role) + ".");
        return;
    }

    if (decoded.kind == "moderation") {
        const auto action = decoded.body.at("action").as_string();
        if (!can_moderate(room, decoded.sender_main_dht, action)) throw std::runtime_error("issuer lacks moderation authority");
        if (action == "ban" || action == "unban") {
            const auto subject = decoded.body.at("subject").as_string();
            if (subject == room.owner_main_dht) throw std::runtime_error("the room owner cannot be banned");
            auto& member = room.members[subject]; member.main_dht = subject; member.banned = action == "ban";
            add_system_message(room, member.display_name + (member.banned ? " was banned." : " was unbanned."));
        } else if (action == "delete") {
            const auto id = decoded.body.at("message_id").as_string();
            if (std::find(room.deleted_message_ids.begin(), room.deleted_message_ids.end(), id) == room.deleted_message_ids.end()) room.deleted_message_ids.push_back(id);
            for (auto& message : room.messages) if (message.event_id == id) message.deleted = true;
        } else if (action == "set_phrases") {
            room.banned_phrases.clear();
            for (const auto& item : decoded.body.at("phrases").as_array()) room.banned_phrases.push_back(item.as_string());
            add_system_message(room, "The room phrase policy was updated.");
        }
        return;
    }

    if (decoded.kind == "replica_ad") {
        sender.replica = value_bool(decoded.body, "enabled");
        sender.replica_record_key = value_string(decoded.body, "record_key");
        auto iterator = std::find_if(room.replicas.begin(), room.replicas.end(), [&](const ReplicaInfo& item) { return item.main_dht == sender.main_dht; });
        if (sender.replica) {
            if (iterator == room.replicas.end()) room.replicas.push_back({sender.main_dht, sender.replica_record_key, 0, unix_time()});
            else { iterator->record_key = sender.replica_record_key; iterator->last_seen = unix_time(); }
        } else if (iterator != room.replicas.end()) room.replicas.erase(iterator);
        return;
    }

    if (decoded.kind == "leave") {
        const auto subject = value_string(decoded.body, "subject", decoded.sender_main_dht);
        if (subject != decoded.sender_main_dht) throw std::runtime_error("a leave event may only remove its signer");
        if (subject == room.owner_main_dht) throw std::runtime_error("the room owner cannot leave without closing the room");
        const auto iterator = room.members.find(subject);
        const std::string display = iterator == room.members.end() ? short_identity(subject) : iterator->second.display_name;
        room.members.erase(subject);
        room.replicas.erase(
            std::remove_if(room.replicas.begin(), room.replicas.end(), [&](const ReplicaInfo& item) { return item.main_dht == subject; }),
            room.replicas.end());
        add_system_message(room, display + " left the room.");
        return;
    }
}

void RoomEngine::send_envelope_to_room(Room& room, const std::string& wire, const std::string& omit) {
    if (!client_) throw std::runtime_error("Not connected to the VeilKnit daemon");
    if (room.suspended) throw std::runtime_error("This room is paused. Use Retry before contacting it again.");
    std::set<std::string> recipients;
    recipients.insert(room.owner_main_dht);
    for (const auto& [id, member] : room.members) if (!member.banned) recipients.insert(id);
    recipients.erase(state_.main_dht);
    if (!omit.empty()) recipients.erase(omit);

    std::size_t delivered = 0;
    std::string last_error;
    for (const auto& recipient : recipients) {
        if (recipient.empty() || recipient == legacy_demo_identity) continue;
        try {
            client_->send_message(recipient, bytes_of(wire));
            ++delivered;
            log("Queued room event for " + short_identity(recipient));
        } catch (const std::exception& error) {
            last_error = error.what();
            log("Room event delivery failed for " + short_identity(recipient) + ": " + last_error);
        }
    }
    if (!recipients.empty() && delivered == 0) {
        const std::string message = "Could not queue the room event for any peer" +
            (last_error.empty() ? std::string{} : std::string(": ") + last_error);
        note_room_failure(room.room_id, message);
        throw std::runtime_error(message);
    }
    if (delivered > 0) note_room_success(room.room_id);
}

void RoomEngine::send_control(Room& room, const std::string& kind, const veilknit::json::Value& body, const std::string& direct_recipient) {
    if (!client_) return;
    if (room.suspended && kind != "leave") throw std::runtime_error("This room is paused. Use Retry before contacting it again.");
    std::string username, main_dht, signing_key;
    {
        std::lock_guard lock(mutex_); username = state_.username; main_dht = state_.main_dht; signing_key = state_.signing_key;
    }
    const auto wire = signed_envelope(*client_, room, kind, main_dht, username, signing_key, body);
    if (!direct_recipient.empty()) {
        try {
            client_->send_message(direct_recipient, bytes_of(wire));
            note_room_success(room.room_id);
            log("Queued " + kind + " for " + short_identity(direct_recipient));
        } catch (const std::exception& error) {
            note_room_failure(room.room_id, error.what());
            throw;
        }
    } else {
        send_envelope_to_room(room, wire);
    }
    handle_wire_message(wire, false);
}

Room RoomEngine::persist_room(Room room) {
    if (!client_ || room.owned_store_id.empty()) return room;
    std::string main_dht;
    std::string username;
    std::string signing_key;
    {
        std::lock_guard lock(mutex_);
        main_dht = state_.main_dht;
        username = state_.username;
        signing_key = state_.signing_key;
    }
    std::vector<std::string> history;
    for (const auto& message : room.messages) if (!message.system && !message.wire_json.empty()) history.push_back(message.wire_json);
    const auto messages_per_page = std::max<std::uint32_t>(1, room.messages_per_page);
    const std::uint32_t latest_page = history.empty() ? 1 : static_cast<std::uint32_t>((history.size() - 1) / messages_per_page + 1);
    const auto subkey_count = room.owned_store_subkeys == 0 ? room_store_subkeys : room.owned_store_subkeys;
    if (latest_page >= subkey_count) throw std::runtime_error("This room-store generation is full; archive rotation is not implemented yet");
    room.latest_page = latest_page;
    const std::size_t begin = (latest_page - 1) * messages_per_page;
    veilknit::json::Value page_body = veilknit::json::Value::make_object();
    page_body["page"] = static_cast<std::uint64_t>(latest_page);
    page_body["writer_main_dht"] = main_dht;
    veilknit::json::Value messages = veilknit::json::Value::make_array();
    for (std::size_t i = begin; i < history.size(); ++i) messages.push_back(history[i]);
    page_body["messages"] = std::move(messages);
    const std::string page_kind = room.owner_main_dht == main_dht ? "history_page" : "replica_history_page";
    const auto page_wire = signed_envelope(*client_, room, page_kind, main_dht, username, signing_key, page_body);

    veilknit::json::Value manifest_body = room_manifest_body(room);
    if (room.owner_main_dht != main_dht) manifest_body["canonical_record_key"] = room.canonical_record_key;
    const std::string manifest_kind = room.owner_main_dht == main_dht ? "manifest" : "replica_manifest";
    const auto manifest_wire = signed_envelope(*client_, room, manifest_kind, main_dht, username, signing_key, manifest_body);
    const auto value_limit = room.owned_store_value_limit == 0 ? fallback_room_value_limit : room.owned_store_value_limit;
    const auto manifest_bytes = bytes_of(manifest_wire);
    const auto page_bytes = bytes_of(page_wire);
    if (manifest_bytes.size() > value_limit) {
        throw std::runtime_error("Room manifest is " + std::to_string(manifest_bytes.size()) +
            " bytes, exceeding this store's " + std::to_string(value_limit) + "-byte value limit");
    }
    if (page_bytes.size() > value_limit) {
        throw std::runtime_error("Room history page is " + std::to_string(page_bytes.size()) +
            " bytes, exceeding this store's " + std::to_string(value_limit) +
            "-byte value limit. Shorten messages or start a new room-store generation.");
    }
    std::vector<veilknit::StoreWrite> writes;
    writes.push_back({0, manifest_bytes});
    writes.push_back({latest_page, page_bytes});
    auto descriptor = client_->write_store(room.owned_store_id, writes, room.owned_store_generation);
    room.owned_store_subkeys = descriptor.subkey_count;
    room.owned_store_value_limit = descriptor.max_value_bytes;
    room.owned_store_generation = descriptor.generation;
    room.manifest_generation++;
    log("Room history persisted: room=" + room.room_id.substr(0, 8) +
        " generation=" + std::to_string(room.owned_store_generation) +
        " page=" + std::to_string(room.latest_page));
    return room;
}

void RoomEngine::sync_selected_room() {
    std::string room_id;
    {
        std::lock_guard lock(mutex_);
        const auto* room = selected_room_locked();
        if (!room) return;
        if (room->suspended) {
            state_.status = "This room is paused. Right-click it and choose Retry.";
            notify();
            return;
        }
        room_id = room->room_id;
    }
    enqueue([this, room_id] { sync_room_impl(room_id); });
}

void RoomEngine::sync_room_impl(const std::string& room_id) {
    Room room;
    {
        std::lock_guard lock(mutex_); auto* current = find_room_locked(room_id); if (!current || current->suspended || !client_) return; room = *current;
    }
    bool synced = false;
    std::string first_error;
    if (!room.canonical_record_key.empty()) {
        try { sync_from_record(room, room.canonical_record_key, true); synced = true; }
        catch (const std::exception& error) { first_error = error.what(); }
    }
    if (!synced) {
        for (const auto& replica : room.replicas) {
            if (replica.record_key.empty()) continue;
            try { sync_from_record(room, replica.record_key, false); synced = true; break; }
            catch (const std::exception& error) { if (first_error.empty()) first_error = error.what(); }
        }
    }
    {
        std::lock_guard lock(mutex_);
        auto* current = find_room_locked(room_id); if (!current) return;
        if (synced) state_.status = "Room history synchronized";
        else state_.status = first_error.empty() ? "No reachable room history store" : "Room history unavailable: " + first_error;
        save_locked();
    }
    if (synced) note_room_success(room_id);
    else note_room_failure(room_id, first_error.empty() ? "No reachable room history store" : first_error);
    notify();
}

void RoomEngine::sync_from_record(Room& room, const std::string& record_key, bool canonical) {
    const auto manifest_read = client_->read_public_store(record_key, {0}, true);
    if (manifest_read.values.empty() || !manifest_read.values.front().value) throw std::runtime_error("history manifest unavailable");
    const auto manifest_wire = string_of(*manifest_read.values.front().value);
    const auto manifest = decode_envelope(*client_, room, manifest_wire);
    if (canonical) {
        if (manifest.kind != "manifest" || manifest.sender_main_dht != room.owner_main_dht || manifest.sender_public_key != room.owner_signing_key)
            throw std::runtime_error("canonical manifest is not owner-signed");
        apply_manifest(room, manifest.body, true);
    } else if (manifest.kind != "replica_manifest") throw std::runtime_error("not a replica manifest");
    const auto latest = static_cast<std::uint32_t>(value_u64(manifest.body, "latest_page", 1));
    std::vector<std::uint32_t> locations;
    for (std::uint32_t page = 1; page <= latest && page < room_store_subkeys; ++page) locations.push_back(page);
    const auto pages = client_->read_public_store(record_key, locations, true);
    for (const auto& value : pages.values) {
        if (!value.value) continue;
        try {
            const auto page = decode_envelope(*client_, room, string_of(*value.value));
            if (page.kind != "history_page" && page.kind != "replica_history_page") continue;
            for (const auto& item : page.body.at("messages").as_array()) {
                const auto wire = item.as_string();
                try { apply_decoded_message(room, wire, false); } catch (...) {}
            }
        } catch (...) {}
    }
    std::lock_guard lock(mutex_);
    auto* current = find_room_locked(room.room_id);
    if (current) {
        const bool suspended = current->suspended;
        const auto failures = current->reachability_failures;
        const auto last_error = current->last_reachability_error;
        *current = room;
        if (suspended) {
            current->suspended = true;
            current->reachability_failures = failures;
            current->last_reachability_error = last_error;
        }
        save_locked();
    }
}

void RoomEngine::apply_manifest(Room& room, const veilknit::json::Value& manifest, bool authoritative) {
    if (!authoritative) return;
    room.name = value_string(manifest, "name", room.name);
    room.authority_epoch = value_u64(manifest, "authority_epoch", room.authority_epoch);
    room.manifest_generation = value_u64(manifest, "manifest_generation", room.manifest_generation);
    room.latest_page = static_cast<std::uint32_t>(value_u64(manifest, "latest_page", room.latest_page));
    room.messages_per_page = static_cast<std::uint32_t>(value_u64(manifest, "messages_per_page", room.messages_per_page));
    if (manifest.contains("members")) {
        for (const auto& item : manifest.at("members").as_array()) {
            const auto id = item.at("main_dht").as_string();
            auto& member = room.members[id]; member.main_dht = id;
            member.display_name = value_string(item, "display_name", member.display_name);
            member.signing_key = value_string(item, "signing_key", member.signing_key);
            member.role = role_from_string(value_string(item, "role", "member"));
            member.banned = value_bool(item, "banned");
            member.replica = value_bool(item, "replica");
            member.replica_record_key = value_string(item, "replica_record_key");
            member.max_helpers = static_cast<std::uint32_t>(value_u64(item, "max_helpers"));
        }
    }
    room.banned_phrases.clear();
    if (manifest.contains("banned_phrases")) for (const auto& item : manifest.at("banned_phrases").as_array()) room.banned_phrases.push_back(item.as_string());
    room.deleted_message_ids.clear();
    if (manifest.contains("deleted_message_ids")) for (const auto& item : manifest.at("deleted_message_ids").as_array()) room.deleted_message_ids.push_back(item.as_string());
    for (auto& message : room.messages) message.deleted = std::find(room.deleted_message_ids.begin(), room.deleted_message_ids.end(), message.event_id) != room.deleted_message_ids.end();
}

Role RoomEngine::local_role(const Room& room) const {
    const auto iterator = room.members.find(state_.main_dht);
    return iterator == room.members.end() ? Role::member : iterator->second.role;
}

bool RoomEngine::can_moderate(const Room& room, const std::string& issuer, const std::string& action) const {
    if (issuer == room.owner_main_dht) return true;
    const auto iterator = room.members.find(issuer);
    if (iterator == room.members.end() || iterator->second.banned) return false;
    if (iterator->second.role == Role::moderator) {
        if (action == "grant_moderator") return false;
        return true;
    }
    if (iterator->second.role == Role::helper) return action == "delete";
    return false;
}

void RoomEngine::change_member_role(std::string subject, Role role) {
    enqueue([this, subject = std::move(subject), role] {
        Room room;
        {
            std::lock_guard lock(mutex_); auto* current = selected_room_locked(); if (!current) return; room = *current;
            const auto my_role = local_role(*current);
            if (role == Role::moderator && my_role != Role::owner) throw std::runtime_error("Only the creator can appoint true moderators");
            if (role == Role::helper && my_role != Role::moderator) throw std::runtime_error("Only true moderators can appoint helpers");
            if (subject == current->owner_main_dht) throw std::runtime_error("The creator role cannot be changed here");
            if (my_role != Role::owner && my_role != Role::moderator) throw std::runtime_error("You do not have role-management permission");
            if (my_role == Role::moderator && role == Role::helper) {
                std::size_t helpers = 0; std::uint32_t limit = 0;
                const auto me = current->members.find(state_.main_dht); if (me != current->members.end()) limit = me->second.max_helpers;
                for (const auto& [_, member] : current->members) if (member.role == Role::helper) ++helpers;
                if (limit == 0 || helpers >= limit) throw std::runtime_error("Your helper delegation limit has been reached");
            }
        }
        veilknit::json::Value body = veilknit::json::Value::make_object();
        body["subject"] = subject; body["role"] = role_wire(role); body["max_helpers"] = static_cast<std::uint64_t>(role == Role::moderator ? 4 : 0);
        send_control(room, "role_grant", body);
    });
}

void RoomEngine::toggle_member_ban(std::string subject) {
    enqueue([this, subject = std::move(subject)] {
        Room room; bool banned = false;
        {
            std::lock_guard lock(mutex_); auto* current = selected_room_locked(); if (!current) return; room = *current;
            if (!can_moderate(*current, state_.main_dht, "ban")) throw std::runtime_error("You do not have ban permission");
            const auto member = current->members.find(subject); if (member != current->members.end()) banned = member->second.banned;
            if (subject == current->owner_main_dht) throw std::runtime_error("The creator cannot be banned");
            if (member != current->members.end() && member->second.role == Role::moderator && state_.main_dht != current->owner_main_dht)
                throw std::runtime_error("Only the creator can ban a true moderator");
        }
        veilknit::json::Value body = veilknit::json::Value::make_object();
        body["action"] = banned ? "unban" : "ban"; body["subject"] = subject;
        send_control(room, "moderation", body);
        if (!banned && client_) {
            try { client_->request_app_restriction(subject, veilknit::RestrictionAction::restrict_user, "Banned in room " + room.room_id); } catch (...) {}
        }
    });
}

void RoomEngine::delete_message(std::string event_id) {
    enqueue([this, event_id = std::move(event_id)] {
        Room room;
        {
            std::lock_guard lock(mutex_); auto* current = selected_room_locked(); if (!current) return; room = *current;
            if (!can_moderate(*current, state_.main_dht, "delete")) throw std::runtime_error("You do not have message-deletion permission");
        }
        veilknit::json::Value body = veilknit::json::Value::make_object(); body["action"] = "delete"; body["message_id"] = event_id;
        send_control(room, "moderation", body);
    });
}

void RoomEngine::set_banned_phrases(std::vector<std::string> phrases) {
    enqueue([this, phrases = std::move(phrases)]() mutable {
        Room room;
        {
            std::lock_guard lock(mutex_); auto* current = selected_room_locked(); if (!current) return; room = *current;
            if (!can_moderate(*current, state_.main_dht, "set_phrases")) throw std::runtime_error("You do not have phrase-policy permission");
        }
        veilknit::json::Value body = veilknit::json::Value::make_object(); body["action"] = "set_phrases";
        veilknit::json::Value values = veilknit::json::Value::make_array();
        for (auto& phrase : phrases) { phrase = trim(phrase); if (!phrase.empty()) values.push_back(phrase); }
        body["phrases"] = std::move(values);
        send_control(room, "moderation", body);
    });
}

void RoomEngine::toggle_replica() {
    enqueue([this] {
        Room room;
        {
            std::lock_guard lock(mutex_); auto* current = selected_room_locked(); if (!current) return; room = *current;
        }
        const bool enable = !room.local_replica;
        if (enable && room.owned_store_id.empty()) {
            auto store = client_->create_store(safe_room_store_name(room.name, room.room_id, true), room_store_subkeys, true);
            room.owned_store_id = store.store_id; room.owned_store_subkeys = store.subkey_count; room.owned_store_value_limit = store.max_value_bytes; room.owned_store_generation = store.generation;
            if (room.owner_main_dht == state_.main_dht) room.canonical_record_key = store.record_key;
            auto& self = room.members[state_.main_dht]; self.replica_record_key = store.record_key;
        }
        room.local_replica = enable;
        room.members[state_.main_dht].replica = enable;
        if (enable) room = persist_room(std::move(room));
        {
            std::lock_guard lock(mutex_);
            auto* current = find_room_locked(room.room_id);
            if (current) {
                const bool suspended = current->suspended;
                const auto failures = current->reachability_failures;
                const auto last_error = current->last_reachability_error;
                *current = room;
                if (suspended) {
                    current->suspended = true;
                    current->reachability_failures = failures;
                    current->last_reachability_error = last_error;
                }
            }
            save_locked();
        }
        veilknit::json::Value body = veilknit::json::Value::make_object(); body["enabled"] = enable;
        body["record_key"] = room.members[state_.main_dht].replica_record_key;
        send_control(room, "replica_ad", body);
        notify();
    });
}

void RoomEngine::refresh_member_reputation(std::string subject) {
    enqueue([this, subject = std::move(subject)] {
        if (!client_) return;
        const auto view = client_->reputation_view(subject);
        std::string class_name;
        std::uint8_t confidence = 0;
        if (view.contains("class")) class_name = view.at("class").is_string() ? view.at("class").as_string() : view.at("class").dump();
        if (view.contains("confidence")) confidence = static_cast<std::uint8_t>(view.at("confidence").as_u64());
        {
            std::lock_guard lock(mutex_); auto* room = selected_room_locked(); if (!room) return; auto iterator = room->members.find(subject);
            if (iterator != room->members.end()) { iterator->second.reputation_class = class_name; iterator->second.reputation_confidence = confidence; save_locked(); }
        }
        notify();
    });
}

std::string RoomEngine::selected_invite_code() const {
    std::lock_guard lock(mutex_);
    const auto* room = selected_room_locked();
    if (!room || room->canonical_record_key.empty()) return {};
    return make_invite_code(*room);
}

void RoomEngine::add_system_message(Room& room, std::string text) {
    ChatMessage message;
    message.event_id = random_hex(12); message.room_id = room.room_id; message.sender_name = "Room";
    message.text = std::move(text); message.created_at = unix_time(); message.verified = true; message.system = true;
    room.messages.push_back(std::move(message));
}

void RoomEngine::note_room_failure(const std::string& room_id, const std::string& error) {
    std::lock_guard lock(mutex_);
    auto* room = find_room_locked(room_id);
    if (!room || room->suspended) return;
    room->last_reachability_error = error;
    room->reachability_failures = std::min<std::uint32_t>(room->reachability_failures + 1, 3);
    if (room->reachability_failures >= 3) {
        room->suspended = true;
        if (selected_room_locked() == room) {
            state_.status = "Room paused after repeated failures. Right-click it and choose Retry.";
        }
        log("Paused unreachable room " + room->room_id.substr(0, 8) + ": " + error);
    }
    save_locked();
}

void RoomEngine::note_room_success(const std::string& room_id) {
    std::lock_guard lock(mutex_);
    auto* room = find_room_locked(room_id);
    if (!room || room->suspended) return;
    if (room->reachability_failures == 0 && room->last_reachability_error.empty()) return;
    room->reachability_failures = 0;
    room->last_reachability_error.clear();
    save_locked();
}

} // namespace vkrooms
