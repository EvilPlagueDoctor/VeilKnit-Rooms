#include "veilknit/veilknit.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <thread>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace veilknit {
namespace {

constexpr std::string_view auth_domain = "veilknit/app-auth/v2";
constexpr std::size_t max_line_bytes = 1024 * 1024;

[[noreturn]] void fail(std::string code, std::string message) {
    throw Error(std::move(code), std::move(message));
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) fail("file_open_failed", "could not open " + path.string());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) fail("file_read_failed", "could not read " + path.string());
    return buffer.str();
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) fail("file_open_failed", "could not create " + temporary);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.put('\n');
        if (!output) fail("file_write_failed", "could not write " + temporary);
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
        if (error) fail("file_replace_failed", "could not replace " + path.string() + ": " + error.message());
    }
}

std::string getenv_string(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return {};
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

std::filesystem::path executable_directory() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
#else
    std::array<char, 4096> buffer{};
    const auto length = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length <= 0) return {};
    buffer[static_cast<std::size_t>(length)] = '\0';
    return std::filesystem::path(buffer.data()).parent_path();
#endif
}

std::string safe_app_id(const std::string& app_id) {
    std::string result;
    result.reserve(app_id.size());
    for (const unsigned char c : app_id) {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        result.push_back(safe ? static_cast<char>(c) : '_');
    }
    return result;
}

std::vector<std::filesystem::path> unique_paths(std::vector<std::filesystem::path> paths) {
    std::vector<std::filesystem::path> result;
    for (auto& path : paths) {
        if (path.empty()) continue;
        if (std::find(result.begin(), result.end(), path) == result.end()) result.push_back(std::move(path));
    }
    return result;
}

