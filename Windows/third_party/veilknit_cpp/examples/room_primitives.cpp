#include <veilknit/veilknit.hpp>

#include <iostream>
#include <string>

int main() {
    try {
        const auto credential = veilknit::Client::load_discovered_credential("veilknit.examples.room");
        auto client = veilknit::Client::authenticate(credential);

        auto stores = client.list_stores();
        veilknit::StoreDescriptor room_store;
        if (stores.empty()) {
            room_store = client.create_store("room-manifest-and-pages", 64);
        } else {
            room_store = stores.front();
        }

        const std::string manifest = R"({"version":1,"room_name":"Model Airplanes"})";
        const veilknit::Bytes manifest_bytes(manifest.begin(), manifest.end());
        room_store = client.write_store(
            room_store.store_id,
            {{0, manifest_bytes}},
            room_store.generation);

        const auto signature = client.sign("veilknit/chat/room-manifest/v1", manifest_bytes);
        std::cout << "Room store DHT: " << room_store.record_key << "\n";
        std::cout << "Store generation: " << room_store.generation << "\n";
        std::cout << "App signing key: " << signature.public_key_hex << "\n";
        std::cout << "Manifest signature: " << signature.signature_hex << "\n";
        return 0;
    } catch (const veilknit::Error& error) {
        std::cerr << "VeilKnit error [" << error.code() << "]: " << error.what() << "\n";
        return 1;
    }
}
