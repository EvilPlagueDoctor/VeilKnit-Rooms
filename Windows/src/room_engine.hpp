#pragma once

#include "core_types.hpp"
#include <veilknit/json.hpp>

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace veilknit { class Client; class MessageSubscription; }

namespace vkrooms {

class RoomEngine {
public:
    using Notify = std::function<void()>;
    using Log = std::function<void(const std::string&)>;

    explicit RoomEngine(Notify notify, Log log = {});
    ~RoomEngine();
    RoomEngine(const RoomEngine&) = delete;
    RoomEngine& operator=(const RoomEngine&) = delete;

    void start();
    void stop();
    AppSnapshot snapshot() const;

    void connect_async();
    void reconnect_daemon(bool reset_credential = false);
    void submit_text(std::string text);
    void start_demo_mode();
    void select_room(int index);
    void remove_selected_room();
    void retry_selected_room();
    void create_room(std::string name);
    void join_room(std::string invite_code);
    void send_chat(std::string text);
    void sync_selected_room();
    void toggle_replica();
    void change_member_role(std::string subject_main_dht, Role role);
    void toggle_member_ban(std::string subject_main_dht);
    void delete_message(std::string event_id);
    void set_banned_phrases(std::vector<std::string> phrases);
    void refresh_member_reputation(std::string subject_main_dht);

    std::string selected_invite_code() const;

private:
    void enqueue(std::function<void()> operation);
    void operation_loop();
    void subscription_loop();
    void heartbeat_loop();
    void notify();
    void log(std::string text) const;
    void set_status(ConnectionState state, std::string text);
    void save_locked();
    Room* find_room_locked(const std::string& room_id);
    const Room* selected_room_locked() const;
    Room* selected_room_locked();

    void connect_impl();
    void reconnect_impl(bool reset_credential);
    void create_room_impl(const std::string& name);
    void join_room_impl(const std::string& invite_code);
    void send_chat_impl(const std::string& room_id, const std::string& text);
    void handle_wire_message(const std::string& wire, bool allow_relay = true);
    void apply_decoded_message(Room& room, const std::string& wire, bool allow_relay);
    void sync_room_impl(const std::string& room_id);
    void sync_from_record(Room& room, const std::string& record_key, bool canonical);
    void send_envelope_to_room(Room& room, const std::string& wire, const std::string& omit = {});
    void send_control(Room& room, const std::string& kind, const veilknit::json::Value& body,
                      const std::string& direct_recipient = {});
    Room persist_room(Room room);
    void apply_manifest(Room& room, const veilknit::json::Value& manifest, bool authoritative);
    bool event_seen(const Room& room, const std::string& event_id) const;
    Role local_role(const Room& room) const;
    bool can_moderate(const Room& room, const std::string& issuer, const std::string& action) const;
    void add_system_message(Room& room, std::string text);
    void note_room_failure(const std::string& room_id, const std::string& error);
    void note_room_success(const std::string& room_id);

    Notify notify_;
    Log log_;
    mutable std::recursive_mutex mutex_;
    AppSnapshot state_;
    std::filesystem::path database_path_;
    std::filesystem::path credential_path_;

    std::unique_ptr<veilknit::Client> client_;
    std::unique_ptr<veilknit::MessageSubscription> subscription_;
    std::thread operation_thread_;
    std::thread subscription_thread_;
    std::thread heartbeat_thread_;
    std::condition_variable_any operation_cv_;
    std::deque<std::function<void()>> operations_;
    bool stopping_ = false;
    bool started_ = false;
    bool reconnecting_ = false;
};

} // namespace vkrooms