class Connection {
public:
    explicit Connection(const std::string& endpoint) {
#ifdef _WIN32
        const std::filesystem::path path(endpoint);
        const std::wstring wide = path.wstring();
        for (int attempt = 0; attempt < 40; ++attempt) {
            handle_ = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (handle_ != INVALID_HANDLE_VALUE) break;
            const DWORD error = GetLastError();
            if (error != ERROR_PIPE_BUSY) {
                fail("connect_failed", "could not open daemon named pipe (Win32 error " + std::to_string(error) + ")");
            }
            if (!WaitNamedPipeW(wide.c_str(), 250)) {
                const DWORD wait_error = GetLastError();
                if (wait_error != ERROR_SEM_TIMEOUT) {
                    fail("connect_failed", "could not wait for daemon named pipe (Win32 error " + std::to_string(wait_error) + ")");
                }
            }
        }
        if (handle_ == INVALID_HANDLE_VALUE) fail("connect_timeout", "daemon named pipe remained busy");
#else
        descriptor_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (descriptor_ < 0) fail("socket_failed", std::strerror(errno));
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        if (endpoint.size() >= sizeof(address.sun_path)) {
            close();
            fail("invalid_endpoint", "Unix socket path is too long");
        }
        std::memcpy(address.sun_path, endpoint.c_str(), endpoint.size() + 1);
        if (::connect(descriptor_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            const std::string message = std::strerror(errno);
            close();
            fail("connect_failed", message);
        }
#endif
    }

    Connection(Connection&& other) noexcept { move_from(std::move(other)); }
    Connection& operator=(Connection&& other) noexcept {
        if (this != &other) { close(); move_from(std::move(other)); }
        return *this;
    }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    ~Connection() { close(); }

    void write_line(const std::string& line) {
        if (line.size() + 1 > max_line_bytes) fail("request_too_large", "request exceeds the local API line limit");
        std::string framed = line;
        framed.push_back('\n');
        std::size_t offset = 0;
        while (offset < framed.size()) {
#ifdef _WIN32
            DWORD written = 0;
            const DWORD amount = static_cast<DWORD>(std::min<std::size_t>(framed.size() - offset, 64 * 1024));
            if (!WriteFile(handle_, framed.data() + offset, amount, &written, nullptr)) {
                fail("write_failed", "named-pipe write failed (Win32 error " + std::to_string(GetLastError()) + ")");
            }
            if (written == 0) fail("write_failed", "named-pipe write returned zero bytes");
            offset += written;
#else
            const auto written = ::send(descriptor_, framed.data() + offset, framed.size() - offset, 0);
            if (written < 0) {
                if (errno == EINTR) continue;
                fail("write_failed", std::strerror(errno));
            }
            if (written == 0) fail("write_failed", "socket write returned zero bytes");
            offset += static_cast<std::size_t>(written);
#endif
        }
    }

    std::string read_line() {
        std::string line;
        line.reserve(4096);
        for (;;) {
            char c = '\0';
#ifdef _WIN32
            DWORD read = 0;
            if (!ReadFile(handle_, &c, 1, &read, nullptr)) {
                const DWORD error = GetLastError();
                if (error == ERROR_BROKEN_PIPE && line.empty()) fail("connection_closed", "daemon closed the connection");
                fail("read_failed", "named-pipe read failed (Win32 error " + std::to_string(error) + ")");
            }
            if (read == 0) fail("connection_closed", "daemon closed the connection");
#else
            const auto read = ::recv(descriptor_, &c, 1, 0);
            if (read < 0) {
                if (errno == EINTR) continue;
                fail("read_failed", std::strerror(errno));
            }
            if (read == 0) fail("connection_closed", "daemon closed the connection");
#endif
            if (c == '\n') break;
            if (c != '\r') line.push_back(c);
            if (line.size() > max_line_bytes) fail("response_too_large", "daemon response exceeds the client line limit");
        }
        return line;
    }

    void close() noexcept {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (descriptor_ >= 0) {
            ::close(descriptor_);
            descriptor_ = -1;
        }
#endif
    }

private:
    void move_from(Connection&& other) noexcept {
#ifdef _WIN32
        handle_ = other.handle_;
        other.handle_ = INVALID_HANDLE_VALUE;
#else
        descriptor_ = other.descriptor_;
        other.descriptor_ = -1;
#endif
    }
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int descriptor_ = -1;
#endif
};

json::Value array_of_strings(const std::vector<std::string>& values) {
    json::Value result = json::Value::make_array();
    for (const auto& value : values) result.push_back(value);
    return result;
}

json::Value array_of_u32(const std::vector<std::uint32_t>& values) {
    json::Value result = json::Value::make_array();
    for (const auto value : values) result.push_back(value);
    return result;
}

std::vector<std::string> strings(const json::Value& value) {
    std::vector<std::string> result;
    for (const auto& item : value.as_array()) result.push_back(item.as_string());
    return result;
}

json::Value send_raw(const std::string& endpoint, const json::Value& request) {
    Connection connection(endpoint);
    connection.write_line(request.dump());
    const auto response = json::Value::parse(connection.read_line());
    if (response.at("protocol_version").as_u64() != protocol_version) {
        fail("protocol_mismatch", "daemon and C++ SDK protocol versions differ");
    }
    if (!response.at("ok").as_bool()) {
        const auto& error = response.at("error");
        fail(json::optional_string(error, "code", "daemon_error"),
             json::optional_string(error, "message", "daemon returned an unspecified error"));
    }
    if (!response.contains("result")) fail("invalid_response", "successful response has no result");
    return response.at("result");
}

const json::Value& expect_type(const json::Value& result, std::string_view expected) {
    const auto actual = result.at("type").as_string();
    if (actual != expected) fail("unexpected_response", "expected " + std::string(expected) + ", received " + actual);
    return result;
}

std::uint16_t u16(const json::Value& value, std::string_view field) {
    const auto number = value.at(field).as_u64();
    if (number > std::numeric_limits<std::uint16_t>::max()) fail("invalid_response", "field exceeds uint16: " + std::string(field));
    return static_cast<std::uint16_t>(number);
}

std::uint32_t u32(const json::Value& value, std::string_view field) {
    const auto number = value.at(field).as_u64();
    if (number > std::numeric_limits<std::uint32_t>::max()) fail("invalid_response", "field exceeds uint32: " + std::string(field));
    return static_cast<std::uint32_t>(number);
}

std::size_t usize(const json::Value& value, std::string_view field) {
    const auto number = value.at(field).as_u64();
    if (number > std::numeric_limits<std::size_t>::max()) fail("invalid_response", "field exceeds size_t: " + std::string(field));
    return static_cast<std::size_t>(number);
}

std::optional<std::string> optional_string_value(const json::Value& object, std::string_view field) {
    if (!object.contains(field) || object.at(field).is_null()) return std::nullopt;
    return object.at(field).as_string();
}

StoreDescriptor parse_store(const json::Value& value) {
    return {
        value.at("store_id").as_string(),
        value.at("application_id").as_string(),
        value.at("name").as_string(),
        value.at("record_key").as_string(),
        u16(value, "subkey_count"),
        value.contains("max_value_bytes") ? usize(value, "max_value_bytes") : 0,
        value.at("generation").as_u64(),
        value.at("created_at").as_u64(),
    };
}

StoreValue parse_store_value(const json::Value& value) {
    StoreValue result;
    result.location = u32(value, "location");
    result.is_null = value.at("is_null").as_bool();
    if (value.contains("value_base64") && !value.at("value_base64").is_null()) {
        result.value = base64_decode(value.at("value_base64").as_string());
    }
    result.error = optional_string_value(value, "error");
    return result;
}

std::vector<StoreValue> parse_store_values(const json::Value& value) {
    std::vector<StoreValue> result;
    for (const auto& entry : value.as_array()) result.push_back(parse_store_value(entry));
    return result;
}

AppSigningIdentity parse_signing_identity(const json::Value& value) {
    return {
        value.at("application_id").as_string(),
        value.at("main_dht").as_string(),
        value.at("key_generation").as_u64(),
        value.at("public_key_hex").as_string(),
        value.at("created_at").as_u64(),
        value.at("binding").as_string(),
    };
}

AppSignature parse_signature(const json::Value& value) {
    return {
        value.at("application_id").as_string(),
        value.at("key_generation").as_u64(),
        value.at("public_key_hex").as_string(),
        value.at("domain").as_string(),
        value.at("signature_hex").as_string(),
    };
}

void append_u32_le(Bytes& target, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) target.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
}

void append_u64_le(Bytes& target, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) target.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
}

