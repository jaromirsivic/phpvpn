package phpvpn.android.bridge.errors

/**
 * Every stable error code of the IPC v1 contract, in the order of
 * client/src/core/base/error.h, with the wire spelling of error.cpp and the
 * recoverable flag. The display text is never the received `description`:
 * [messageKey] selects an Android string resource instead.
 *
 * `tunnel_permission_missing` is the Android addition of plan-android-v001
 * ("Nový chybový kód"); the completeness test accepts it before error.h has it.
 */
enum class ErrorCode(val wireName: String, val recoverable: Boolean) {
    NONE("ok", false),
    INTERNAL("internal_error", true),
    INVALID_ARGUMENT("invalid_argument", false),
    NOT_SUPPORTED("not_supported", false),
    CANCELLED("cancelled", true),
    TIMEOUT("timeout", true),
    BUSY("busy", true),
    CONFIG_UNREADABLE("config_unreadable", false),
    CONFIG_ENCODING_ERROR("config_encoding_error", false),
    CONFIG_TOO_LARGE("config_too_large", false),
    WIZARD_CONTRACT_SELF_CONFLICT("wizard_contract_self_conflict", false),
    WIZARD_SESSION_EXPIRED("wizard_session_expired", false),
    WIZARD_PREREQUISITE_FAILED("wizard_prerequisite_failed", false),
    WIZARD_ATTEMPT_CLOSED("wizard_attempt_closed", false),
    WIZARD_CONNECTION_INTERRUPTED("wizard_connection_interrupted", false),
    WIZARD_COMMIT_UNKNOWN("wizard_commit_unknown", false),
    WIZARD_UNSUPPORTED("wizard_unsupported", false),
    CONFIG_MISSING("config_missing", false),
    CONFIG_NOT_FOUND("config_not_found", false),
    CONFIG_READ_FAILED("config_read_failed", false),
    CONFIG_WRITE_FAILED("config_write_failed", false),
    CONFIG_DIRECTORY_SECURITY_INVALID("config_directory_security_invalid", false),
    CONFIG_INVALID_JSON("config_invalid_json", false),
    CONFIG_INVALID_VALUE("config_invalid_value", false),
    CONFIG_INVALID_URL("config_invalid_url", false),
    CONFIG_INVALID_CIDR("config_invalid_cidr", false),
    CONFIG_INVALID_ADDRESS("config_invalid_address", false),
    CONFIG_INVALID_KEY("config_invalid_key", false),
    CONFIG_INVALID_LIMIT("config_invalid_limit", false),
    CONFIG_QUEUE_BUDGET("config_queue_budget", false),
    CONTRACT_MISSING("contract_missing", false),
    WIRE_INVALID_BASE64URL("wire_invalid_base64url", true),
    WIRE_INVALID_TIMESTAMP("wire_invalid_timestamp", true),
    WIRE_INVALID_UUID("wire_invalid_uuid", true),
    WIRE_INVALID_JSON("wire_invalid_json", true),
    WIRE_UNEXPECTED_FIELD("wire_unexpected_field", true),
    WIRE_MISSING_FIELD("wire_missing_field", true),
    WIRE_DUPLICATE_FIELD("wire_duplicate_field", true),
    WIRE_INVALID_RECORD_ORDER("wire_invalid_record_order", true),
    WIRE_LINE_TOO_LONG("wire_line_too_long", true),
    WIRE_TOO_MANY_RECORDS("wire_too_many_records", true),
    WIRE_BODY_TOO_LARGE("wire_body_too_large", true),
    WIRE_INVALID_BATCH("wire_invalid_batch", true),
    WIRE_INVALID_PACKET("wire_invalid_packet", true),
    CRYPTO_UNAVAILABLE("crypto_unavailable", false),
    CRYPTO_FAILED("crypto_failed", true),
    CRYPTO_TAG_MISMATCH("crypto_tag_mismatch", true),
    TRANSPORT_UNAVAILABLE("transport_unavailable", true),
    TRANSPORT_DNS_FAILED("transport_dns_failed", true),
    TRANSPORT_CONNECT_FAILED("transport_connect_failed", true),
    TRANSPORT_TLS_FAILED("transport_tls_failed", true),
    TRANSPORT_SEND_FAILED("transport_send_failed", true),
    TRANSPORT_RECEIVE_FAILED("transport_receive_failed", true),
    TRANSPORT_RESPONSE_TOO_LARGE("transport_response_too_large", true),
    TRANSPORT_TIMEOUT("transport_timeout", true),
    SERVER_BAD_REQUEST("server_bad_request", false),
    SERVER_UNAUTHORIZED("server_unauthorized", false),
    SERVER_NOT_FOUND("server_not_found", false),
    SERVER_METHOD_NOT_ALLOWED("server_method_not_allowed", false),
    SERVER_PAYLOAD_TOO_LARGE("server_payload_too_large", true),
    SERVER_UNSUPPORTED_MEDIA_TYPE("server_unsupported_media_type", false),
    SERVER_UNAVAILABLE("server_unavailable", true),
    SERVER_INTERNAL_ERROR("server_internal_error", true),
    SERVER_UNEXPECTED_STATUS("server_unexpected_status", true),
    RESPONSE_INVALID("response_invalid", true),
    RESPONSE_REQUEST_ID_MISMATCH("response_request_id_mismatch", true),
    RESPONSE_MAC_INVALID("response_mac_invalid", true),
    SERVER_VERSION_INCOMPATIBLE("server_version_incompatible", false),
    TIME_NOT_SYNCHRONISED("time_not_synchronised", true),
    TIME_INVALID_SAMPLE("time_invalid_sample", true),
    TUNNEL_LIBRARY_MISSING("tunnel_library_missing", false),
    TUNNEL_LIBRARY_INVALID("tunnel_library_invalid", false),
    TUNNEL_API_MISSING("tunnel_api_missing", false),
    TUNNEL_DRIVER_UNSUPPORTED("tunnel_driver_unsupported", false),
    TUNNEL_ADAPTER_FAILED("tunnel_adapter_failed", true),
    TUNNEL_SESSION_FAILED("tunnel_session_failed", true),
    TUNNEL_IO_FAILED("tunnel_io_failed", true),
    TUNNEL_PERMISSION_MISSING("tunnel_permission_missing", false),
    NETCONFIG_PREFLIGHT_CONFLICT("netconfig_preflight_conflict", false),
    NETCONFIG_APPLY_FAILED("netconfig_apply_failed", true),
    NETCONFIG_VERIFY_FAILED("netconfig_verify_failed", true),
    JOURNAL_MISSING("journal_missing", false),
    JOURNAL_CORRUPT("journal_corrupt", false),
    JOURNAL_AMBIGUOUS("journal_ambiguous", false),
    IPC_UNSUPPORTED_VERSION("ipc_unsupported_version", false),
    IPC_UNKNOWN_COMMAND("ipc_unknown_command", false),
    IPC_MESSAGE_TOO_LARGE("ipc_message_too_large", false),
    IPC_MALFORMED("ipc_malformed", false),
    IPC_UNAVAILABLE("ipc_unavailable", true),
    SERVICE_ALREADY_RUNNING("service_already_running", false),
    SERVICE_NOT_RUNNING("service_not_running", true),
    STATE_CONFLICT("state_conflict", true),
    ;

    /** Resource key of the safe one-sentence explanation of this code. */
    val messageKey: ErrorTextKey get() = ErrorTextKey.valueOf("ERROR_" + name)

    companion object {
        private val byWireName = entries.associateBy { it.wireName }

        /** Codes added on Android that the desktop error.h may not list yet. */
        val ANDROID_ADDITIONS: Set<ErrorCode> = setOf(TUNNEL_PERMISSION_MISSING)

        fun fromWire(name: String?): ErrorCode? = name?.let { byWireName[it] }
    }
}
