#pragma once

#include "core_types.hpp"
#include <veilknit/veilknit.hpp>

#include <string>

namespace vkrooms {

struct DecodedEnvelope {
    std::string kind;
    std::string room_id;
    std::string event_id;
    std::string sender_main_dht;
    std::string sender_name;
    std::string sender_public_key;
    std::uint64_t created_at = 0;
    veilknit::json::Value body;
    bool verified = false;
    std::string wire_json;
};

struct RoomInvite {
    std::string room_id;
    std::string room_name;
    std::string owner_main_dht;
    std::string owner_public_key;
    std::string record_key;
    std::string access_secret_hex;
    std::uint64_t authority_epoch = 1;
};

std::string signed_envelope(veilknit::Client& client,
                            const Room& room,
                            const std::string& kind,
                            const std::string& sender_main_dht,
                            const std::string& sender_name,
                            const std::string& sender_public_key,
                            const veilknit::json::Value& body,
                            std::string event_id = {});

DecodedEnvelope decode_envelope(veilknit::Client& client,
                                const Room& room,
                                const std::string& wire_json);

std::string make_invite_code(const Room& room);
RoomInvite parse_invite_code(const std::string& code);
veilknit::json::Value room_manifest_body(const Room& room);

} // namespace vkrooms