class Sha256 {
public:
    Sha256() { reset(); }
    void update(const std::uint8_t* data, std::size_t length) {
        total_bytes_ += length;
        while (length > 0) {
            const auto amount = std::min<std::size_t>(length, block_.size() - block_size_);
            std::memcpy(block_.data() + block_size_, data, amount);
            block_size_ += amount;
            data += amount;
            length -= amount;
            if (block_size_ == block_.size()) {
                transform(block_.data());
                block_size_ = 0;
            }
        }
    }
    void update(const Bytes& data) { update(data.data(), data.size()); }
    std::array<std::uint8_t, 32> finish() {
        const std::uint64_t bits = static_cast<std::uint64_t>(total_bytes_) * 8;
        block_[block_size_++] = 0x80;
        if (block_size_ > 56) {
            while (block_size_ < 64) block_[block_size_++] = 0;
            transform(block_.data());
            block_size_ = 0;
        }
        while (block_size_ < 56) block_[block_size_++] = 0;
        for (int shift = 56; shift >= 0; shift -= 8) block_[block_size_++] = static_cast<std::uint8_t>((bits >> shift) & 0xff);
        transform(block_.data());
        std::array<std::uint8_t, 32> output{};
        for (std::size_t index = 0; index < state_.size(); ++index) {
            output[index * 4] = static_cast<std::uint8_t>(state_[index] >> 24);
            output[index * 4 + 1] = static_cast<std::uint8_t>(state_[index] >> 16);
            output[index * 4 + 2] = static_cast<std::uint8_t>(state_[index] >> 8);
            output[index * 4 + 3] = static_cast<std::uint8_t>(state_[index]);
        }
        return output;
    }
private:
    static constexpr std::array<std::uint32_t, 64> constants_ = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    static std::uint32_t rotate(std::uint32_t value, unsigned amount) { return (value >> amount) | (value << (32 - amount)); }
    void reset() {
        state_ = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
        block_size_ = 0;
        total_bytes_ = 0;
    }
    void transform(const std::uint8_t* block) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16; ++i) {
            words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                       (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                       (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                       static_cast<std::uint32_t>(block[i * 4 + 3]);
        }
        for (std::size_t i = 16; i < 64; ++i) {
            const auto s0 = rotate(words[i - 15], 7) ^ rotate(words[i - 15], 18) ^ (words[i - 15] >> 3);
            const auto s1 = rotate(words[i - 2], 17) ^ rotate(words[i - 2], 19) ^ (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }
        auto a=state_[0], b=state_[1], c=state_[2], d=state_[3], e=state_[4], f=state_[5], g=state_[6], h=state_[7];
        for (std::size_t i = 0; i < 64; ++i) {
            const auto s1 = rotate(e,6) ^ rotate(e,11) ^ rotate(e,25);
            const auto choice = (e & f) ^ ((~e) & g);
            const auto temp1 = h + s1 + choice + constants_[i] + words[i];
            const auto s0 = rotate(a,2) ^ rotate(a,13) ^ rotate(a,22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temp2 = s0 + majority;
            h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
        }
        state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
        state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
    }
    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> block_{};
    std::size_t block_size_ = 0;
    std::size_t total_bytes_ = 0;
};

std::array<std::uint8_t, 32> hmac_sha256(const Bytes& key, const Bytes& message) {
    std::array<std::uint8_t, 64> normalized{};
    if (key.size() > normalized.size()) {
        Sha256 hash;
        hash.update(key);
        const auto digest = hash.finish();
        std::copy(digest.begin(), digest.end(), normalized.begin());
    } else {
        std::copy(key.begin(), key.end(), normalized.begin());
    }
    Bytes inner_pad(64), outer_pad(64);
    for (std::size_t i = 0; i < 64; ++i) {
        inner_pad[i] = normalized[i] ^ 0x36;
        outer_pad[i] = normalized[i] ^ 0x5c;
    }
    Sha256 inner;
    inner.update(inner_pad);
    inner.update(message);
    const auto inner_digest = inner.finish();
    Sha256 outer;
    outer.update(outer_pad);
    outer.update(inner_digest.data(), inner_digest.size());
    return outer.finish();
}

Bytes compute_auth_proof(
    const Credential& credential,
    std::uint64_t challenge_id,
    const Bytes& nonce,
    std::uint64_t issued_at,
    std::uint64_t expires_at,
    std::uint64_t generation,
    const std::vector<std::string>& capabilities) {
    if (nonce.size() != 32) fail("invalid_challenge", "authentication nonce must be 32 bytes");
    Bytes input(auth_domain.begin(), auth_domain.end());
    append_u32_le(input, static_cast<std::uint32_t>(credential.app_id.size()));
    input.insert(input.end(), credential.app_id.begin(), credential.app_id.end());
    append_u64_le(input, challenge_id);
    input.insert(input.end(), nonce.begin(), nonce.end());
    append_u64_le(input, issued_at);
    append_u64_le(input, expires_at);
    append_u64_le(input, generation);
    append_u32_le(input, static_cast<std::uint32_t>(capabilities.size()));
    for (const auto& capability : capabilities) {
        input.insert(input.end(), capability.begin(), capability.end());
        input.push_back(0);
    }
    const auto digest = hmac_sha256(hex_decode(credential.secret_hex), input);
    return Bytes(digest.begin(), digest.end());
}


Bytes secure_random(std::size_t size) {
    Bytes output(size);
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, output.data(), static_cast<ULONG>(output.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        fail("random_failed", "BCryptGenRandom failed");
    }
#else
    std::ifstream input("/dev/urandom", std::ios::binary);
    if (!input) fail("random_failed", "could not open /dev/urandom");
    input.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(output.size()));
    if (!input) fail("random_failed", "could not read /dev/urandom");
#endif
    return output;
}

json::Value make_request(std::uint64_t request_id, std::string action) {
    json::Value request = json::Value::make_object();
    request["protocol_version"] = protocol_version;
    request["request_id"] = request_id;
    request["action"] = std::move(action);
    return request;
}

IncomingMessage parse_incoming(const json::Value& event) {
    IncomingMessage message;
    message.application_id = event.at("application_id").as_string();
    message.message_id_hex = event.at("message_id_hex").as_string();
    message.sender_main_dht = event.at("sender_main_dht").as_string();
    message.recipient_main_dht = event.at("recipient_main_dht").as_string();
    message.posted_at = event.at("posted_at").as_u64();
    message.expires_at = event.at("expires_at").as_u64();
    message.conversation_id_hex = optional_string_value(event, "conversation_id_hex");
    message.payload = base64_decode(event.at("payload_base64").as_string());
    return message;
}

} // namespace

