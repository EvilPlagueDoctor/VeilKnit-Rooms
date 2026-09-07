#include "crypto.hpp"
#include "persistence.hpp"
#include "room_engine.hpp"
#include "room_protocol.hpp"
#include "util.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>

int main() {
    using namespace vkrooms;

    const std::vector<std::uint8_t> abc{'a','b','c'};
    assert(bytes_to_hex(sha256(abc)) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    Room room;
    room.room_id = random_hex(16);
    room.name = "Model Airplanes";
    room.owner_main_dht = "VLD0:owner";
    room.owner_signing_key = "001122";
    room.canonical_record_key = "VLD0:store";
    room.access_secret_hex = random_hex(32);
    const auto invite = make_invite_code(room);
    const auto parsed = parse_invite_code(invite);
    assert(parsed.room_id == room.room_id);
    assert(parsed.room_name == room.name);
    assert(parsed.access_secret_hex == room.access_secret_hex);

    const auto key = derive_room_key(room.access_secret_hex, room.room_id, 1);
    const std::vector<std::uint8_t> message{'h','e','l','l','o'};
    const auto cipher = encrypt_room_payload(message, key, "aad");
    assert(decrypt_room_payload(cipher, key, "aad") == message);

    Member owner;
    owner.main_dht = room.owner_main_dht;
    owner.display_name = "Owner";
    owner.role = Role::owner;
    room.members[owner.main_dht] = owner;
    ChatMessage chat;
    chat.event_id = random_hex(12); chat.room_id = room.room_id; chat.sender_name = "Owner"; chat.text = "Hello"; chat.created_at = unix_time();
    room.messages.push_back(chat);
    room.suspended = true;
    room.reachability_failures = 3;
    room.last_reachability_error = "test failure";
    const auto path = std::filesystem::temp_directory_path() / "veilknit-rooms-core-test.json";
    save_rooms(path, {room});
    const auto loaded = load_rooms(path);
    assert(loaded.size() == 1);
    assert(loaded.front().name == room.name);
    assert(loaded.front().messages.front().text == "Hello");
    assert(loaded.front().suspended);
    assert(loaded.front().reachability_failures == 3);
    assert(loaded.front().last_reachability_error == "test failure");
    std::filesystem::remove(path);

    const auto isolated_data = std::filesystem::temp_directory_path() / "veilknit-rooms-engine-test";
    std::filesystem::remove_all(isolated_data);
#ifdef _WIN32
    _putenv_s("LOCALAPPDATA", isolated_data.string().c_str());
#else
    setenv("XDG_DATA_HOME", isolated_data.string().c_str(), 1);
#endif
    RoomEngine engine([]{});
    engine.start();
    engine.start_demo_mode();
    auto snapshot = engine.snapshot();
    assert(snapshot.demo_mode);
    assert(!snapshot.rooms.empty());
    engine.stop();
    std::filesystem::remove_all(isolated_data);

    std::cout << "VeilKnit Rooms core tests passed\n";
    return 0;
}
