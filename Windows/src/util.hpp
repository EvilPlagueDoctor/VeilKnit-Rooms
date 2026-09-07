#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vkrooms {

std::uint64_t unix_time();
std::string random_hex(std::size_t byte_count);
std::vector<std::uint8_t> random_bytes(std::size_t byte_count);
std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> hex_to_bytes(const std::string& text);
std::string base64url_encode(const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> base64url_decode(const std::string& text);
std::filesystem::path app_data_directory();
std::wstring utf8_to_wide(const std::string& text);
std::string wide_to_utf8(const std::wstring& text);
std::string short_identity(const std::string& value, std::size_t keep = 9);
std::string format_time(std::uint64_t timestamp);
std::string lowercase_ascii(std::string value);
std::string trim(std::string value);

} // namespace vkrooms