Error::Error(std::string code, std::string message)
    : std::runtime_error(message), code_(std::move(code)) {}
const std::string& Error::code() const noexcept { return code_; }

Credential Credential::load(const std::filesystem::path& path) {
    const auto value = json::Value::parse(read_text(path));
    Credential credential;
    credential.protocol_version = u16(value, "protocol_version");
    credential.endpoint = value.at("endpoint").as_string();
    credential.app_id = value.at("app_id").as_string();
    credential.display_name = value.at("display_name").as_string();
    credential.secret_hex = value.at("secret_hex").as_string();
    credential.credential_generation = value.at("credential_generation").as_u64();
    // Protocol 1 and 2 credential files contain the same app id, secret, and
    // generation fields. The wire envelope/authentication domain changed, but
    // the approved secret itself remains valid, so migrate the local file in
    // memory rather than forcing the user to re-authorize after an upgrade.
    if (credential.protocol_version == 0 || credential.protocol_version > veilknit::protocol_version) {
        fail("credential_protocol_mismatch", "credential uses unsupported protocol " +
             std::to_string(credential.protocol_version) + ", SDK uses protocol " +
             std::to_string(veilknit::protocol_version));
    }
    credential.protocol_version = veilknit::protocol_version;
    if (hex_decode(credential.secret_hex).size() != 32) fail("invalid_credential", "credential secret must be 32 bytes");
    return credential;
}

void Credential::save(const std::filesystem::path& path) const {
    json::Value value = json::Value::make_object();
    value["protocol_version"] = protocol_version;
    value["endpoint"] = endpoint;
    value["app_id"] = app_id;
    value["display_name"] = display_name;
    value["secret_hex"] = secret_hex;
    value["credential_generation"] = credential_generation;
    write_text(path, value.dump());
}

std::vector<std::string> default_capabilities() {
    return {
        "SendMessages", "ReceiveMessages", "ManageOwnStorage", "ReadOwnStorage",
        "ReadPublicProfiles", "SubscribeNetworkStatus", "SubmitReputation",
        "RequestAppScopedRestriction", "InspectOwnReputationSubmissions", "SignAppData"
    };
}

std::string to_wire(RestrictionAction action) {
    return action == RestrictionAction::ban ? "ban" : "restrict";
}

std::string to_wire(ObservationKind kind) {
    static constexpr std::array<const char*, 27> values = {
        "InteractionSucceeded","InteractionFailed","UsefulService","ExcessiveActivity",
        "RepetitiveActivity","SuspiciousCoordination","MessageDelivered","MessageRejected",
        "UnsolicitedMessage","Spam","Harassment","ValidDhtResponse","InvalidDhtResponse",
        "InvalidSignature","ImpossibleProtocolState","MalformedProtocolMessage",
        "DeliberateStateCorruption","FutureTimestampClaim","ConflictingAccountCreationClaim",
        "SuspiciousCreationBurst","Reachable","Unreachable","StableAvailability",
        "AppBanRequested","UserMarkedHarmful","UserMarkedTrusted","HandshakeUnavailable"
    };
    return values.at(static_cast<std::size_t>(kind));
}

struct MessageSubscription::Impl {
    explicit Impl(Connection connection) : connection(std::move(connection)) {}
    Connection connection;
    bool closed = false;
};

