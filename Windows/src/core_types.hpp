#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vkrooms {

enum class ConnectionState { disconnected, connecting, authorizing, connected, error };
enum class Role { owner, moderator, helper, member };

inline const char* role_name(Role role) {
    switch (role) {
        case Role::owner: return "Owner";
        case Role::moderator: return "Moderator";
        case Role::helper: return "Helper";
        default: return "Member";
    }
}

inline Role role_from_string(const std::string& value) {
    if (value == "owner") return Role::owner;
    if (value == "moderator") return Role::moderator;
    if (value == "helper") return Role::helper;
    return Role::member;
}

inline const char* role_wire(Role role) {
    switch (role) {
        case Role::owner: return "owner";
        case Role::moderator: return "moderator";
        case Role::helper: return "helper";
        default: return "member";
    }
}

struct Member {
    std::string main_dht;
    std::string display_name;
    std::string signing_key;
    Role role = Role::member;
    bool online = false;
    bool banned = false;
    bool replica = false;
    std::uint64_t last_seen = 0;
    std::uint32_t max_helpers = 0;
    std::string replica_record_key;
    std::string reputation_class;
    std::uint8_t reputation_confidence = 0;
};

struct ChatMessage {
    std::string event_id;
    std::string room_id;
    std::string sender_main_dht;
    std::string sender_name;
    std::string sender_signing_key;
    std::string text;
    std::string wire_json;
    std::uint64_t created_at = 0;
    bool verified = false;
    bool pending = false;
    bool deleted = false;
    bool system = false;
    bool recovery = false;
};

struct ReplicaInfo {
    std::string main_dht;
    std::string record_key;
    std::uint64_t generation = 0;
    std::uint64_t last_seen = 0;
};

struct Room {
    std::string room_id;
    std::string name;
    std::string owner_main_dht;
    std::string owner_signing_key;
    std::string access_secret_hex;
    std::string canonical_record_key;
    std::string owned_store_id;
    std::uint16_t owned_store_subkeys = 0;
    std::size_t owned_store_value_limit = 0;
    std::uint64_t owned_store_generation = 0;
    std::uint64_t authority_epoch = 1;
    std::uint64_t manifest_generation = 0;
    std::uint32_t latest_page = 1;
    std::uint32_t messages_per_page = 4;
    std::uint64_t created_at = 0;
    std::uint64_t owner_last_seen = 0;
    bool local_replica = false;
    bool joined = true;
    bool suspended = false;
    std::uint32_t reachability_failures = 0;
    std::string last_reachability_error;
    std::map<std::string, Member> members;
    std::vector<ReplicaInfo> replicas;
    std::vector<ChatMessage> messages;
    std::vector<std::string> banned_phrases;
    std::vector<std::string> deleted_message_ids;
};

struct AppSnapshot {
    ConnectionState connection = ConnectionState::disconnected;
    std::string status = "Not connected";
    std::string username;
    std::string main_dht;
    std::string signing_key;
    std::vector<Room> rooms;
    int selected_room = -1;
    bool demo_mode = false;
    bool room_creation_in_progress = false;
};

} // namespace vkrooms
