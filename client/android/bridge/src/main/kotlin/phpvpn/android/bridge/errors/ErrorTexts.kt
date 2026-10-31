package phpvpn.android.bridge.errors

/**
 * Safe public connection error categories of `get-status.lastError`, produced by
 * `SafeError::fromConnection` (core/runtime/status_snapshot.cpp), plus the two
 * local conditions of the progress model (`status_unavailable` and
 * `disconnect_unconfirmed`).
 */
enum class ConnectionErrorCode(val wireName: String) {
    ENDPOINT_UNAVAILABLE_OR_INVALID("endpoint_unavailable_or_invalid"),
    SERVER_VERIFICATION_FAILED("server_verification_failed"),
    ACCESS_KEY_REJECTED("access_key_rejected"),
    TUNNEL_START_FAILED("tunnel_start_failed"),
    // Android only (WP03): the VPN consent is missing or revoked, screen 37.
    TUNNEL_PERMISSION_MISSING("tunnel_permission_missing"),
    COMMUNICATION_FAILED("communication_failed"),
    COMMUNICATION_TIMEOUT("communication_timeout"),
    COMMUNICATION_CONTRACTS_VERIFICATION_FAILED("communication_contracts_verification_failed"),
    STATUS_UNAVAILABLE("status_unavailable"),
    SERVER_VERSION_INCOMPATIBLE("server_version_incompatible"),
    CONNECTION_FAILED("connection_failed"),
    DISCONNECT_UNCONFIRMED("disconnect_unconfirmed"),
    ;

    val title: ErrorTextKey get() = ErrorTextKey.valueOf("CONNECTION_${name}_TITLE")
    val message: ErrorTextKey get() = ErrorTextKey.valueOf("CONNECTION_${name}_MESSAGE")
    val action: ErrorTextKey get() = ErrorTextKey.valueOf("CONNECTION_${name}_ACTION")

    companion object {
        private val byWireName = entries.associateBy { it.wireName }

        fun fromWire(name: String?): ConnectionErrorCode? = name?.let { byWireName[it] }
    }
}

/**
 * Display text of a failed Connect, the Kotlin port of `ProgressErrorText`
 * (apps/tray/progress_model.h): the allowlisted canonical [code] and three
 * resource keys instead of the English literals.
 */
data class ProgressErrorText(
    val code: String,
    val description: ErrorTextKey,
    val panel: ErrorTextKey,
    val action: ErrorTextKey,
)

object ErrorTexts {
    // Exactly the allowlist of progressErrorText() in progress_model.cpp, in order.
    private val progressAllowlist: List<ConnectionErrorCode> = listOf(
        ConnectionErrorCode.ENDPOINT_UNAVAILABLE_OR_INVALID,
        ConnectionErrorCode.SERVER_VERIFICATION_FAILED,
        ConnectionErrorCode.ACCESS_KEY_REJECTED,
        ConnectionErrorCode.TUNNEL_START_FAILED,
        ConnectionErrorCode.COMMUNICATION_FAILED,
        ConnectionErrorCode.COMMUNICATION_TIMEOUT,
        ConnectionErrorCode.COMMUNICATION_CONTRACTS_VERIFICATION_FAILED,
        ConnectionErrorCode.STATUS_UNAVAILABLE,
        ConnectionErrorCode.SERVER_VERSION_INCOMPATIBLE,
    )

    /** The codes progressErrorText() recognises; everything else maps to communication_failed. */
    val progressCodes: List<String> = progressAllowlist.map { it.wireName }

    /**
     * Port of `progressErrorText`: only this allowlist supplies display text,
     * never a received description. Unknown codes cannot inject text or claim a
     * key rejection, so they fall back to `communication_failed`.
     */
    fun progressErrorText(code: String): ProgressErrorText {
        val entry = progressAllowlist.firstOrNull { it.wireName == code } ?: ConnectionErrorCode.COMMUNICATION_FAILED
        return ProgressErrorText(entry.wireName, entry.message, entry.title, entry.action)
    }

    /** Text of the local Disconnect failure of the disconnect progress (screen 38). */
    val disconnectUnconfirmed: ProgressErrorText = ConnectionErrorCode.DISCONNECT_UNCONFIRMED.let {
        ProgressErrorText(it.wireName, it.message, it.title, it.action)
    }

    /**
     * Message key for any received error code: an [ErrorCode], a public
     * connection category or, for anything else, [ErrorTextKey.ERROR_UNKNOWN].
     * The received description is never used.
     */
    fun messageKey(code: String?): ErrorTextKey {
        ErrorCode.fromWire(code)?.let { return it.messageKey }
        ConnectionErrorCode.fromWire(code)?.let { return it.message }
        return ErrorTextKey.ERROR_UNKNOWN
    }

    /**
     * Port of the `suggestedAction` selection of `WizardIpc::safeError`
     * (core/ipc/wizard_ipc.cpp) as a resource key. [field] is the error field
     * (serverUrl, serverSecret, virtualIp, accessKey, configuration or null).
     *
     * `config_directory_security_invalid` points to restarting the app on
     * Android (plan-android-v001 step 10): `prepareStorage` normalises the
     * private directory on every runtime start, there is no administrator.
     */
    fun wizardActionKey(code: String, field: String?): ErrorTextKey {
        val known = ErrorCode.fromWire(code)
        return when {
            known == ErrorCode.CONFIG_DIRECTORY_SECURITY_INVALID -> ErrorTextKey.ACTION_CONFIG_DIRECTORY_SECURITY
            (code.startsWith("config_") && (field == null || field == "configuration")) ||
                known == ErrorCode.JOURNAL_CORRUPT || known == ErrorCode.JOURNAL_AMBIGUOUS ||
                known == ErrorCode.JOURNAL_MISSING -> ErrorTextKey.ACTION_CONFIG_CHECK
            known == ErrorCode.SERVER_UNAUTHORIZED -> ErrorTextKey.ACTION_SERVER_UNAUTHORIZED
            known == ErrorCode.BUSY -> ErrorTextKey.ACTION_BUSY
            known == ErrorCode.WIZARD_SESSION_EXPIRED -> ErrorTextKey.ACTION_SESSION_EXPIRED
            known == ErrorCode.WIZARD_COMMIT_UNKNOWN -> ErrorTextKey.ACTION_COMMIT_UNKNOWN
            known == ErrorCode.WIZARD_CONNECTION_INTERRUPTED -> ErrorTextKey.ACTION_CONNECTION_INTERRUPTED
            known == ErrorCode.WIZARD_CONTRACT_SELF_CONFLICT -> ErrorTextKey.ACTION_CONTRACT_SELF_CONFLICT
            known == ErrorCode.WIZARD_UNSUPPORTED -> ErrorTextKey.ACTION_UNSUPPORTED
            known == ErrorCode.SERVER_VERSION_INCOMPATIBLE -> ErrorTextKey.ACTION_SERVER_VERSION_INCOMPATIBLE
            else -> ErrorTextKey.ACTION_VERIFY_AGAIN
        }
    }
}