MessageSubscription::MessageSubscription(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
MessageSubscription::MessageSubscription(MessageSubscription&&) noexcept = default;
MessageSubscription& MessageSubscription::operator=(MessageSubscription&&) noexcept = default;
MessageSubscription::~MessageSubscription() = default;
IncomingMessage MessageSubscription::next() {
    if (!impl_ || impl_->closed) fail("subscription_closed", "message subscription is closed");
    const auto envelope = json::Value::parse(impl_->connection.read_line());
    if (envelope.at("protocol_version").as_u64() != protocol_version) fail("protocol_mismatch", "subscription protocol mismatch");
    if (envelope.at("stream").as_string() != "application_messages") fail("unexpected_stream", "unexpected subscription stream");
    return parse_incoming(envelope.at("event"));
}
void MessageSubscription::close() {
    if (impl_) { impl_->connection.close(); impl_->closed = true; }
}

std::string Client::discover_endpoint() {
    const auto environment = getenv_string("DAEMON_NETWORK_ENDPOINT");
    if (!environment.empty()) return environment;
    std::vector<std::filesystem::path> paths = {
        std::filesystem::path("app_credentials") / "daemon_endpoint.json",
        executable_directory() / "app_credentials" / "daemon_endpoint.json"
    };
    const auto local = getenv_string("LOCALAPPDATA");
    if (!local.empty()) paths.push_back(std::filesystem::path(local) / "DaemonNetwork" / "daemon_endpoint.json");
    const auto home = getenv_string("HOME");
    if (!home.empty()) paths.push_back(std::filesystem::path(home) / ".daemon_network" / "daemon_endpoint.json");
    for (const auto& path : unique_paths(std::move(paths))) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) continue;
        try {
            const auto value = json::Value::parse(read_text(path));
            if (value.at("protocol_version").as_u64() == protocol_version) return value.at("endpoint").as_string();
        } catch (...) {}
    }
    fail("endpoint_not_found", "could not find the VeilKnit daemon endpoint");
}

std::filesystem::path Client::discover_credential_path(const std::string& app_id) {
    const auto filename = safe_app_id(app_id) + ".json";
    std::vector<std::filesystem::path> paths;
    const auto explicit_file = getenv_string("DAEMON_NETWORK_CREDENTIAL");
    if (!explicit_file.empty()) paths.emplace_back(explicit_file);
    const auto explicit_directory = getenv_string("DAEMON_NETWORK_CREDENTIAL_DIR");
    if (!explicit_directory.empty()) paths.push_back(std::filesystem::path(explicit_directory) / filename);
    paths.push_back(std::filesystem::path("app_credentials") / filename);
    paths.push_back(executable_directory() / "app_credentials" / filename);
    const auto local = getenv_string("LOCALAPPDATA");
    if (!local.empty()) paths.push_back(std::filesystem::path(local) / "DaemonNetwork" / "credentials" / filename);
    const auto home = getenv_string("HOME");
    if (!home.empty()) paths.push_back(std::filesystem::path(home) / ".daemon_network" / "credentials" / filename);
    for (const auto& path : unique_paths(std::move(paths))) {
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error)) return path;
    }
    fail("credential_not_found", "no approved credential found for application " + app_id);
}

Credential Client::load_discovered_credential(const std::string& app_id) {
    return Credential::load(discover_credential_path(app_id));
}

ApiInfo Client::api_info(const std::string& endpoint) {
    const auto result = expect_type(send_raw(endpoint, make_request(1, "get_api_info")), "api_info");
    ApiInfo info;
    info.protocol_version = u16(result, "protocol_version");
    info.authentication_proof = result.at("authentication_proof").as_string();
    info.features = strings(result.at("features"));
    info.max_message_bytes = usize(result, "max_message_bytes");
    info.max_store_value_bytes = usize(result, "max_store_value_bytes");
    info.max_store_subkeys = u16(result, "max_store_subkeys");
    info.max_stores_per_app = usize(result, "max_stores_per_app");
    info.max_store_reads_per_request = usize(result, "max_store_reads_per_request");
    info.max_store_writes_per_request = usize(result, "max_store_writes_per_request");
    info.max_store_write_bytes_per_request = usize(result, "max_store_write_bytes_per_request");
    info.max_signature_payload_bytes = usize(result, "max_signature_payload_bytes");
    info.max_signature_domain_bytes = usize(result, "max_signature_domain_bytes");
    return info;
}

void Client::ping(const std::string& endpoint) {
    expect_type(send_raw(endpoint, make_request(1, "ping")), "pong");
}

Credential Client::register_app(
    const std::string& endpoint,
    const std::string& app_id,
    const std::string& display_name,
    std::vector<std::string> capabilities,
    std::uint64_t timeout_seconds) {
    if (app_id.empty() || app_id.size() > 128) fail("invalid_app_id", "application id must contain 1 to 128 bytes");
    for (const unsigned char c : app_id) {
        const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (!valid) fail("invalid_app_id", "application id contains an unsupported character");
    }
    const auto token = secure_random(32);
    auto request = make_request(1, "request_app_registration");
    request["app_id"] = app_id;
    request["display_name"] = display_name;
    request["requested_capabilities"] = array_of_strings(capabilities);
    request["request_token_hex"] = hex_encode(token);
    const auto pending = expect_type(send_raw(endpoint, request), "app_registration_pending");
    const auto request_id = pending.at("request_id").as_u64();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    for (;;) {
        auto status = make_request(1, "get_app_registration_status");
        status["registration_request_id"] = request_id;
        status["request_token_hex"] = hex_encode(token);
        const auto result = send_raw(endpoint, status);
        const auto type = result.at("type").as_string();
        if (type == "app_registration_approved") {
            Credential credential;
            credential.protocol_version = u16(result, "protocol_version");
            credential.endpoint = result.at("endpoint").as_string();
            credential.app_id = result.at("app_id").as_string();
            credential.display_name = result.at("display_name").as_string();
            credential.secret_hex = result.at("secret_hex").as_string();
            credential.credential_generation = result.at("credential_generation").as_u64();
            return credential;
        }
        if (type == "app_registration_rejected") {
            fail("authorization_rejected", result.at("reason").as_string());
        }
        if (type == "app_registration_expired") {
            fail("authorization_expired", "application authorization request expired");
        }
        if (type != "app_registration_still_pending") {
            fail("unexpected_response", "unexpected registration result: " + type);
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            fail("authorization_timeout", "application authorization was not approved before the timeout");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(750));
    }
}

