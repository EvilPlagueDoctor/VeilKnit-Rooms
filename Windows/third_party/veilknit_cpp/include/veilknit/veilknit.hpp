#pragma once

#include "veilknit/json.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace veilknit {

inline constexpr std::uint16_t protocol_version = 3;

class Error : public std::runtime_error {
public:
    Error(std::string code, std::string message);
    const std::string& code() const noexcept;
private:
    std::string code_;
};

using Bytes = std::vector<std::uint8_t>;

struct Credential {
    std::uint16_t protocol_version = veilknit::protocol_version;
    std::string endpoint;
    std::string app_id;
    std::string display_name;
    std::string secret_hex;
    std::uint64_t credential_generation = 0;

    static Credential load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
};

struct ApiInfo {
    std::uint16_t protocol_version = 0;
    std::string authentication_proof;
    std::vector<std::string> features;
    std::size_t max_message_bytes = 0;
    std::size_t max_store_value_bytes = 0;
    std::uint16_t max_store_subkeys = 0;
    std::size_t max_stores_per_app = 0;
    std::size_t max_store_reads_per_request = 0;
    std::size_t max_store_writes_per_request = 0;
    std::size_t max_store_write_bytes_per_request = 0;
    std::size_t max_signature_payload_bytes = 0;
    std::size_t max_signature_domain_bytes = 0;
};

struct Session {
    std::string app_id;
    std::string session_id;
    std::string token_hex;
    std::uint64_t authenticated_at = 0;
    std::uint64_t expires_at = 0;
    std::vector<std::string> capabilities;
};

struct LocalIdentity {
    std::string username;
    std::string main_dht;
};

struct MessageOptions {
    std::optional<std::string> conversation_id_hex;
    std::optional<std::uint64_t> expires_at;
    bool await_response = false;
};

struct IncomingMessage {
    std::string application_id;
    std::string message_id_hex;
    std::string sender_main_dht;
    std::string recipient_main_dht;
    std::uint64_t posted_at = 0;
    std::uint64_t expires_at = 0;
    std::optional<std::string> conversation_id_hex;
    Bytes payload;
};

struct MailboxStatus {
    std::uint16_t storage_layout_version = 0;
    std::uint32_t mailbox_index_subkeys = 0;
    std::uint32_t mail_send_subkeys = 0;
    std::uint32_t mail_response_subkeys = 0;
    std::size_t index_value_capacity = 0;
    std::size_t payload_value_capacity = 0;
    std::optional<std::string> mailbox_dht;
    std::optional<std::string> mail_send_dht;
    std::string mail_response_dht;
    std::uint64_t receive_key_epoch = 0;
    std::size_t pending_page_sets = 0;
    std::size_t outgoing_message_count = 0;
    std::size_t awaiting_response_count = 0;
    std::size_t stored_inbox_count = 0;
    std::size_t unread_inbox_count = 0;
    std::size_t known_custodian_count = 0;
};

struct AppSigningIdentity {
    std::string application_id;
    std::string main_dht;
    std::uint64_t key_generation = 0;
    std::string public_key_hex;
    std::uint64_t created_at = 0;
    std::string binding;
};

struct AppSignature {
    std::string application_id;
    std::uint64_t key_generation = 0;
    std::string public_key_hex;
    std::string domain;
    std::string signature_hex;
};

struct StoreDescriptor {
    std::string store_id;
    std::string application_id;
    std::string name;
    std::string record_key;
    std::uint16_t subkey_count = 0;
    std::size_t max_value_bytes = 0;
    std::uint64_t generation = 0;
    std::uint64_t created_at = 0;
};

struct StoreValue {
    std::uint32_t location = 0;
    std::optional<Bytes> value;
    bool is_null = false;
    std::optional<std::string> error;
};

struct StoreRead {
    StoreDescriptor store;
    std::vector<StoreValue> values;
};

struct PublicStoreRead {
    std::string record_key;
    std::vector<StoreValue> values;
};

struct StoreWrite {
    std::uint32_t location = 0;
    Bytes value;
};

enum class RestrictionAction { restrict_user, ban };

