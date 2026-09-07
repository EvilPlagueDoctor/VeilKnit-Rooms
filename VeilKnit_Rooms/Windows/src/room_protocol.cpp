#include "room_protocol.hpp"

#include "crypto.hpp"
#include "util.hpp"

#include <stdexcept>

namespace vkrooms {
namespace {

veilknit::Bytes as_bytes(const std::string& value) {
    return veilknit::Bytes(value.begin(), value.end());
}

std::string signing_domain(const std::string& kind) {
    return "veilknit/rooms/" + kind + "/v1";
}

std::string base64url_text(const std::string& value) {
    return base64url_encode(std::vector<std::uint8_t>(value.begin(), value.end()));
}

void append_text_field(std::string& output, const char* name, const std::string& value) {
    output += name;
    output.push_back('=');
    output += base64url_text(value);
    output.push_back('\n');
}

void append_number_field(std::string& output, const char* name, std::uint64_t value) {
    output += name;
    output.push_back('=');
    output += std::to_string(value);
    output.push_back('\n');
}

// Protocol-v2 signatures do not depend on either platform's JSON parser or
// serializer. Every textual value is UTF-8/base64url encoded and every field
// appears in one fixed order.
std::string signature_payload_v2(const veilknit::json::Value& envelope) {
    std::string output = "VKROOMSIG2\n";
    append_text_field(output, "protocol", envelope.at("protocol").as_string());
    append_number_field(output, "version", envelope.at("version").as_u64());
    append_text_field(output, "kind", envelope.at("kind").as_string());
    append_text_field(output, "room_id", envelope.at("room_id").as_string());
    append_text_field(output, "event_id", envelope.at("event_id").as_string());
    append_text_field(output, "sender_main_dht", envelope.at("sender_main_dht").as_string());
    append_text_field(output, "sender_name", envelope.at("sender_name").as_string());
    append_text_field(output, "sender_public_key", envelope.at("sender_public_key").as_string());
    append_number_field(output, "authority_epoch", envelope.at("authority_epoch").as_u64());
    append_number_field(output, "created_at", envelope.at("created_at").as_u64());
    append_text_field(output, "nonce_hex", envelope.at("nonce_hex").as_string());
    append_text_field(output, "cipher_base64", envelope.at("cipher_base64").as_string());
    append_text_field(output, "tag_hex", envelope.at("tag_hex").as_string());
    return output;
}

// A v1 sender signed the canonical JSON before adding signature_hex. Removing
// that one top-level member from the received canonical wire preserves the
// exact bytes the sender signed and gives us a compatibility path when two JSON
// implementations serialize an otherwise equivalent value differently.
std::string strip_signature_member(const std::string& wire) {
    constexpr const char* key = "\"signature_hex\"";
    const auto key_position = wire.find(key);
    if (key_position == std::string::npos) return {};

    auto colon = wire.find(':', key_position + std::char_traits<char>::length(key));
    if (colon == std::string::npos) return {};
    auto value_start = wire.find('"', colon + 1);
    if (value_start == std::string::npos) return {};

    bool escaped = false;
    std::size_t value_end = value_start + 1;
    for (; value_end < wire.size(); ++value_end) {
        const char character = wire[value_end];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (character == '\\') {
            escaped = true;
            continue;
        }
        if (character == '"') {
            ++value_end;
            break;
        }
    }
    if (value_end > wire.size()) return {};

    std::size_t erase_start = key_position;
    std::size_t erase_end = value_end;
    if (erase_start > 0 && wire[erase_start - 1] == ',') {
        --erase_start;
    } else if (erase_end < wire.size() && wire[erase_end] == ',') {
        ++erase_end;
    }

    return wire.substr(0, erase_start) + wire.substr(erase_end);
}

} // namespace

std::string signed_envelope(veilknit::Client& client,
                            const Room& room,
                            const std::string& kind,
                            const std::string& sender_main_dht,
                            const std::string& sender_name,
                            const std::string& sender_public_key,
                            const veilknit::json::Value& body,
                            std::string event_id) {
    if (event_id.empty()) event_id = random_hex(16);
    const auto created = unix_time();
    const auto key = derive_room_key(room.access_secret_hex, room.room_id, room.authority_epoch);
    const std::string associated = room.room_id + ":" + kind + ":" + event_id;
    const auto body_json = body.dump();
    const auto encrypted = encrypt_room_payload(as_bytes(body_json), key, associated);

    veilknit::json::Value envelope = veilknit::json::Value::make_object();
    envelope["protocol"] = "veilknit.rooms";
    envelope["version"] = static_cast<std::uint64_t>(2);
    envelope["kind"] = kind;
    envelope["room_id"] = room.room_id;
    envelope["event_id"] = event_id;
    envelope["sender_main_dht"] = sender_main_dht;
    envelope["sender_name"] = sender_name;
    envelope["sender_public_key"] = sender_public_key;
    envelope["authority_epoch"] = room.authority_epoch;
    envelope["created_at"] = created;
    envelope["nonce_hex"] = encrypted.nonce_hex;
    envelope["cipher_base64"] = encrypted.cipher_base64;
    envelope["tag_hex"] = encrypted.tag_hex;

    auto signature = client.sign(signing_domain(kind), as_bytes(signature_payload_v2(envelope)));
    if (signature.public_key_hex != envelope.at("sender_public_key").as_string()) {
        // The daemon is authoritative about which key actually signed. This
        // can happen after app-ID migration or key rotation. Rebuild and sign
        // once more so sender_public_key and signature always agree.
        envelope["sender_public_key"] = signature.public_key_hex;
        signature = client.sign(signing_domain(kind), as_bytes(signature_payload_v2(envelope)));
        if (signature.public_key_hex != envelope.at("sender_public_key").as_string()) {
            throw std::runtime_error("daemon application signing key changed while creating an envelope");
        }
    }
    envelope["signature_hex"] = signature.signature_hex;
    return envelope.dump();
}

DecodedEnvelope decode_envelope(veilknit::Client& client,
                                const Room& room,
                                const std::string& wire_json) {
    const auto parsed = veilknit::json::Value::parse(wire_json);
    if (parsed.at("protocol").as_string() != "veilknit.rooms") throw std::runtime_error("not a VeilKnit Rooms envelope");
    const auto version = parsed.at("version").as_u64();
    if (version != 1 && version != 2) throw std::runtime_error("unsupported room protocol version");
    if (parsed.at("room_id").as_string() != room.room_id) throw std::runtime_error("room id mismatch");

    veilknit::json::Value canonical = parsed;
    auto& object = canonical.as_object();
    const auto signature_iterator = object.find("signature_hex");
    if (signature_iterator == object.end()) throw std::runtime_error("missing envelope signature");
    const std::string signature = signature_iterator->second.as_string();
    object.erase(signature_iterator);

    DecodedEnvelope result;
    result.kind = parsed.at("kind").as_string();
    result.room_id = parsed.at("room_id").as_string();
    result.event_id = parsed.at("event_id").as_string();
    result.sender_main_dht = parsed.at("sender_main_dht").as_string();
    result.sender_name = parsed.at("sender_name").as_string();
    result.sender_public_key = parsed.at("sender_public_key").as_string();
    result.created_at = parsed.at("created_at").as_u64();
    result.wire_json = wire_json;

    if (version == 2) {
        result.verified = client.verify(result.sender_public_key,
                                        signing_domain(result.kind),
                                        as_bytes(signature_payload_v2(canonical)),
                                        signature);
    } else {
        result.verified = client.verify(result.sender_public_key,
                                        signing_domain(result.kind),
                                        as_bytes(canonical.dump()),
                                        signature);
        if (!result.verified) {
            const auto exact_v1_payload = strip_signature_member(wire_json);
            if (!exact_v1_payload.empty()) {
                result.verified = client.verify(result.sender_public_key,
                                                signing_domain(result.kind),
                                                as_bytes(exact_v1_payload),
                                                signature);
            }
        }
    }
    if (!result.verified) {
        throw std::runtime_error("invalid room envelope signature (protocol v" + std::to_string(version) + ")");
    }

    CipherText encrypted;
    encrypted.nonce_hex = parsed.at("nonce_hex").as_string();
    encrypted.cipher_base64 = parsed.at("cipher_base64").as_string();
    encrypted.tag_hex = parsed.at("tag_hex").as_string();
    const auto key = derive_room_key(room.access_secret_hex, room.room_id, parsed.at("authority_epoch").as_u64());
    const std::string associated = result.room_id + ":" + result.kind + ":" + result.event_id;
    const auto plaintext = decrypt_room_payload(encrypted, key, associated);
    result.body = veilknit::json::Value::parse(std::string(plaintext.begin(), plaintext.end()));
    return result;
}

std::string make_invite_code(const Room& room) {
    veilknit::json::Value invite = veilknit::json::Value::make_object();
    invite["v"] = static_cast<std::uint64_t>(1);
    invite["room_id"] = room.room_id;
    invite["room_name"] = room.name;
    invite["owner_main_dht"] = room.owner_main_dht;
    invite["owner_public_key"] = room.owner_signing_key;
    invite["record_key"] = room.canonical_record_key;
    invite["access_secret_hex"] = room.access_secret_hex;
    invite["authority_epoch"] = room.authority_epoch;
    const auto json = invite.dump();
    return "VKROOM1:" + base64url_encode(std::vector<std::uint8_t>(json.begin(), json.end()));
}

RoomInvite parse_invite_code(const std::string& original) {
    const auto code = trim(original);
    constexpr const char* prefix = "VKROOM1:";
    if (code.rfind(prefix, 0) != 0) throw std::runtime_error("invite code does not begin with VKROOM1:");
    const auto bytes = base64url_decode(code.substr(std::char_traits<char>::length(prefix)));
    const auto value = veilknit::json::Value::parse(std::string(bytes.begin(), bytes.end()));
    if (value.at("v").as_u64() != 1) throw std::runtime_error("unsupported invite code version");
    RoomInvite invite;
    invite.room_id = value.at("room_id").as_string();
    invite.room_name = value.at("room_name").as_string();
    invite.owner_main_dht = value.at("owner_main_dht").as_string();
    invite.owner_public_key = value.at("owner_public_key").as_string();
    invite.record_key = value.at("record_key").as_string();
    invite.access_secret_hex = value.at("access_secret_hex").as_string();
    invite.authority_epoch = value.at("authority_epoch").as_u64();
    return invite;
}

veilknit::json::Value room_manifest_body(const Room& room) {
    veilknit::json::Value manifest = veilknit::json::Value::make_object();
    manifest["schema"] = "veilknit.rooms.manifest.v1";
    manifest["room_id"] = room.room_id;
    manifest["name"] = room.name;
    manifest["owner_main_dht"] = room.owner_main_dht;
    manifest["owner_signing_key"] = room.owner_signing_key;
    manifest["authority_epoch"] = room.authority_epoch;
    manifest["manifest_generation"] = room.manifest_generation;
    manifest["latest_page"] = room.latest_page;
    manifest["messages_per_page"] = room.messages_per_page;
    manifest["updated_at"] = unix_time();

    veilknit::json::Value members = veilknit::json::Value::make_array();
    for (const auto& [_, member] : room.members) {
        veilknit::json::Value value = veilknit::json::Value::make_object();
        value["main_dht"] = member.main_dht;
        value["display_name"] = member.display_name;
        value["signing_key"] = member.signing_key;
        value["role"] = role_wire(member.role);
        value["banned"] = member.banned;
        value["replica"] = member.replica;
        value["replica_record_key"] = member.replica_record_key;
        value["max_helpers"] = static_cast<std::uint64_t>(member.max_helpers);
        members.push_back(std::move(value));
    }
    manifest["members"] = std::move(members);

    veilknit::json::Value phrases = veilknit::json::Value::make_array();
    for (const auto& phrase : room.banned_phrases) phrases.push_back(phrase);
    manifest["banned_phrases"] = std::move(phrases);

    veilknit::json::Value deleted = veilknit::json::Value::make_array();
    for (const auto& id : room.deleted_message_ids) deleted.push_back(id);
    manifest["deleted_message_ids"] = std::move(deleted);
    return manifest;
}

} // namespace vkrooms
