# VeilKnit C++ SDK 0.1.0

A dependency-free C++17 client for the VeilKnit daemon local API protocol v1.
It supports Windows named pipes and Unix-domain sockets, and includes a Visual
Studio 2022 CMake preset.

## Included API areas

- First-run application authorization and credential discovery
- HMAC-SHA256 challenge-response authentication
- Local identity and network status
- Direct/mailbox message submission and live message subscriptions
- Daemon-held Ed25519 application signing keys
- Application-owned DHT stores with generation guards
- Read-only access to shared/public DHT stores
- App-scoped reputation observations, restrictions, and source reports

The app never receives a DHT writer descriptor or private signing key. Those
remain in the daemon and are gated by the approved application capabilities.

## Visual Studio 2022

1. Open Visual Studio 2022.
2. Choose **Open a local folder** and select this SDK folder.
3. Select the `vs2022-x64` CMake preset.
4. Build `veilknit_cpp`, `veilknit_hello`, or `veilknit_room_primitives`.

The public header is:

```cpp
#include <veilknit/veilknit.hpp>
```

Link the `VeilKnit::cpp` CMake target.

## First connection

```cpp
#include <veilknit/veilknit.hpp>
#include <filesystem>
#include <iostream>

int main() {
    constexpr const char* app_id = "my.company.chat";

    veilknit::Credential credential;
    try {
        credential = veilknit::Client::load_discovered_credential(app_id);
    } catch (const veilknit::Error& error) {
        if (error.code() != "credential_not_found") throw;

        const auto endpoint = veilknit::Client::discover_endpoint();
        credential = veilknit::Client::register_app(
            endpoint,
            app_id,
            "My Chat App");
        credential.save(
            std::filesystem::path("app_credentials") /
            (std::string(app_id) + ".json"));
    }

    auto client = veilknit::Client::authenticate(credential);
    std::cout << client.identity().main_dht << "\n";
}
```

`register_app` creates a pending request in the daemon and polls until the local
user approves or rejects it. The returned secret should be stored as a private
credential file and never published.

## Messaging

```cpp
veilknit::Bytes payload{'h', 'e', 'l', 'l', 'o'};
const auto message_id = client.send_message(recipient_main_dht, payload);

auto subscription = client.subscribe_messages();
for (;;) {
    const auto message = subscription.next();
    // message.payload is the original binary payload.
}
```

The local daemon chooses direct delivery when possible and falls back to the
mailbox system. A queued receipt is not the same as room-history acceptance;
room applications should add their own signed acceptance receipts.

## Application signing

```cpp
const std::string manifest = R"({"room":"Model Airplanes"})";
veilknit::Bytes bytes(manifest.begin(), manifest.end());

const auto signature = client.sign(
    "veilknit/chat/room-manifest/v1",
    bytes);

const bool valid = client.verify(
    signature.public_key_hex,
    signature.domain,
    bytes,
    signature.signature_hex);
```

Use a distinct domain for every signed object type. The daemon-held app key is
bound to the local account, but remote clients still need to learn or pin the
public key through a signed room/profile record.

## Application-owned DHT stores

```cpp
auto store = client.create_store("room-history", 128);

const veilknit::Bytes page{/* serialized room page */};
store = client.write_store(
    store.store_id,
    {{1, page}},
    store.generation);

const auto read = client.read_store(store.store_id, {0, 1, 2}, true);
```

`expected_generation` prevents two local app operations from silently updating
an outdated store descriptor. A multi-subkey write is not a network-wide atomic
transaction: an interrupted DHT write may partially complete. Room data should
therefore use manifests, immutable pages, content hashes, and a final manifest
commit.

Current limits are discoverable with `Client::api_info(endpoint)` rather than
being hard-coded by applications.

## Public and private room encryption

Encryption is intentionally left to the application protocol. An open-but-
obscured room can derive page keys from a room access secret and epoch. A truly
private room should distribute rotating epoch keys only to current members.
The daemon storage API stores opaque bytes and does not interpret room content.

## Error handling

All SDK failures throw `veilknit::Error`:

```cpp
try {
    // API call
} catch (const veilknit::Error& error) {
    std::cerr << error.code() << ": " << error.what() << "\n";
}
```

## Build from a terminal

```text
cmake -S . -B out/build
cmake --build out/build
ctest --test-dir out/build --output-on-failure
```

## Protocol version

This SDK targets the first unreleased local API baseline, protocol **1**. Older
development credentials and wire formats are intentionally not migrated.