Client Client::authenticate(const Credential& credential, std::vector<std::string> capabilities) {
    if (credential.protocol_version != protocol_version) fail("credential_protocol_mismatch", "credential protocol is incompatible");
    auto begin = make_request(1, "begin_authentication");
    begin["app_id"] = credential.app_id;
    begin["requested_capabilities"] = array_of_strings(capabilities);
    const auto challenge = expect_type(send_raw(credential.endpoint, begin), "authentication_challenge");
    if (challenge.at("app_id").as_string() != credential.app_id) fail("invalid_challenge", "challenge application id differs");
    const auto challenge_capabilities = strings(challenge.at("requested_capabilities"));
    const auto generation = challenge.at("credential_generation").as_u64();
    if (generation != credential.credential_generation) fail("credential_generation_mismatch", "credential generation differs from daemon");
    const auto challenge_id = challenge.at("challenge_id").as_u64();
    const auto proof = compute_auth_proof(
        credential, challenge_id, hex_decode(challenge.at("nonce_hex").as_string()),
        challenge.at("issued_at").as_u64(), challenge.at("expires_at").as_u64(), generation,
        challenge_capabilities);
    auto finish = make_request(2, "finish_authentication");
    finish["app_id"] = credential.app_id;
    finish["challenge_id"] = challenge_id;
    finish["proof_hex"] = hex_encode(proof);
    const auto result = expect_type(send_raw(credential.endpoint, finish), "authentication_succeeded");
    Session session;
    session.app_id = result.at("app_id").as_string();
    session.session_id = result.at("session_id").as_string();
    session.token_hex = result.at("session_token_hex").as_string();
    session.authenticated_at = result.at("authenticated_at").as_u64();
    session.expires_at = result.at("expires_at").as_u64();
    session.capabilities = strings(result.at("capabilities"));
    return Client(credential.endpoint, std::move(session));
}

Client::Client(std::string endpoint, Session session)
    : endpoint_(std::move(endpoint)), session_(std::move(session)),
      next_request_id_(std::make_shared<std::atomic<std::uint64_t>>(3)) {}
const Session& Client::session() const noexcept { return session_; }
const std::string& Client::endpoint() const noexcept { return endpoint_; }

json::Value Client::raw_request(json::Value request) const {
    request["protocol_version"] = protocol_version;
    request["request_id"] = next_request_id_->fetch_add(1, std::memory_order_relaxed);
    if (!request.contains("session_token")) request["session_token"] = session_.token_hex;
    return send_raw(endpoint_, request);
}

LocalIdentity Client::identity() const {
    auto request = json::Value::make_object(); request["action"] = "get_identity";
    const auto result = expect_type(raw_request(std::move(request)), "identity");
    return {result.at("username").as_string(), result.at("main_dht").as_string()};
}

json::Value Client::network_status() const {
    auto request = json::Value::make_object(); request["action"] = "get_status";
    const auto result = expect_type(raw_request(std::move(request)), "status");
    return result.at("status");
}

std::string Client::send_message(const std::string& recipient, const Bytes& payload, const MessageOptions& options) const {
    auto request = json::Value::make_object();
    request["action"] = "send_message";
    request["recipient_main_dht"] = recipient;
    request["payload_base64"] = base64_encode(payload);
    request["conversation_id_hex"] = options.conversation_id_hex ? json::Value(*options.conversation_id_hex) : json::Value(nullptr);
    request["expires_at"] = options.expires_at ? json::Value(*options.expires_at) : json::Value(nullptr);
    request["await_response"] = options.await_response;
    const auto result = expect_type(raw_request(std::move(request)), "message_queued");
    return result.at("message_id_hex").as_string();
}

void Client::trigger_message_retrieval() const {
    auto request = json::Value::make_object(); request["action"] = "trigger_message_retrieval";
    expect_type(raw_request(std::move(request)), "message_retrieval_scheduled");
}

MailboxStatus Client::mailbox_status() const {
    auto request = json::Value::make_object(); request["action"] = "get_mailbox_status";
    const auto result = expect_type(raw_request(std::move(request)), "mailbox_status");
    const auto optional_u16 = [&](std::string_view field) -> std::uint16_t {
        return result.contains(field) && !result.at(field).is_null() ? u16(result, field) : 0;
    };
    const auto optional_u32 = [&](std::string_view field) -> std::uint32_t {
        return result.contains(field) && !result.at(field).is_null() ? u32(result, field) : 0;
    };
    const auto optional_usize = [&](std::string_view field) -> std::size_t {
        return result.contains(field) && !result.at(field).is_null() ? usize(result, field) : 0;
    };
    return {
        optional_u16("storage_layout_version"),
        optional_u32("mailbox_index_subkeys"),
        optional_u32("mail_send_subkeys"),
        optional_u32("mail_response_subkeys"),
        optional_usize("index_value_capacity"),
        optional_usize("payload_value_capacity"),
        optional_string_value(result, "mailbox_dht"),
        optional_string_value(result, "mail_send_dht"),
        result.contains("mail_response_dht") && !result.at("mail_response_dht").is_null()
            ? result.at("mail_response_dht").as_string() : std::string{},
        result.contains("receive_key_epoch") ? result.at("receive_key_epoch").as_u64() : 0,
        optional_usize("pending_page_sets"),
        optional_usize("outgoing_message_count"),
        optional_usize("awaiting_response_count"),
        optional_usize("stored_inbox_count"),
        optional_usize("unread_inbox_count"),
        optional_usize("known_custodian_count")
    };
}

