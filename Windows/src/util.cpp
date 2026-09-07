#include "util.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iterator>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <codecvt>
#include <locale>
#include <openssl/rand.h>
#endif

namespace vkrooms {

std::uint64_t unix_time() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::vector<std::uint8_t> random_bytes(std::size_t byte_count) {
    std::vector<std::uint8_t> bytes(byte_count);
#ifdef _WIN32
    if (byte_count != 0 && BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        throw std::runtime_error("BCryptGenRandom failed");
    }
#else
    if (byte_count != 0 && RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
        throw std::runtime_error("OpenSSL RAND_bytes failed");
    }
#endif
    return bytes;
}

std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.resize(bytes.size() * 2);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        output[i * 2] = hex[(bytes[i] >> 4) & 0x0f];
        output[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    return output;
}

std::vector<std::uint8_t> hex_to_bytes(const std::string& text) {
    if ((text.size() % 2) != 0) throw std::runtime_error("invalid hexadecimal string");
    auto digit = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(10 + c - 'a');
        if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(10 + c - 'A');
        throw std::runtime_error("invalid hexadecimal digit");
    };
    std::vector<std::uint8_t> output(text.size() / 2);
    for (std::size_t i = 0; i < output.size(); ++i) {
        output[i] = static_cast<std::uint8_t>((digit(text[i * 2]) << 4) | digit(text[i * 2 + 1]));
    }
    return output;
}

std::string random_hex(std::size_t byte_count) { return bytes_to_hex(random_bytes(byte_count)); }

std::string base64url_encode(const std::vector<std::uint8_t>& bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string output;
    std::uint32_t accumulator = 0;
    int bits = 0;
    for (const auto byte : bytes) {
        accumulator = (accumulator << 8) | byte;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            output.push_back(alphabet[(accumulator >> bits) & 63]);
        }
    }
    if (bits > 0) output.push_back(alphabet[(accumulator << (6 - bits)) & 63]);
    return output;
}

std::vector<std::uint8_t> base64url_decode(const std::string& text) {
    std::array<int, 256> reverse{};
    reverse.fill(-1);
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    for (std::size_t i = 0; i < alphabet.size(); ++i) reverse[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
    std::vector<std::uint8_t> output;
    std::uint32_t accumulator = 0;
    int bits = 0;
    for (const unsigned char c : text) {
        if (c == '=') break;
        const int value = reverse[c];
        if (value < 0) throw std::runtime_error("invalid base64url character");
        accumulator = (accumulator << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<std::uint8_t>((accumulator >> bits) & 0xff));
        }
    }
    return output;
}

std::filesystem::path app_data_directory() {
#ifdef _WIN32
    wchar_t buffer[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    std::filesystem::path root = length > 0 ? std::filesystem::path(buffer) : std::filesystem::current_path();
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    std::filesystem::path root;
    if (xdg && *xdg) root = xdg;
    else if (home && *home) root = std::filesystem::path(home) / ".local" / "share";
    else root = std::filesystem::current_path();
#endif
    auto result = root / "VeilKnit" / "Rooms";
    std::error_code error;
    std::filesystem::create_directories(result, error);
    return result;
}

std::wstring utf8_to_wide(const std::string& text) {
#ifdef _WIN32
    if (text.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (count <= 0) {
        // Preserve displayability for malformed legacy input without narrowing bytes manually.
        code_page = CP_ACP;
        flags = 0;
        count = MultiByteToWideChar(code_page, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
    }
    if (count <= 0) return {};
    std::wstring output(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(code_page, flags, text.data(), static_cast<int>(text.size()), output.data(), count);
    return output;
#else
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.from_bytes(text);
#endif
}

std::string wide_to_utf8(const std::wstring& text) {
#ifdef _WIN32
    if (text.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    UINT code_page = CP_UTF8;
    DWORD flags = WC_ERR_INVALID_CHARS;
    if (count <= 0) {
        // Fall back to the active Windows code page rather than truncating wchar_t values.
        code_page = CP_ACP;
        flags = 0;
        count = WideCharToMultiByte(code_page, flags, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    }
    if (count <= 0) return {};
    std::string output(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(code_page, flags, text.data(), static_cast<int>(text.size()), output.data(), count, nullptr, nullptr);
    return output;
#else
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.to_bytes(text);
#endif
}

std::string short_identity(const std::string& value, std::size_t keep) {
    if (value.size() <= keep * 2 + 3) return value;
    return value.substr(0, keep) + "..." + value.substr(value.size() - keep);
}

std::string format_time(std::uint64_t timestamp) {
    const std::time_t raw = static_cast<std::time_t>(timestamp);
    std::tm time{};
#ifdef _WIN32
    localtime_s(&time, &raw);
#else
    localtime_r(&raw, &time);
#endif
    std::ostringstream stream;
    stream << std::put_time(&time, "%H:%M");
    return stream.str();
}

std::string lowercase_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

} // namespace vkrooms
