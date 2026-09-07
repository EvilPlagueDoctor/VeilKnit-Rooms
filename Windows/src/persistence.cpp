#include "persistence.hpp"

#include <veilknit/json.hpp>

#include <fstream>
#include <stdexcept>

namespace vkrooms {
namespace {

using veilknit::json::Value;

std::string optional_string(const Value& value, const char* key, std::string fallback = {}) {
    if (!value.contains(key) || value.at(key).is_null()) return fallback;
    return value.at(key).as_string();
}

std::uint64_t optional_u64(const Value& value, const char* key, std::uint64_t fallback = 0) {
    if (!value.contains(key) || value.at(key).is_null()) return fallback;
    return value.at(key).as_u64();
}

bool optional_bool(const Value& value, const char* key, bool fallback = false) {
    if (!value.contains(key) || value.at(key).is_null()) return fallback;
    return value.at(key).as_bool();
}

Value member_to_json(const Member& member) {
    Value value = Value::make_object();
    value["main_dht"] = member.main_dht;
    value["display_name"] = member.display_name;
    value["signing_key"] = member.signing_key;
    value["role"] = role_wire(member.role);
    value["online"] = member.online;
    value["banned"] = member.banned;
    value["replica"] = member.replica;
    value["last_seen"] = member.last_seen;
    value["max_helpers"] = static_cast<std::uint64_t>(member.max_helpers);
    value["replica_record_key"] = member.replica_record_key;
    value["reputation_class"] = member.reputation_class;
    value["reputation_confidence"] = static_cast<std::uint64_t>(member.reputation_confidence);
    return value;
}

Member member_from_json(const Value& value) {
    Member member;
    member.main_dht = value.at("main_dht").as_string();
    member.display_name = optional_string(value, "display_name", "Unknown");
    member.signing_key = optional_string(value, "signing_key");
    member.role = role_from_string(optional_string(value, "role", "member"));
    member.online = optional_bool(value, "online");
    member.banned = optional_bool(value, "banned");
    member.replica = optional_bool(value, "replica");
    member.last_seen = optional_u64(value, "last_seen");
    member.max_helpers = static_cast<std::uint32_t>(optional_u64(value, "max_helpers"));
    member.replica_record_key = optional_string(value, "replica_record_key");
    member.reputation_class = optional_string(value, "reputation_class");
    member.reputation_confidence = static_cast<std::uint8_t>(optional_u64(value, "reputation_confidence"));
    return member;
}

Value message_to_json(const ChatMessage& message) {
    Value value = Value::make_object();
    value["event_id"] = message.event_id;
    value["room_id"] = message.room_id;
    value["sender_main_dht"] = message.sender_main_dht;
    value["sender_name"] = message.sender_name;
    value["sender_signing_key"] = message.sender_signing_key;
    value["text"] = message.text;
    value["wire_json"] = message.wire_json;
    value["created_at"] = message.created_at;
    value["verified"] = message.verified;
    value["pending"] = message.pending;
    value["deleted"] = message.deleted;
    value["system"] = message.system;
    value["recovery"] = message.recovery;
    return value;
}

ChatMessage message_from_json(const Value& value) {
    ChatMessage message;
    message.event_id = optional_string(value, "event_id");
    message.room_id = optional_string(value, "room_id");
    message.sender_main_dht = optional_string(value, "sender_main_dht");
    message.sender_name = optional_string(value, "sender_name");
    message.sender_signing_key = optional_string(value, "sender_signing_key");
    message.text = optional_string(value, "text");
    message.wire_json = optional_string(value, "wire_json");
    message.created_at = optional_u64(value, "created_at");
    message.verified = optional_bool(value, "verified");
    message.pending = optional_bool(value, "pending");
    message.deleted = optional_bool(value, "deleted");
    message.system = optional_bool(value, "system");
    message.recovery = optional_bool(value, "recovery");
    return message;
}

Value room_to_json(const Room& room) {
    Value value = Value::make_object();
    value["room_id"] = room.room_id;
    value["name"] = room.name;
    value["owner_main_dht"] = room.owner_main_dht;
    value["owner_signing_key"] = room.owner_signing_key;
    value["access_secret_hex"] = room.access_secret_hex;
    value["canonical_record_key"] = room.canonical_record_key;
    value["owned_store_id"] = room.owned_store_id;
    value["owned_store_subkeys"] = static_cast<std::uint64_t>(room.owned_store_subkeys);
    value["owned_store_value_limit"] = static_cast<std::uint64_t>(room.owned_store_value_limit);
    value["owned_store_generation"] = room.owned_store_generation;
    value["authority_epoch"] = room.authority_epoch;
    value["manifest_generation"] = room.manifest_generation;
    value["latest_page"] = static_cast<std::uint64_t>(room.latest_page);
    value["messages_per_page"] = static_cast<std::uint64_t>(room.messages_per_page);
    value["created_at"] = room.created_at;
    value["owner_last_seen"] = room.owner_last_seen;
    value["local_replica"] = room.local_replica;
    value["joined"] = room.joined;
    value["suspended"] = room.suspended;
    value["reachability_failures"] = static_cast<std::uint64_t>(room.reachability_failures);
    value["last_reachability_error"] = room.last_reachability_error;

    Value members = Value::make_array();
    for (const auto& [_, member] : room.members) members.push_back(member_to_json(member));
    value["members"] = std::move(members);

    Value replicas = Value::make_array();
    for (const auto& replica : room.replicas) {
        Value item = Value::make_object();
        item["main_dht"] = replica.main_dht;
        item["record_key"] = replica.record_key;
        item["generation"] = replica.generation;
        item["last_seen"] = replica.last_seen;
        replicas.push_back(std::move(item));
    }
    value["replicas"] = std::move(replicas);

    Value messages = Value::make_array();
    const std::size_t start = room.messages.size() > 1000 ? room.messages.size() - 1000 : 0;
    for (std::size_t i = start; i < room.messages.size(); ++i) messages.push_back(message_to_json(room.messages[i]));
    value["messages"] = std::move(messages);

    Value phrases = Value::make_array();
    for (const auto& phrase : room.banned_phrases) phrases.push_back(phrase);
    value["banned_phrases"] = std::move(phrases);
    Value deleted = Value::make_array();
    for (const auto& id : room.deleted_message_ids) deleted.push_back(id);
    value["deleted_message_ids"] = std::move(deleted);
    return value;
}

Room room_from_json(const Value& value) {
    Room room;
    room.room_id = value.at("room_id").as_string();
    room.name = optional_string(value, "name", "Unnamed room");
    room.owner_main_dht = optional_string(value, "owner_main_dht");
    room.owner_signing_key = optional_string(value, "owner_signing_key");
    room.access_secret_hex = optional_string(value, "access_secret_hex");
    room.canonical_record_key = optional_string(value, "canonical_record_key");
    room.owned_store_id = optional_string(value, "owned_store_id");
    room.owned_store_subkeys = static_cast<std::uint16_t>(optional_u64(value, "owned_store_subkeys"));
    room.owned_store_value_limit = static_cast<std::size_t>(optional_u64(value, "owned_store_value_limit"));
    room.owned_store_generation = optional_u64(value, "owned_store_generation");
    room.authority_epoch = optional_u64(value, "authority_epoch", 1);
    room.manifest_generation = optional_u64(value, "manifest_generation");
    room.latest_page = static_cast<std::uint32_t>(optional_u64(value, "latest_page", 1));
    room.messages_per_page = static_cast<std::uint32_t>(optional_u64(value, "messages_per_page", 4));
    room.created_at = optional_u64(value, "created_at");
    room.owner_last_seen = optional_u64(value, "owner_last_seen");
    room.local_replica = optional_bool(value, "local_replica");
    room.joined = optional_bool(value, "joined", true);
    room.suspended = optional_bool(value, "suspended");
    room.reachability_failures = static_cast<std::uint32_t>(optional_u64(value, "reachability_failures"));
    room.last_reachability_error = optional_string(value, "last_reachability_error");
    if (value.contains("members")) {
        for (const auto& item : value.at("members").as_array()) {
            auto member = member_from_json(item);
            room.members[member.main_dht] = std::move(member);
        }
    }
    if (value.contains("replicas")) {
        for (const auto& item : value.at("replicas").as_array()) {
            room.replicas.push_back({optional_string(item, "main_dht"), optional_string(item, "record_key"),
                                     optional_u64(item, "generation"), optional_u64(item, "last_seen")});
        }
    }
    if (value.contains("messages")) for (const auto& item : value.at("messages").as_array()) room.messages.push_back(message_from_json(item));
    if (value.contains("banned_phrases")) for (const auto& item : value.at("banned_phrases").as_array()) room.banned_phrases.push_back(item.as_string());
    if (value.contains("deleted_message_ids")) for (const auto& item : value.at("deleted_message_ids").as_array()) room.deleted_message_ids.push_back(item.as_string());
    return room;
}

} // namespace

std::vector<Room> load_rooms(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return {};
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto root = Value::parse(text);
    std::vector<Room> rooms;
    if (!root.contains("rooms")) return rooms;
    for (const auto& item : root.at("rooms").as_array()) rooms.push_back(room_from_json(item));
    return rooms;
}

void save_rooms(const std::filesystem::path& path, const std::vector<Room>& rooms) {
    Value root = Value::make_object();
    root["version"] = static_cast<std::uint64_t>(1);
    Value values = Value::make_array();
    for (const auto& room : rooms) values.push_back(room_to_json(room));
    root["rooms"] = std::move(values);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("could not open room database for writing");
        output << root.dump();
        output.flush();
        if (!output) throw std::runtime_error("could not write room database");
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
        if (error) throw std::runtime_error("could not replace room database");
    }
}

} // namespace vkrooms