MessageSubscription Client::subscribe_messages() const {
    auto request = make_request(next_request_id_->fetch_add(1, std::memory_order_relaxed), "subscribe_messages");
    request["session_token"] = session_.token_hex;
    Connection connection(endpoint_);
    connection.write_line(request.dump());
    const auto response = json::Value::parse(connection.read_line());
    if (!response.at("ok").as_bool()) {
        const auto& error = response.at("error");
        fail(json::optional_string(error, "code", "subscription_failed"), json::optional_string(error, "message", "subscription failed"));
    }
    expect_type(response.at("result"), "application_message_subscription_started");
    return MessageSubscription(std::make_unique<MessageSubscription::Impl>(std::move(connection)));
}

AppSigningIdentity Client::signing_identity() const {
    auto request = json::Value::make_object(); request["action"] = "get_app_signing_identity";
    const auto result = expect_type(raw_request(std::move(request)), "app_signing_identity");
    return parse_signing_identity(result.at("identity"));
}
AppSigningIdentity Client::rotate_signing_key() const {
    auto request = json::Value::make_object(); request["action"] = "rotate_app_signing_key";
    const auto result = expect_type(raw_request(std::move(request)), "app_signing_identity");
    return parse_signing_identity(result.at("identity"));
}
AppSignature Client::sign(const std::string& domain, const Bytes& payload) const {
    auto request = json::Value::make_object(); request["action"] = "sign_app_payload";
    request["domain"] = domain; request["payload_base64"] = base64_encode(payload);
    const auto result = expect_type(raw_request(std::move(request)), "app_payload_signed");
    return parse_signature(result.at("signature"));
}
bool Client::verify(const std::string& public_key, const std::string& domain, const Bytes& payload, const std::string& signature) const {
    auto request = json::Value::make_object(); request["action"] = "verify_app_signature";
    request["public_key_hex"] = public_key; request["domain"] = domain;
    request["payload_base64"] = base64_encode(payload); request["signature_hex"] = signature;
    const auto result = expect_type(raw_request(std::move(request)), "app_signature_verified");
    return result.at("valid").as_bool();
}

std::vector<StoreDescriptor> Client::list_stores() const {
    auto request = json::Value::make_object(); request["action"] = "list_app_stores";
    const auto result = expect_type(raw_request(std::move(request)), "app_stores");
    std::vector<StoreDescriptor> stores;
    for (const auto& value : result.at("stores").as_array()) stores.push_back(parse_store(value));
    return stores;
}
StoreDescriptor Client::create_store(const std::string& name, std::uint16_t count, bool initialize) const {
    auto request = json::Value::make_object(); request["action"] = "create_app_store";
    request["name"] = name; request["subkey_count"] = count; request["initialize"] = initialize;
    const auto result = expect_type(raw_request(std::move(request)), "app_store_created");
    return parse_store(result.at("store"));
}
StoreRead Client::read_store(const std::string& id, const std::vector<std::uint32_t>& locations, bool refresh) const {
    auto request = json::Value::make_object(); request["action"] = "read_app_store";
    request["store_id"] = id; request["locations"] = array_of_u32(locations); request["force_refresh"] = refresh;
    const auto result = expect_type(raw_request(std::move(request)), "app_store_read");
    return {parse_store(result.at("store")), parse_store_values(result.at("values"))};
}
StoreDescriptor Client::write_store(const std::string& id, const std::vector<StoreWrite>& writes, std::optional<std::uint64_t> expected) const {
    auto request = json::Value::make_object(); request["action"] = "write_app_store";
    request["store_id"] = id;
    request["expected_generation"] = expected ? json::Value(*expected) : json::Value(nullptr);
    json::Value entries = json::Value::make_array();
    for (const auto& write : writes) {
        json::Value entry = json::Value::make_object();
        entry["location"] = write.location; entry["value_base64"] = base64_encode(write.value);
        entries.push_back(std::move(entry));
    }
    request["writes"] = std::move(entries);
    const auto result = expect_type(raw_request(std::move(request)), "app_store_written");
    return parse_store(result.at("store"));
}
PublicStoreRead Client::read_public_store(const std::string& record, const std::vector<std::uint32_t>& locations, bool refresh) const {
    auto request = json::Value::make_object(); request["action"] = "read_public_store";
    request["record_key"] = record; request["locations"] = array_of_u32(locations); request["force_refresh"] = refresh;
    const auto result = expect_type(raw_request(std::move(request)), "public_store_read");
    return {result.at("record_key").as_string(), parse_store_values(result.at("values"))};
}

