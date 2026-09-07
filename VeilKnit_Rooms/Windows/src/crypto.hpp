#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vkrooms {

struct CipherText {
    std::string nonce_hex;
    std::string cipher_base64;
    std::string tag_hex;
};

std::vector<std::uint8_t> sha256(const std::vector<std::uint8_t>& input);
std::vector<std::uint8_t> derive_room_key(const std::string& access_secret_hex,
                                          const std::string& room_id,
                                          std::uint64_t epoch);
CipherText encrypt_room_payload(const std::vector<std::uint8_t>& plaintext,
                                const std::vector<std::uint8_t>& key,
                                const std::string& associated_data);
std::vector<std::uint8_t> decrypt_room_payload(const CipherText& value,
                                               const std::vector<std::uint8_t>& key,
                                               const std::string& associated_data);

} // namespace vkrooms
