#include "crypto.hpp"
#include "util.hpp"

#include <array>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif

namespace vkrooms {
namespace {

constexpr std::array<std::uint32_t, 64> k = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

std::uint32_t rotr(std::uint32_t value, int bits) { return (value >> bits) | (value << (32 - bits)); }

void transform(std::array<std::uint32_t, 8>& state, const std::uint8_t* block) {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    auto a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
    for (int i = 0; i < 64; ++i) {
        const auto s1 = rotr(e,6)^rotr(e,11)^rotr(e,25);
        const auto ch = (e&f)^((~e)&g);
        const auto temp1 = h+s1+ch+k[i]+w[i];
        const auto s0 = rotr(a,2)^rotr(a,13)^rotr(a,22);
        const auto maj = (a&b)^(a&c)^(b&c);
        const auto temp2 = s0+maj;
        h=g;g=f;f=e;e=d+temp1;d=c;c=b;b=a;a=temp1+temp2;
    }
    state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
}

#ifdef _WIN32
void check(NTSTATUS status, const char* message) {
    if (status < 0) throw std::runtime_error(message);
}
#endif

} // namespace

std::vector<std::uint8_t> sha256(const std::vector<std::uint8_t>& input) {
    std::array<std::uint32_t, 8> state = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::vector<std::uint8_t> data = input;
    const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while ((data.size() % 64) != 56) data.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8) data.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xff));
    for (std::size_t offset = 0; offset < data.size(); offset += 64) transform(state, data.data() + offset);
    std::vector<std::uint8_t> output(32);
    for (std::size_t i = 0; i < state.size(); ++i) {
        output[i*4] = static_cast<std::uint8_t>(state[i] >> 24);
        output[i*4+1] = static_cast<std::uint8_t>(state[i] >> 16);
        output[i*4+2] = static_cast<std::uint8_t>(state[i] >> 8);
        output[i*4+3] = static_cast<std::uint8_t>(state[i]);
    }
    return output;
}

std::vector<std::uint8_t> derive_room_key(const std::string& secret, const std::string& room_id, std::uint64_t epoch) {
    auto seed = hex_to_bytes(secret);
    const std::string context = room_id + ":" + std::to_string(epoch) + ":veilknit.rooms.key.v1";
    seed.insert(seed.end(), context.begin(), context.end());
    return sha256(seed);
}

CipherText encrypt_room_payload(const std::vector<std::uint8_t>& plaintext,
                                const std::vector<std::uint8_t>& key,
                                const std::string& associated_data) {
#ifdef _WIN32
    if (key.size() != 32) throw std::runtime_error("room key must be 32 bytes");
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key_handle = nullptr;
    check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0), "BCryptOpenAlgorithmProvider failed");
    struct Cleanup { BCRYPT_ALG_HANDLE a; BCRYPT_KEY_HANDLE* k; ~Cleanup(){ if (*k) BCryptDestroyKey(*k); if (a) BCryptCloseAlgorithmProvider(a,0);} } cleanup{algorithm,&key_handle};
    check(BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
                            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                            static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_GCM)), 0), "BCryptSetProperty GCM failed");
    DWORD object_length = 0, result_length = 0;
    check(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_length), sizeof(object_length), &result_length, 0), "BCryptGetProperty failed");
    std::vector<std::uint8_t> key_object(object_length);
    check(BCryptGenerateSymmetricKey(algorithm, &key_handle, key_object.data(), static_cast<ULONG>(key_object.size()),
                                     const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0), "BCryptGenerateSymmetricKey failed");
    auto nonce = random_bytes(12);
    std::vector<std::uint8_t> tag(16);
    std::vector<std::uint8_t> cipher(plaintext.size());
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = nonce.data(); info.cbNonce = static_cast<ULONG>(nonce.size());
    info.pbTag = tag.data(); info.cbTag = static_cast<ULONG>(tag.size());
    info.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<char*>(associated_data.data()));
    info.cbAuthData = static_cast<ULONG>(associated_data.size());
    ULONG produced = 0;
    check(BCryptEncrypt(key_handle, const_cast<PUCHAR>(plaintext.data()), static_cast<ULONG>(plaintext.size()), &info,
                        nullptr, 0, cipher.data(), static_cast<ULONG>(cipher.size()), &produced, 0), "BCryptEncrypt failed");
    cipher.resize(produced);
    return {bytes_to_hex(nonce), base64url_encode(cipher), bytes_to_hex(tag)};