std::uint64_t Client::submit_reputation_observation(const std::string& subject, ObservationKind kind, std::optional<std::uint32_t> code, std::optional<std::string> description) const {
    auto request = json::Value::make_object(); request["action"] = "submit_reputation_observation";
    request["subject_main_dht"] = subject; request["kind"] = to_wire(kind);
    request["application_code"] = code ? json::Value(*code) : json::Value(nullptr);
    request["description"] = description ? json::Value(*description) : json::Value(nullptr);
    const auto result = expect_type(raw_request(std::move(request)), "reputation_observation_submitted");
    return result.at("observation_id").as_u64();
}
void Client::retract_reputation_observation(const std::string& subject, std::uint64_t id) const {
    auto request = json::Value::make_object(); request["action"] = "retract_reputation_observation";
    request["subject_main_dht"] = subject; request["observation_id"] = id;
    expect_type(raw_request(std::move(request)), "reputation_observation_retracted");
}
std::uint64_t Client::request_app_restriction(const std::string& subject, RestrictionAction action, const std::string& reason, std::optional<std::uint64_t> expires) const {
    auto request = json::Value::make_object(); request["action"] = "request_app_restriction";
    request["subject_main_dht"] = subject; request["restriction_action"] = to_wire(action); request["reason"] = reason;
    request["expires_at"] = expires ? json::Value(*expires) : json::Value(nullptr);
    const auto result = expect_type(raw_request(std::move(request)), "app_restriction_requested");
    return result.at("decision_id").as_u64();
}
void Client::revoke_app_decision(const std::string& subject, std::uint64_t id) const {
    auto request = json::Value::make_object(); request["action"] = "revoke_app_decision";
    request["subject_main_dht"] = subject; request["decision_id"] = id;
    expect_type(raw_request(std::move(request)), "app_decision_revoked");
}
json::Value Client::reputation_view(const std::string& subject) const {
    auto request = json::Value::make_object(); request["action"] = "get_reputation_view"; request["subject_main_dht"] = subject;
    const auto result = expect_type(raw_request(std::move(request)), "reputation_view");
    return result.at("view");
}
json::Value Client::own_reputation_submissions() const {
    auto request = json::Value::make_object(); request["action"] = "get_own_reputation_submissions";
    const auto result = expect_type(raw_request(std::move(request)), "own_reputation_submissions");
    return result.at("report");
}

std::string hex_encode(const Bytes& bytes) {
    static constexpr char alphabet[] = "0123456789abcdef";
    std::string output(bytes.size() * 2, '0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        output[i * 2] = alphabet[bytes[i] >> 4];
        output[i * 2 + 1] = alphabet[bytes[i] & 0x0f];
    }
    return output;
}
Bytes hex_decode(const std::string& value) {
    if (value.size() % 2 != 0) fail("invalid_hex", "hexadecimal value has odd length");
    auto digit = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(10 + c - 'a');
        if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(10 + c - 'A');
        fail("invalid_hex", "invalid hexadecimal character");
    };
    Bytes output(value.size() / 2);
    for (std::size_t i = 0; i < output.size(); ++i) output[i] = static_cast<std::uint8_t>((digit(value[i * 2]) << 4) | digit(value[i * 2 + 1]));
    return output;
}

std::string base64_encode(const Bytes& bytes) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t a = bytes[i];
        const std::uint32_t b = i + 1 < bytes.size() ? bytes[i + 1] : 0;
        const std::uint32_t c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        const std::uint32_t value = (a << 16) | (b << 8) | c;
        output.push_back(alphabet[(value >> 18) & 63]);
        output.push_back(alphabet[(value >> 12) & 63]);
        output.push_back(i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=');
        output.push_back(i + 2 < bytes.size() ? alphabet[value & 63] : '=');
    }
    return output;
}
Bytes base64_decode(const std::string& value) {
    std::array<int, 256> table{};
    table.fill(-1);
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (std::size_t i = 0; i < alphabet.size(); ++i) table[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
    if (value.size() % 4 != 0) fail("invalid_base64", "base64 length is not a multiple of four");
    Bytes output;
    output.reserve((value.size() / 4) * 3);
    for (std::size_t i = 0; i < value.size(); i += 4) {
        const bool pad2 = value[i + 2] == '=';
        const bool pad3 = value[i + 3] == '=';
        if (pad2 && !pad3) fail("invalid_base64", "invalid base64 padding");
        const int a = table[static_cast<unsigned char>(value[i])];
        const int b = table[static_cast<unsigned char>(value[i + 1])];
        const int c = pad2 ? 0 : table[static_cast<unsigned char>(value[i + 2])];
        const int d = pad3 ? 0 : table[static_cast<unsigned char>(value[i + 3])];
        if (a < 0 || b < 0 || c < 0 || d < 0) fail("invalid_base64", "invalid base64 character");
        const std::uint32_t combined = (static_cast<std::uint32_t>(a) << 18) |
                                       (static_cast<std::uint32_t>(b) << 12) |
                                       (static_cast<std::uint32_t>(c) << 6) |
                                       static_cast<std::uint32_t>(d);
        output.push_back(static_cast<std::uint8_t>(combined >> 16));
        if (!pad2) output.push_back(static_cast<std::uint8_t>(combined >> 8));
        if (!pad3) output.push_back(static_cast<std::uint8_t>(combined));
        if ((pad2 || pad3) && i + 4 != value.size()) fail("invalid_base64", "padding is only valid in the final base64 block");
    }
    return output;
}

} // namespace veilknit
