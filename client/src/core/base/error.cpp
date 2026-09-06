#include "core/base/error.h"

namespace phpvpn {
namespace {

struct ErrorInfo {
    ErrorCode code;
    const char* name;
    const char* description;
    bool recoverable;
};

// clang-format off
constexpr ErrorInfo kErrors[] = {
    {ErrorCode::wizard_session_expired, "wizard_session_expired", "This wizard session has expired. Open the wizard again.", false},
    {ErrorCode::wizard_prerequisite_failed, "wizard_prerequisite_failed", "Complete the required checks for these exact values before continuing.", false},
    {ErrorCode::wizard_attempt_closed, "wizard_attempt_closed", "This check attempt is already closed. Choose Next to start a new attempt.", false},
    {ErrorCode::wizard_connection_interrupted, "wizard_connection_interrupted", "Another application or user started a VPN connection. Previous checks are no longer valid.", false},
    {ErrorCode::wizard_commit_unknown, "wizard_commit_unknown", "The configuration publication has not been confirmed. Check its receipt before taking another action.", false},
    {ErrorCode::wizard_unsupported, "wizard_unsupported", "This wizard requires the updated PHPVPN service.", false},
    {ErrorCode::config_unreadable, "config_unreadable", "The configuration file could not be accessed.", false},
    {ErrorCode::config_encoding_error, "config_encoding_error", "The configuration file is not supported UTF-8 text.", false},
    {ErrorCode::config_too_large, "config_too_large", "The configuration exceeds the 512 KiB limit.", false},
    {ErrorCode::wizard_contract_self_conflict, "wizard_contract_self_conflict", "This IP is already used by a local communication contract. Choose another IP or correct the contract in Edit Config.", false},
    {ErrorCode::none,                          "ok",                            "No error.",                                                        false},

    {ErrorCode::internal,                      "internal_error",                "An unexpected internal error occurred.",                           true},
    {ErrorCode::invalid_argument,              "invalid_argument",              "An argument was not valid.",                                       false},
    {ErrorCode::not_supported,                 "not_supported",                 "The requested operation is not supported.",                        false},
    {ErrorCode::cancelled,                     "cancelled",                     "The operation was cancelled.",                                     true},
    {ErrorCode::timeout,                       "timeout",                       "The operation did not finish in time.",                            true},
    {ErrorCode::busy,                          "busy",                          "Another operation of the same kind is already running.",           true},

    {ErrorCode::config_missing,                "config_missing",                "No configuration is active.",                                      false},
    {ErrorCode::config_not_found,              "config_not_found",              "The configuration file does not exist.",                           false},
    {ErrorCode::config_read_failed,            "config_read_failed",            "The configuration file could not be read.",                        false},
    {ErrorCode::config_write_failed,           "config_write_failed",           "The configuration file could not be written.",                     false},
    {ErrorCode::config_invalid_json,           "config_invalid_json",           "The configuration is not a valid JSON document.",                  false},
    {ErrorCode::config_invalid_value,          "config_invalid_value",          "A configuration value has an unexpected type or content.",         false},
    {ErrorCode::config_invalid_url,            "config_invalid_url",            "The server URL is not an absolute http or https URL.",             false},
    {ErrorCode::config_invalid_cidr,           "config_invalid_cidr",           "The VPN CIDR is not a canonical IPv4 network prefix.",              false},
    {ErrorCode::config_invalid_address,        "config_invalid_address",        "A VPN address is not a valid client address of the prefix.",       false},
    {ErrorCode::config_invalid_key,            "config_invalid_key",            "A key is not exactly 64 lowercase hexadecimal characters.",        false},
    {ErrorCode::config_invalid_limit,          "config_invalid_limit",          "A runtime limit is not a positive integer.",                       false},
    {ErrorCode::config_queue_budget,           "config_queue_budget",           "The queue limit cannot cover the per peer reservation.",           false},
    {ErrorCode::contract_missing,              "contract_missing",              "No communication contract exists for the peer.",                   false},

    {ErrorCode::wire_invalid_base64url,        "wire_invalid_base64url",        "A field is not canonical base64url without padding.",              true},
    {ErrorCode::wire_invalid_timestamp,        "wire_invalid_timestamp",        "A timestamp is not an exact UTC value with milliseconds.",         true},
    {ErrorCode::wire_invalid_uuid,             "wire_invalid_uuid",             "An identifier is not a lowercase UUID version 4.",                 true},
    {ErrorCode::wire_invalid_json,             "wire_invalid_json",             "A protocol message is not valid compact JSON.",                    true},
    {ErrorCode::wire_unexpected_field,         "wire_unexpected_field",         "A protocol message contains an unknown member.",                   true},
    {ErrorCode::wire_missing_field,            "wire_missing_field",            "A protocol message is missing a required member.",                 true},
    {ErrorCode::wire_duplicate_field,          "wire_duplicate_field",          "A protocol message contains a duplicate member.",                  true},
    {ErrorCode::wire_invalid_record_order,     "wire_invalid_record_order",     "The response records do not follow the required order.",           true},
    {ErrorCode::wire_line_too_long,            "wire_line_too_long",            "A response line exceeds the configured line limit.",               true},
    {ErrorCode::wire_too_many_records,         "wire_too_many_records",         "The response contains more data records than configured.",         true},
    {ErrorCode::wire_body_too_large,           "wire_body_too_large",           "The response body exceeds the configured size limit.",             true},
    {ErrorCode::wire_invalid_batch,            "wire_invalid_batch",            "A decrypted payload batch has an invalid structure.",              true},
    {ErrorCode::wire_invalid_packet,           "wire_invalid_packet",           "An IPv4 packet failed structural validation.",                     true},

    {ErrorCode::crypto_unavailable,            "crypto_unavailable",            "The platform cryptography provider is unavailable.",               false},
    {ErrorCode::crypto_failed,                 "crypto_failed",                 "A cryptographic operation failed.",                                true},
    {ErrorCode::crypto_tag_mismatch,           "crypto_tag_mismatch",           "An authentication tag did not verify.",                            true},

    {ErrorCode::transport_unavailable,         "transport_unavailable",         "The HTTP transport could not be initialised.",                     true},
    {ErrorCode::transport_dns_failed,          "transport_dns_failed",          "The server name could not be resolved.",                           true},
    {ErrorCode::transport_connect_failed,      "transport_connect_failed",      "The connection to the server could not be established.",           true},
    {ErrorCode::transport_tls_failed,          "transport_tls_failed",          "The TLS handshake or certificate validation failed.",              true},
    {ErrorCode::transport_send_failed,         "transport_send_failed",         "The request could not be sent.",                                   true},
    {ErrorCode::transport_receive_failed,      "transport_receive_failed",      "The response could not be received completely.",                   true},
    {ErrorCode::transport_response_too_large,  "transport_response_too_large",  "The response exceeded the configured body limit.",                 true},
    {ErrorCode::transport_timeout,             "transport_timeout",             "The request did not complete within the response timeout.",        true},

    {ErrorCode::server_bad_request,            "server_bad_request",            "The server rejected the request as malformed.",                    false},
    {ErrorCode::server_unauthorized,           "server_unauthorized",           "The server rejected the credentials or the request time.",         false},
    {ErrorCode::server_not_found,              "server_not_found",              "The server does not know the requested action.",                   false},
    {ErrorCode::server_method_not_allowed,     "server_method_not_allowed",     "The server does not allow this HTTP method.",                      false},
    {ErrorCode::server_payload_too_large,      "server_payload_too_large",      "The server rejected the request body as too large.",               true},
    {ErrorCode::server_unsupported_media_type, "server_unsupported_media_type", "The server rejected the request content type.",                    false},
    {ErrorCode::server_unavailable,            "server_unavailable",            "The server is temporarily unavailable.",                           true},
    {ErrorCode::server_internal_error,         "server_internal_error",         "The server reported an internal error.",                           true},
    {ErrorCode::server_unexpected_status,      "server_unexpected_status",      "The server returned an unexpected HTTP status.",                   true},

    {ErrorCode::response_invalid,              "response_invalid",              "The server response failed validation.",                           true},
    {ErrorCode::response_request_id_mismatch,  "response_request_id_mismatch",  "The server response belongs to a different request.",              true},
    {ErrorCode::response_mac_invalid,          "response_mac_invalid",          "The response authentication code did not verify.",                 true},

    {ErrorCode::time_not_synchronised,         "time_not_synchronised",         "No verified server time is available yet.",                        true},
    {ErrorCode::time_invalid_sample,           "time_invalid_sample",           "The measured time sample was not usable.",                         true},

    {ErrorCode::tunnel_library_missing,        "tunnel_library_missing",        "The tunnel driver library is missing from the installation.",      false},
    {ErrorCode::tunnel_library_invalid,        "tunnel_library_invalid",        "The tunnel driver library failed integrity verification.",         false},
    {ErrorCode::tunnel_api_missing,            "tunnel_api_missing",            "The tunnel driver library does not export a required function.",   false},
    {ErrorCode::tunnel_driver_unsupported,     "tunnel_driver_unsupported",     "The running tunnel driver version is not supported.",              false},
    {ErrorCode::tunnel_adapter_failed,         "tunnel_adapter_failed",         "The virtual adapter could not be created or opened.",              true},
    {ErrorCode::tunnel_session_failed,         "tunnel_session_failed",         "The tunnel session could not be started.",                         true},
    {ErrorCode::tunnel_io_failed,              "tunnel_io_failed",              "A tunnel read or write operation failed.",                         true},
    {ErrorCode::netconfig_preflight_conflict,  "netconfig_preflight_conflict",  "An existing address, prefix or route conflicts with the tunnel.",  false},
    {ErrorCode::netconfig_apply_failed,        "netconfig_apply_failed",        "The address or route could not be applied.",                       true},
    {ErrorCode::netconfig_verify_failed,       "netconfig_verify_failed",       "The network state after the change did not pass verification.",    true},
    {ErrorCode::journal_missing,               "journal_missing",               "The ownership journal is missing.",                                false},
    {ErrorCode::journal_corrupt,               "journal_corrupt",               "The ownership journal is unreadable or has an unknown version.",   false},
    {ErrorCode::journal_ambiguous,             "journal_ambiguous",             "Ownership of an existing resource could not be proven.",           false},

    {ErrorCode::ipc_unsupported_version,       "ipc_unsupported_version",       "The IPC contract version is not supported.",                       false},
    {ErrorCode::ipc_unknown_command,           "ipc_unknown_command",           "The IPC command is not part of the contract.",                     false},
    {ErrorCode::ipc_message_too_large,         "ipc_message_too_large",         "The IPC message exceeds the allowed size.",                        false},
    {ErrorCode::ipc_malformed,                 "ipc_malformed",                 "The IPC message is malformed.",                                    false},
    {ErrorCode::ipc_unavailable,               "ipc_unavailable",               "The phpvpn service is not reachable over the control pipe.",       true},
    {ErrorCode::service_already_running,       "service_already_running",       "Another instance of the service already owns the installation.",   false},
    {ErrorCode::service_not_running,           "service_not_running",           "The service is not running.",                                      true},
    {ErrorCode::state_conflict,                "state_conflict",                "The command conflicts with the current state.",                    true},
};
// clang-format on

const ErrorInfo& lookup(ErrorCode code) noexcept {
    for (const ErrorInfo& info : kErrors) {
        if (info.code == code) {
            return info;
        }
    }
    return kErrors[1];  // internal
}

}  // namespace

const char* errorCodeName(ErrorCode code) noexcept { return lookup(code).name; }

const char* errorCodeDescription(ErrorCode code) noexcept { return lookup(code).description; }

bool errorCodeIsRecoverable(ErrorCode code) noexcept { return lookup(code).recoverable; }

std::string Error::message() const {
    std::string text = errorCodeDescription(code);
    if (!detail.empty()) {
        text += " (";
        text += detail;
        text += ")";
    }
    return text;
}

}  // namespace phpvpn
