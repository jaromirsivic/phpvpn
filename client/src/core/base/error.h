// Stable error identity for the whole client.
//
// Every failure that crosses a role boundary is reported with one of these
// codes. Codes are part of the IPC contract described in client.md chapter
// "Verejne lokalni IPC", so their spelling must stay stable across releases.
#pragma once

#include <string>

namespace phpvpn {

enum class ErrorCode {
    none = 0,

    // Generic
    internal,
    invalid_argument,
    not_supported,
    cancelled,
    timeout,
    busy,

    // Configuration, client.md "Konfiguracni soubor"
    config_unreadable,
    config_encoding_error,
    config_too_large,
    wizard_contract_self_conflict,
    wizard_session_expired,
    wizard_prerequisite_failed,
    wizard_attempt_closed,
    wizard_connection_interrupted,
    wizard_commit_unknown,
    wizard_unsupported,
    config_missing,
    config_not_found,
    config_read_failed,
    config_write_failed,
    config_directory_security_invalid,
    config_invalid_json,
    config_invalid_value,
    config_invalid_url,
    config_invalid_cidr,
    config_invalid_address,
    config_invalid_key,
    config_invalid_limit,
    config_queue_budget,
    contract_missing,

    // Wire format, protocol.md chapters 6, 8 and 9
    wire_invalid_base64url,
    wire_invalid_timestamp,
    wire_invalid_uuid,
    wire_invalid_json,
    wire_unexpected_field,
    wire_missing_field,
    wire_duplicate_field,
    wire_invalid_record_order,
    wire_line_too_long,
    wire_too_many_records,
    wire_body_too_large,
    wire_invalid_batch,
    wire_invalid_packet,

    // Cryptography
    crypto_unavailable,
    crypto_failed,
    crypto_tag_mismatch,

    // Transport, protocol.md chapter 4 and client.md "Prijem dat"
    transport_unavailable,
    transport_dns_failed,
    transport_connect_failed,
    transport_tls_failed,
    transport_send_failed,
    transport_receive_failed,
    transport_response_too_large,
    transport_timeout,

    // Server reported statuses, protocol.md chapter 13
    server_bad_request,
    server_unauthorized,
    server_not_found,
    server_method_not_allowed,
    server_payload_too_large,
    server_unsupported_media_type,
    server_unavailable,
    server_internal_error,
    server_unexpected_status,

    // Response verification
    response_invalid,
    response_request_id_mismatch,
    response_mac_invalid,
    // The verified `serverVersion` has another MAJOR than this client.
    server_version_incompatible,

    // Time synchronisation, client.md "Synchronizace casu"
    time_not_synchronised,
    time_invalid_sample,

    // Tunnel and network configuration, client.md "Windows"
    tunnel_library_missing,
    tunnel_library_invalid,
    tunnel_api_missing,
    tunnel_driver_unsupported,
    tunnel_adapter_failed,
    tunnel_session_failed,
    tunnel_io_failed,
    // Android: the VPN consent is missing or was revoked when establish() is
    // needed (plan-android-v001, "Nový chybový kód"). No other platform uses it.
    tunnel_permission_missing,
    netconfig_preflight_conflict,
    netconfig_apply_failed,
    netconfig_verify_failed,
    journal_missing,
    journal_corrupt,
    journal_ambiguous,

    // Service and IPC
    ipc_unsupported_version,
    ipc_unknown_command,
    ipc_message_too_large,
    ipc_malformed,
    ipc_unavailable,
    service_already_running,
    service_not_running,
    state_conflict,
};

// Stable machine readable spelling, safe for logs and IPC payloads.
const char* errorCodeName(ErrorCode code) noexcept;

// Short human readable description. Never contains a key, tag, ciphertext,
// packet content, internal path or system error text.
const char* errorCodeDescription(ErrorCode code) noexcept;

// True when a caller may retry after a backoff instead of asking the user to
// change something, see client.md "Obnova".
bool errorCodeIsRecoverable(ErrorCode code) noexcept;

// True for a recoverable failure of one `data` exchange that the running
// connection retries without releasing the tunnel (soft recovery, client.md
// "Obnova"): transport failures, server 5xx, and a response that could not be
// received or verified. False for everything that needs a new bootstrap or
// the user, including 401, cancellation and configuration errors.
bool errorCodeIsTransient(ErrorCode code) noexcept;

struct Error {
    ErrorCode code = ErrorCode::internal;
    std::string detail;

    Error() = default;
    Error(ErrorCode c) : code(c) {}
    Error(ErrorCode c, std::string d) : code(c), detail(std::move(d)) {}

    const char* name() const noexcept { return errorCodeName(code); }
    bool recoverable() const noexcept { return errorCodeIsRecoverable(code); }
    bool transient() const noexcept { return errorCodeIsTransient(code); }

    // Safe one line rendering used by the logger and by IPC error envelopes.
    std::string message() const;
};

}  // namespace phpvpn
