#include "room_engine.hpp"
#include "util.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace {
std::mutex output_mutex;

void print_snapshot(const vkrooms::AppSnapshot& snapshot) {
    std::lock_guard lock(output_mutex);
    std::cout << "\nStatus: " << snapshot.status << "\n";
    if (!snapshot.username.empty()) std::cout << "Account: " << snapshot.username << "\n";
    std::cout << "Rooms (" << snapshot.rooms.size() << "):\n";
    for (std::size_t i = 0; i < snapshot.rooms.size(); ++i) {
        const auto& room = snapshot.rooms[i];
        std::cout << (static_cast<int>(i) == snapshot.selected_room ? " * " : "   ")
                  << (i + 1) << ". " << room.name
                  << (room.suspended ? " [suspended]" : "") << "\n";
    }
    std::cout << "> " << std::flush;
}

void print_messages(const vkrooms::AppSnapshot& snapshot) {
    std::lock_guard lock(output_mutex);
    if (snapshot.selected_room < 0 || snapshot.selected_room >= static_cast<int>(snapshot.rooms.size())) {
        std::cout << "No room selected.\n";
        return;
    }
    const auto& room = snapshot.rooms[static_cast<std::size_t>(snapshot.selected_room)];
    std::cout << "--- " << room.name << " ---\n";
    for (const auto& message : room.messages) {
        if (message.deleted) continue;
        std::cout << '[' << vkrooms::format_time(message.created_at) << "] "
                  << (message.sender_name.empty() ? vkrooms::short_identity(message.sender_main_dht) : message.sender_name)
                  << ": " << message.text << (message.pending ? " [pending]" : "") << "\n";
    }
}

void help() {
    std::lock_guard lock(output_mutex);
    std::cout << R"(Commands:
  status                    Show connection and room list
  rooms                     List rooms
  select <number>           Select a room
  messages                  Show selected-room messages
  create <name>             Create a room
  join <invite>             Join from an invite code
  send <message>            Send to the selected room
  sync                      Refresh the selected room
  invite                    Print selected-room invite code
  replica                   Toggle local replication for selected room
  leave                     Remove selected room locally
  reconnect                 Reconnect using saved authorization
  fresh-connect             Request/reuse authorization for current daemon account
  demo                      Enter offline demonstration mode
  help                      Show this help
  quit                      Exit
)";
}
}

int main() {
    std::atomic<bool> changed{false};
    vkrooms::RoomEngine engine([&] { changed.store(true, std::memory_order_release); }, [](const std::string& line) {
        std::lock_guard lock(output_mutex);
        std::cerr << "[rooms] " << line << '\n';
    });
    engine.start();
    engine.connect_async();
    help();

    std::string line;
    while (true) {
        if (changed.exchange(false, std::memory_order_acq_rel)) print_snapshot(engine.snapshot());
        {
            std::lock_guard lock(output_mutex);
            std::cout << "> " << std::flush;
        }
        if (!std::getline(std::cin, line)) break;
        line = vkrooms::trim(std::move(line));
        if (line.empty()) continue;
        const auto space = line.find(' ');
        const std::string command = vkrooms::lowercase_ascii(line.substr(0, space));
        const std::string argument = space == std::string::npos ? std::string{} : vkrooms::trim(line.substr(space + 1));
        try {
            if (command == "quit" || command == "exit") break;
            if (command == "help") help();
            else if (command == "status" || command == "rooms") print_snapshot(engine.snapshot());
            else if (command == "messages") print_messages(engine.snapshot());
            else if (command == "select") {
                const int index = std::stoi(argument);
                engine.select_room(index - 1);
                print_messages(engine.snapshot());
            } else if (command == "create") engine.create_room(argument);
            else if (command == "join") engine.join_room(argument);
            else if (command == "send") engine.send_chat(argument);
            else if (command == "sync") engine.sync_selected_room();
            else if (command == "invite") {
                std::lock_guard lock(output_mutex);
                std::cout << engine.selected_invite_code() << '\n';
            } else if (command == "replica") engine.toggle_replica();
            else if (command == "leave") engine.remove_selected_room();
            else if (command == "reconnect") engine.reconnect_daemon(false);
            else if (command == "fresh-connect" || command == "reauthorize") engine.reconnect_daemon(true);
            else if (command == "demo") engine.start_demo_mode();
            else {
                std::lock_guard lock(output_mutex);
                std::cout << "Unknown command. Type 'help'.\n";
            }
        } catch (const std::exception& error) {
            std::lock_guard lock(output_mutex);
            std::cerr << "Command error: " << error.what() << '\n';
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    engine.stop();
    return 0;
}