#else
    if (key.size() != 32) throw std::runtime_error("room key must be 32 bytes");
    auto nonce = random_bytes(12);
    std::vector<std::uint8_t> cipher(plaintext.size() + 16);
    std::vector<std::uint8_t> tag(16);
    EVP_CIPHER_CTX* raw = EVP_CIPHER_CTX_new();
    if (!raw) throw std::runtime_error("EVP_CIPHER_CTX_new failed");
    struct Cleanup { EVP_CIPHER_CTX* value; ~Cleanup(){ EVP_CIPHER_CTX_free(value); } } cleanup{raw};
    int written = 0;
    int total = 0;
    if (EVP_EncryptInit_ex(raw, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(raw, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1 ||
        EVP_EncryptInit_ex(raw, nullptr, nullptr, key.data(), nonce.data()) != 1) {
        throw std::runtime_error("OpenSSL AES-GCM initialization failed");
    }
    if (!associated_data.empty() && EVP_EncryptUpdate(raw, nullptr, &written,
            reinterpret_cast<const unsigned char*>(associated_data.data()), static_cast<int>(associated_data.size())) != 1) {
        throw std::runtime_error("OpenSSL AES-GCM associated-data update failed");
    }
    if (!plaintext.empty() && EVP_EncryptUpdate(raw, cipher.data(), &written, plaintext.data(), static_cast<int>(plaintext.size())) != 1) {
        throw std::runtime_error("OpenSSL AES-GCM encryption failed");
    }
    total = written;
    if (EVP_EncryptFinal_ex(raw, cipher.data() + total, &written) != 1) {
        throw std::runtime_error("OpenSSL AES-GCM finalization failed");
    }
    total += written;
    cipher.resize(static_cast<std::size_t>(total));
    if (EVP_CIPHER_CTX_ctrl(raw, EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size()), tag.data()) != 1) {
        throw std::runtime_error("OpenSSL AES-GCM tag extraction failed");
    }
    return {bytes_to_hex(nonce), base64url_encode(cipher), bytes_to_hex(tag)};
#endif
}

std::vector<std::uint8_t> decrypt_room_payload(const CipherText& value,
                                               const std::vector<std::uint8_t>& key,
                                               const std::string& associated_data) {
    const auto nonce = hex_to_bytes(value.nonce_hex);
    const auto cipher = base64url_decode(value.cipher_base64);
    const auto tag = hex_to_bytes(value.tag_hex);
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key_handle = nullptr;
    check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0), "BCryptOpenAlgorithmProvider failed");
    struct Cleanup { BCRYPT_ALG_HANDLE a; BCRYPT_KEY_HANDLE* k; ~Cleanup(){ if (*k) BCryptDestroyKey(*k); if (a) BCryptCloseAlgorithmProvider(a,0);} } cleanup{algorithm,&key_handle};
    check(BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
                            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                            static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_GCM)), 0), "BCryptSetProperty GCM failed");
    DWORD object_length = 0, result_length = 0;
    check(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_length), sizeof(object_length), &result_length, 0), "BCryptGetProperty failed");
    std::vector<std::uint8_t> key_object(object_length);
    check(BCryptGenerateSymmetricKey(algorithm, &key_handle, key_object.data(), static_cast<ULONG>(key_object.size()),
                                     const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0), "BCryptGenerateSymmetricKey failed");
    std::vector<std::uint8_t> plaintext(cipher.size());
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(nonce.data()); info.cbNonce = static_cast<ULONG>(nonce.size());
    info.pbTag = const_cast<PUCHAR>(tag.data()); info.cbTag = static_cast<ULONG>(tag.size());
    info.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<char*>(associated_data.data()));
    info.cbAuthData = static_cast<ULONG>(associated_data.size());
    ULONG produced = 0;
    check(BCryptDecrypt(key_handle, const_cast<PUCHAR>(cipher.data()), static_cast<ULONG>(cipher.size()), &info,
                        nullptr, 0, plaintext.data(), static_cast<ULONG>(plaintext.size()), &produced, 0), "room payload authentication failed");
    plaintext.resize(produced);
    return plaintext;
#else
    if (key.size() != 32 || nonce.size() != 12 || tag.size() != 16) {
        throw std::runtime_error("invalid AES-GCM key, nonce, or tag size");
    }
    std::vector<std::uint8_t> plaintext(cipher.size() + 16);
    EVP_CIPHER_CTX* raw = EVP_CIPHER_CTX_new();
    if (!raw) throw std::runtime_error("EVP_CIPHER_CTX_new failed");
    struct Cleanup { EVP_CIPHER_CTX* value; ~Cleanup(){ EVP_CIPHER_CTX_free(value); } } cleanup{raw};
    int written = 0;
    int total = 0;
    if (EVP_DecryptInit_ex(raw, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(raw, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1 ||
        EVP_DecryptInit_ex(raw, nullptr, nullptr, key.data(), nonce.data()) != 1) {
        throw std::runtime_error("OpenSSL AES-GCM initialization failed");
    }
    if (!associated_data.empty() && EVP_DecryptUpdate(raw, nullptr, &written,
            reinterpret_cast<const unsigned char*>(associated_data.data()), static_cast<int>(associated_data.size())) != 1) {
        throw std::runtime_error("OpenSSL AES-GCM associated-data update failed");
    }
    if (!cipher.empty() && EVP_DecryptUpdate(raw, plaintext.data(), &written, cipher.data(), static_cast<int>(cipher.size())) != 1) {
        throw std::runtime_error("OpenSSL AES-GCM decryption failed");
    }
    total = written;
    if (EVP_CIPHER_CTX_ctrl(raw, EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size()), const_cast<std::uint8_t*>(tag.data())) != 1 ||
        EVP_DecryptFinal_ex(raw, plaintext.data() + total, &written) != 1) {
        throw std::runtime_error("room payload authentication failed");
    }
    total += written;
    plaintext.resize(static_cast<std::size_t>(total));
    return plaintext;
#endif
}

} // namespace vkrooms