enum class ObservationKind {
    interaction_succeeded,
    interaction_failed,
    useful_service,
    excessive_activity,
    repetitive_activity,
    suspicious_coordination,
    message_delivered,
    message_rejected,
    unsolicited_message,
    spam,
    harassment,
    valid_dht_response,
    invalid_dht_response,
    invalid_signature,
    impossible_protocol_state,
    malformed_protocol_message,
    deliberate_state_corruption,
    future_timestamp_claim,
    conflicting_account_creation_claim,
    suspicious_creation_burst,
    reachable,
    unreachable,
    stable_availability,
    app_ban_requested,
    user_marked_harmful,
    user_marked_trusted,
    handshake_unavailable,
};

std::vector<std::string> default_capabilities();
std::string to_wire(RestrictionAction action);
std::string to_wire(ObservationKind kind);

class MessageSubscription {
public:
    MessageSubscription(MessageSubscription&&) noexcept;
    MessageSubscription& operator=(MessageSubscription&&) noexcept;
    ~MessageSubscription();
    MessageSubscription(const MessageSubscription&) = delete;
    MessageSubscription& operator=(const MessageSubscription&) = delete;

    IncomingMessage next();
    void close();

private:
    struct Impl;
    explicit MessageSubscription(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend class Client;
};

class Client {
public:
    static std::string discover_endpoint();
    static std::filesystem::path discover_credential_path(const std::string& app_id);
    static Credential load_discovered_credential(const std::string& app_id);
    static ApiInfo api_info(const std::string& endpoint);
    static void ping(const std::string& endpoint);
    static Credential register_app(
        const std::string& endpoint,
        const std::string& app_id,
        const std::string& display_name,
        std::vector<std::string> requested_capabilities = default_capabilities(),
        std::uint64_t timeout_seconds = 15 * 60);

    static Client authenticate(
        const Credential& credential,
        std::vector<std::string> requested_capabilities = default_capabilities());

    const Session& session() const noexcept;
    const std::string& endpoint() const noexcept;

    LocalIdentity identity() const;
    json::Value network_status() const;
    std::string send_message(
        const std::string& recipient_main_dht,
        const Bytes& payload,
        const MessageOptions& options = {}) const;
    void trigger_message_retrieval() const;
    MailboxStatus mailbox_status() const;
    MessageSubscription subscribe_messages() const;

    AppSigningIdentity signing_identity() const;
    AppSigningIdentity rotate_signing_key() const;
    AppSignature sign(const std::string& domain, const Bytes& payload) const;
    bool verify(
        const std::string& public_key_hex,
        const std::string& domain,
        const Bytes& payload,
        const std::string& signature_hex) const;

    std::vector<StoreDescriptor> list_stores() const;
    StoreDescriptor create_store(
        const std::string& name,
        std::uint16_t subkey_count,
        bool initialize = true) const;
    StoreRead read_store(
        const std::string& store_id,
        const std::vector<std::uint32_t>& locations,
        bool force_refresh = false) const;
    StoreDescriptor write_store(
        const std::string& store_id,
        const std::vector<StoreWrite>& writes,
        std::optional<std::uint64_t> expected_generation = std::nullopt) const;
    PublicStoreRead read_public_store(
        const std::string& record_key,
        const std::vector<std::uint32_t>& locations,
        bool force_refresh = false) const;

    std::uint64_t submit_reputation_observation(
        const std::string& subject_main_dht,
        ObservationKind kind,
        std::optional<std::uint32_t> application_code = std::nullopt,
        std::optional<std::string> description = std::nullopt) const;
    void retract_reputation_observation(
        const std::string& subject_main_dht,
        std::uint64_t observation_id) const;
    std::uint64_t request_app_restriction(
        const std::string& subject_main_dht,
        RestrictionAction action,
        const std::string& reason,
        std::optional<std::uint64_t> expires_at = std::nullopt) const;
    void revoke_app_decision(
        const std::string& subject_main_dht,
        std::uint64_t decision_id) const;
    json::Value reputation_view(const std::string& subject_main_dht) const;
    json::Value own_reputation_submissions() const;

    json::Value raw_request(json::Value request) const;

private:
    Client(std::string endpoint, Session session);
    std::string endpoint_;
    Session session_;
    std::shared_ptr<std::atomic<std::uint64_t>> next_request_id_;
};

std::string hex_encode(const Bytes& bytes);
Bytes hex_decode(const std::string& value);
std::string base64_encode(const Bytes& bytes);
Bytes base64_decode(const std::string& value);

} // namespace veilknit
