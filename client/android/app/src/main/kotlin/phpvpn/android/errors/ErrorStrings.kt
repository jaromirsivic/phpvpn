package phpvpn.android.errors

import androidx.annotation.StringRes
import phpvpn.android.R
import phpvpn.android.bridge.errors.ErrorTextKey

/**
 * Resolves an [ErrorTextKey] to its string resource in values/errors.xml and
 * values-cs/errors.xml. The `when` is exhaustive, so a new key does not compile
 * until it has a resource; the JVM tests check that both languages define it.
 */
object ErrorStrings {
    @StringRes
    fun resource(key: ErrorTextKey): Int = when (key) {
        ErrorTextKey.ERROR_NONE -> R.string.error_ok
        ErrorTextKey.ERROR_INTERNAL -> R.string.error_internal_error
        ErrorTextKey.ERROR_INVALID_ARGUMENT -> R.string.error_invalid_argument
        ErrorTextKey.ERROR_NOT_SUPPORTED -> R.string.error_not_supported
        ErrorTextKey.ERROR_CANCELLED -> R.string.error_cancelled
        ErrorTextKey.ERROR_TIMEOUT -> R.string.error_timeout
        ErrorTextKey.ERROR_BUSY -> R.string.error_busy
        ErrorTextKey.ERROR_CONFIG_UNREADABLE -> R.string.error_config_unreadable
        ErrorTextKey.ERROR_CONFIG_ENCODING_ERROR -> R.string.error_config_encoding_error
        ErrorTextKey.ERROR_CONFIG_TOO_LARGE -> R.string.error_config_too_large
        ErrorTextKey.ERROR_WIZARD_CONTRACT_SELF_CONFLICT -> R.string.error_wizard_contract_self_conflict
        ErrorTextKey.ERROR_WIZARD_SESSION_EXPIRED -> R.string.error_wizard_session_expired
        ErrorTextKey.ERROR_WIZARD_PREREQUISITE_FAILED -> R.string.error_wizard_prerequisite_failed
        ErrorTextKey.ERROR_WIZARD_ATTEMPT_CLOSED -> R.string.error_wizard_attempt_closed
        ErrorTextKey.ERROR_WIZARD_CONNECTION_INTERRUPTED -> R.string.error_wizard_connection_interrupted
        ErrorTextKey.ERROR_WIZARD_COMMIT_UNKNOWN -> R.string.error_wizard_commit_unknown
        ErrorTextKey.ERROR_WIZARD_UNSUPPORTED -> R.string.error_wizard_unsupported
        ErrorTextKey.ERROR_CONFIG_MISSING -> R.string.error_config_missing
        ErrorTextKey.ERROR_CONFIG_NOT_FOUND -> R.string.error_config_not_found
        ErrorTextKey.ERROR_CONFIG_READ_FAILED -> R.string.error_config_read_failed
        ErrorTextKey.ERROR_CONFIG_WRITE_FAILED -> R.string.error_config_write_failed
        ErrorTextKey.ERROR_CONFIG_DIRECTORY_SECURITY_INVALID -> R.string.error_config_directory_security_invalid
        ErrorTextKey.ERROR_CONFIG_INVALID_JSON -> R.string.error_config_invalid_json
        ErrorTextKey.ERROR_CONFIG_INVALID_VALUE -> R.string.error_config_invalid_value
        ErrorTextKey.ERROR_CONFIG_INVALID_URL -> R.string.error_config_invalid_url
        ErrorTextKey.ERROR_CONFIG_INVALID_CIDR -> R.string.error_config_invalid_cidr
        ErrorTextKey.ERROR_CONFIG_INVALID_ADDRESS -> R.string.error_config_invalid_address
        ErrorTextKey.ERROR_CONFIG_INVALID_KEY -> R.string.error_config_invalid_key
        ErrorTextKey.ERROR_CONFIG_INVALID_LIMIT -> R.string.error_config_invalid_limit
        ErrorTextKey.ERROR_CONFIG_QUEUE_BUDGET -> R.string.error_config_queue_budget
        ErrorTextKey.ERROR_CONTRACT_MISSING -> R.string.error_contract_missing
        ErrorTextKey.ERROR_WIRE_INVALID_BASE64URL -> R.string.error_wire_invalid_base64url
        ErrorTextKey.ERROR_WIRE_INVALID_TIMESTAMP -> R.string.error_wire_invalid_timestamp
        ErrorTextKey.ERROR_WIRE_INVALID_UUID -> R.string.error_wire_invalid_uuid
        ErrorTextKey.ERROR_WIRE_INVALID_JSON -> R.string.error_wire_invalid_json
        ErrorTextKey.ERROR_WIRE_UNEXPECTED_FIELD -> R.string.error_wire_unexpected_field
        ErrorTextKey.ERROR_WIRE_MISSING_FIELD -> R.string.error_wire_missing_field
        ErrorTextKey.ERROR_WIRE_DUPLICATE_FIELD -> R.string.error_wire_duplicate_field
        ErrorTextKey.ERROR_WIRE_INVALID_RECORD_ORDER -> R.string.error_wire_invalid_record_order
        ErrorTextKey.ERROR_WIRE_LINE_TOO_LONG -> R.string.error_wire_line_too_long
        ErrorTextKey.ERROR_WIRE_TOO_MANY_RECORDS -> R.string.error_wire_too_many_records
        ErrorTextKey.ERROR_WIRE_BODY_TOO_LARGE -> R.string.error_wire_body_too_large
        ErrorTextKey.ERROR_WIRE_INVALID_BATCH -> R.string.error_wire_invalid_batch
        ErrorTextKey.ERROR_WIRE_INVALID_PACKET -> R.string.error_wire_invalid_packet
        ErrorTextKey.ERROR_CRYPTO_UNAVAILABLE -> R.string.error_crypto_unavailable
        ErrorTextKey.ERROR_CRYPTO_FAILED -> R.string.error_crypto_failed
        ErrorTextKey.ERROR_CRYPTO_TAG_MISMATCH -> R.string.error_crypto_tag_mismatch
        ErrorTextKey.ERROR_TRANSPORT_UNAVAILABLE -> R.string.error_transport_unavailable
        ErrorTextKey.ERROR_TRANSPORT_DNS_FAILED -> R.string.error_transport_dns_failed
        ErrorTextKey.ERROR_TRANSPORT_CONNECT_FAILED -> R.string.error_transport_connect_failed
        ErrorTextKey.ERROR_TRANSPORT_TLS_FAILED -> R.string.error_transport_tls_failed
        ErrorTextKey.ERROR_TRANSPORT_SEND_FAILED -> R.string.error_transport_send_failed
        ErrorTextKey.ERROR_TRANSPORT_RECEIVE_FAILED -> R.string.error_transport_receive_failed
        ErrorTextKey.ERROR_TRANSPORT_RESPONSE_TOO_LARGE -> R.string.error_transport_response_too_large
        ErrorTextKey.ERROR_TRANSPORT_TIMEOUT -> R.string.error_transport_timeout
        ErrorTextKey.ERROR_SERVER_BAD_REQUEST -> R.string.error_server_bad_request
        ErrorTextKey.ERROR_SERVER_UNAUTHORIZED -> R.string.error_server_unauthorized
        ErrorTextKey.ERROR_SERVER_NOT_FOUND -> R.string.error_server_not_found
        ErrorTextKey.ERROR_SERVER_METHOD_NOT_ALLOWED -> R.string.error_server_method_not_allowed
        ErrorTextKey.ERROR_SERVER_PAYLOAD_TOO_LARGE -> R.string.error_server_payload_too_large
        ErrorTextKey.ERROR_SERVER_UNSUPPORTED_MEDIA_TYPE -> R.string.error_server_unsupported_media_type
        ErrorTextKey.ERROR_SERVER_UNAVAILABLE -> R.string.error_server_unavailable
        ErrorTextKey.ERROR_SERVER_INTERNAL_ERROR -> R.string.error_server_internal_error
        ErrorTextKey.ERROR_SERVER_UNEXPECTED_STATUS -> R.string.error_server_unexpected_status
        ErrorTextKey.ERROR_RESPONSE_INVALID -> R.string.error_response_invalid
        ErrorTextKey.ERROR_RESPONSE_REQUEST_ID_MISMATCH -> R.string.error_response_request_id_mismatch
        ErrorTextKey.ERROR_RESPONSE_MAC_INVALID -> R.string.error_response_mac_invalid
        ErrorTextKey.ERROR_SERVER_VERSION_INCOMPATIBLE -> R.string.error_server_version_incompatible
        ErrorTextKey.ERROR_TIME_NOT_SYNCHRONISED -> R.string.error_time_not_synchronised
        ErrorTextKey.ERROR_TIME_INVALID_SAMPLE -> R.string.error_time_invalid_sample
        ErrorTextKey.ERROR_TUNNEL_LIBRARY_MISSING -> R.string.error_tunnel_library_missing
        ErrorTextKey.ERROR_TUNNEL_LIBRARY_INVALID -> R.string.error_tunnel_library_invalid
        ErrorTextKey.ERROR_TUNNEL_API_MISSING -> R.string.error_tunnel_api_missing
        ErrorTextKey.ERROR_TUNNEL_DRIVER_UNSUPPORTED -> R.string.error_tunnel_driver_unsupported
        ErrorTextKey.ERROR_TUNNEL_ADAPTER_FAILED -> R.string.error_tunnel_adapter_failed
        ErrorTextKey.ERROR_TUNNEL_SESSION_FAILED -> R.string.error_tunnel_session_failed
        ErrorTextKey.ERROR_TUNNEL_IO_FAILED -> R.string.error_tunnel_io_failed
        ErrorTextKey.ERROR_TUNNEL_PERMISSION_MISSING -> R.string.error_tunnel_permission_missing
        ErrorTextKey.ERROR_NETCONFIG_PREFLIGHT_CONFLICT -> R.string.error_netconfig_preflight_conflict
        ErrorTextKey.ERROR_NETCONFIG_APPLY_FAILED -> R.string.error_netconfig_apply_failed
        ErrorTextKey.ERROR_NETCONFIG_VERIFY_FAILED -> R.string.error_netconfig_verify_failed
        ErrorTextKey.ERROR_JOURNAL_MISSING -> R.string.error_journal_missing
        ErrorTextKey.ERROR_JOURNAL_CORRUPT -> R.string.error_journal_corrupt
        ErrorTextKey.ERROR_JOURNAL_AMBIGUOUS -> R.string.error_journal_ambiguous
        ErrorTextKey.ERROR_IPC_UNSUPPORTED_VERSION -> R.string.error_ipc_unsupported_version
        ErrorTextKey.ERROR_IPC_UNKNOWN_COMMAND -> R.string.error_ipc_unknown_command
        ErrorTextKey.ERROR_IPC_MESSAGE_TOO_LARGE -> R.string.error_ipc_message_too_large
        ErrorTextKey.ERROR_IPC_MALFORMED -> R.string.error_ipc_malformed
        ErrorTextKey.ERROR_IPC_UNAVAILABLE -> R.string.error_ipc_unavailable
        ErrorTextKey.ERROR_SERVICE_ALREADY_RUNNING -> R.string.error_service_already_running
        ErrorTextKey.ERROR_SERVICE_NOT_RUNNING -> R.string.error_service_not_running
        ErrorTextKey.ERROR_STATE_CONFLICT -> R.string.error_state_conflict
        ErrorTextKey.CONNECTION_ENDPOINT_UNAVAILABLE_OR_INVALID_TITLE -> R.string.error_connection_endpoint_unavailable_or_invalid_title
        ErrorTextKey.CONNECTION_ENDPOINT_UNAVAILABLE_OR_INVALID_MESSAGE -> R.string.error_connection_endpoint_unavailable_or_invalid_message
        ErrorTextKey.CONNECTION_ENDPOINT_UNAVAILABLE_OR_INVALID_ACTION -> R.string.error_connection_endpoint_unavailable_or_invalid_action
        ErrorTextKey.CONNECTION_SERVER_VERIFICATION_FAILED_TITLE -> R.string.error_connection_server_verification_failed_title
        ErrorTextKey.CONNECTION_SERVER_VERIFICATION_FAILED_MESSAGE -> R.string.error_connection_server_verification_failed_message
        ErrorTextKey.CONNECTION_SERVER_VERIFICATION_FAILED_ACTION -> R.string.error_connection_server_verification_failed_action
        ErrorTextKey.CONNECTION_ACCESS_KEY_REJECTED_TITLE -> R.string.error_connection_access_key_rejected_title
        ErrorTextKey.CONNECTION_ACCESS_KEY_REJECTED_MESSAGE -> R.string.error_connection_access_key_rejected_message
        ErrorTextKey.CONNECTION_ACCESS_KEY_REJECTED_ACTION -> R.string.error_connection_access_key_rejected_action
        ErrorTextKey.CONNECTION_TUNNEL_START_FAILED_TITLE -> R.string.error_connection_tunnel_start_failed_title
        ErrorTextKey.CONNECTION_TUNNEL_START_FAILED_MESSAGE -> R.string.error_connection_tunnel_start_failed_message
        ErrorTextKey.CONNECTION_TUNNEL_START_FAILED_ACTION -> R.string.error_connection_tunnel_start_failed_action
        ErrorTextKey.CONNECTION_COMMUNICATION_FAILED_TITLE -> R.string.error_connection_communication_failed_title
        ErrorTextKey.CONNECTION_COMMUNICATION_FAILED_MESSAGE -> R.string.error_connection_communication_failed_message
        ErrorTextKey.CONNECTION_COMMUNICATION_FAILED_ACTION -> R.string.error_connection_communication_failed_action
        ErrorTextKey.CONNECTION_COMMUNICATION_TIMEOUT_TITLE -> R.string.error_connection_communication_timeout_title
        ErrorTextKey.CONNECTION_COMMUNICATION_TIMEOUT_MESSAGE -> R.string.error_connection_communication_timeout_message
        ErrorTextKey.CONNECTION_COMMUNICATION_TIMEOUT_ACTION -> R.string.error_connection_communication_timeout_action
        ErrorTextKey.CONNECTION_COMMUNICATION_CONTRACTS_VERIFICATION_FAILED_TITLE -> R.string.error_connection_communication_contracts_verification_failed_title
        ErrorTextKey.CONNECTION_COMMUNICATION_CONTRACTS_VERIFICATION_FAILED_MESSAGE -> R.string.error_connection_communication_contracts_verification_failed_message
        ErrorTextKey.CONNECTION_COMMUNICATION_CONTRACTS_VERIFICATION_FAILED_ACTION -> R.string.error_connection_communication_contracts_verification_failed_action
        ErrorTextKey.CONNECTION_STATUS_UNAVAILABLE_TITLE -> R.string.error_connection_status_unavailable_title
        ErrorTextKey.CONNECTION_STATUS_UNAVAILABLE_MESSAGE -> R.string.error_connection_status_unavailable_message
        ErrorTextKey.CONNECTION_STATUS_UNAVAILABLE_ACTION -> R.string.error_connection_status_unavailable_action
        ErrorTextKey.CONNECTION_SERVER_VERSION_INCOMPATIBLE_TITLE -> R.string.error_connection_server_version_incompatible_title
        ErrorTextKey.CONNECTION_SERVER_VERSION_INCOMPATIBLE_MESSAGE -> R.string.error_connection_server_version_incompatible_message
        ErrorTextKey.CONNECTION_SERVER_VERSION_INCOMPATIBLE_ACTION -> R.string.error_connection_server_version_incompatible_action
        ErrorTextKey.CONNECTION_CONNECTION_FAILED_TITLE -> R.string.error_connection_connection_failed_title
        ErrorTextKey.CONNECTION_CONNECTION_FAILED_MESSAGE -> R.string.error_connection_connection_failed_message
        ErrorTextKey.CONNECTION_CONNECTION_FAILED_ACTION -> R.string.error_connection_connection_failed_action
        ErrorTextKey.CONNECTION_DISCONNECT_UNCONFIRMED_TITLE -> R.string.error_connection_disconnect_unconfirmed_title
        ErrorTextKey.CONNECTION_DISCONNECT_UNCONFIRMED_MESSAGE -> R.string.error_connection_disconnect_unconfirmed_message
        ErrorTextKey.CONNECTION_DISCONNECT_UNCONFIRMED_ACTION -> R.string.error_connection_disconnect_unconfirmed_action
        ErrorTextKey.ACTION_VERIFY_AGAIN -> R.string.error_action_verify_again
        ErrorTextKey.ACTION_CONFIG_CHECK -> R.string.error_action_config_check
        ErrorTextKey.ACTION_CONFIG_DIRECTORY_SECURITY -> R.string.error_action_config_directory_security
        ErrorTextKey.ACTION_SERVER_UNAUTHORIZED -> R.string.error_action_server_unauthorized
        ErrorTextKey.ACTION_BUSY -> R.string.error_action_busy
        ErrorTextKey.ACTION_SESSION_EXPIRED -> R.string.error_action_session_expired
        ErrorTextKey.ACTION_COMMIT_UNKNOWN -> R.string.error_action_commit_unknown
        ErrorTextKey.ACTION_CONNECTION_INTERRUPTED -> R.string.error_action_connection_interrupted
        ErrorTextKey.ACTION_CONTRACT_SELF_CONFLICT -> R.string.error_action_contract_self_conflict
        ErrorTextKey.ACTION_UNSUPPORTED -> R.string.error_action_unsupported
        ErrorTextKey.ACTION_SERVER_VERSION_INCOMPATIBLE -> R.string.error_action_server_version_incompatible
        ErrorTextKey.ERROR_UNKNOWN -> R.string.error_unknown
    }
}
