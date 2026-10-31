package phpvpn.android.standin

import kotlinx.serialization.json.JsonObject
import phpvpn.android.bridge.controller.SaveOperation
import phpvpn.android.bridge.controller.SaveOutcome
import phpvpn.android.bridge.controller.SaveResponseClassifier
import phpvpn.android.bridge.controller.SaveResult
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.IPC_MAX_MESSAGE_BYTES
import phpvpn.android.bridge.ipc.JsonChecks
import phpvpn.android.bridge.ipc.StrictJson
import phpvpn.android.bridge.ipc.WireFormats

/**
 * Kotlin stand-in of `classifySaveResponse` (apps/tray/save_response.cpp)
 * until WP04 exposes the native classifier. Only a correlated `result:ok`
 * confirms, only the listed pre-publication errors with their canonical
 * description and recoverable flag prove a rejection; everything else after a
 * write attempt stays unknown. The descriptions are protocol data from
 * error.cpp (a JVM test compares them), never shown to the user.
 */
object StandInSaveResponseClassifier : SaveResponseClassifier {
    /** The canonical `errorCodeDescription` of every rejecting code (error.cpp). */
    val rejectingDescriptions: Map<ErrorCode, String> = linkedMapOf(
        ErrorCode.BUSY to "Another operation of the same kind is already running.",
        ErrorCode.IPC_MALFORMED to "The IPC message is malformed.",
        ErrorCode.IPC_MESSAGE_TOO_LARGE to "The IPC message exceeds the allowed size.",
        ErrorCode.IPC_UNSUPPORTED_VERSION to "The IPC contract version is not supported.",
        ErrorCode.IPC_UNKNOWN_COMMAND to "The IPC command is not part of the contract.",
        ErrorCode.CONFIG_ENCODING_ERROR to "The configuration file is not supported UTF-8 text.",
        ErrorCode.CONFIG_TOO_LARGE to "The configuration exceeds the 512 KiB limit.",
        ErrorCode.CONFIG_INVALID_JSON to "The configuration is not a valid JSON document.",
        ErrorCode.CONFIG_INVALID_VALUE to "A configuration value has an unexpected type or content.",
        ErrorCode.CONFIG_INVALID_URL to "The server URL is not an absolute http or https URL.",
        ErrorCode.CONFIG_INVALID_CIDR to "The VPN CIDR is not a canonical IPv4 network prefix.",
        ErrorCode.CONFIG_INVALID_ADDRESS to "A VPN address is not a valid client address of the prefix.",
        ErrorCode.CONFIG_INVALID_KEY to "A key is not exactly 64 lowercase hexadecimal characters.",
        ErrorCode.CONFIG_INVALID_LIMIT to "A runtime limit is not a positive integer.",
        ErrorCode.CONFIG_QUEUE_BUDGET to "The queue limit cannot cover the per peer reservation.",
        ErrorCode.WIZARD_CONTRACT_SELF_CONFLICT to
            "This IP is already used by a local communication contract. Choose another IP or correct the contract in Edit Config.",
    )

    private val headerReasons = listOf(
        "header block too large", "at least one header is required", "empty line or missing colon",
        "bare CR is forbidden", "invalid field name", "forbidden control octet",
    )

    override fun classify(
        operation: SaveOperation,
        requestId: String,
        response: ByteArray?,
        writeAttempted: Boolean,
        completedAtMs: Long,
    ): SaveResult {
        val unknown = SaveResult(operation, SaveOutcome.UNKNOWN, ErrorCode.RESPONSE_INVALID)
        if (!writeAttempted) return SaveResult(operation, SaveOutcome.NOT_SENT, ErrorCode.IPC_UNAVAILABLE)
        if (operation.deadlineMs == 0L || completedAtMs >= operation.deadlineMs || !WireFormats.isUuidV4(requestId) ||
            response == null || response.size > IPC_MAX_MESSAGE_BYTES
        ) return unknown
        val root = StrictJson.parse(response) as? JsonObject ?: return unknown
        if (root.size != 3 || JsonChecks.int64(root["ipcVersion"]) != 1L || JsonChecks.string(root["requestId"]) != requestId) return unknown
        if (root.containsKey("result")) {
            return if (JsonChecks.string(root["result"]) == "ok") SaveResult(operation, SaveOutcome.CONFIRMED, ErrorCode.NONE) else unknown
        }
        val error = root["error"] as? JsonObject ?: return unknown
        if (error.size != 3) return unknown
        val code = JsonChecks.string(error["code"]) ?: return unknown
        val description = JsonChecks.string(error["description"]) ?: return unknown
        val recoverable = JsonChecks.bool(error["recoverable"]) ?: return unknown
        for ((candidate, canonical) in rejectingDescriptions) {
            val header = candidate == ErrorCode.CONFIG_INVALID_VALUE && isHttpHeadersErrorDescription(description)
            if (code == candidate.wireName && (description == canonical || header) && recoverable == candidate.recoverable) {
                return SaveResult(operation, SaveOutcome.REJECTED, candidate, if (header) description else "")
            }
        }
        return unknown
    }

    /** `isHttpHeadersErrorDescription` (core/protocol/http_headers.cpp): the curated validator text with its line. */
    fun isHttpHeadersErrorDescription(description: String): Boolean {
        if (description == "masterSetup.httpHeaders is required" || description == "masterSetup.httpHeaders must be a string") return true
        val prefix = "masterSetup.httpHeaders line "
        if (!description.startsWith(prefix)) return false
        val rest = description.substring(prefix.length)
        val end = rest.indexOf(": ")
        if (end <= 0) return false
        val number = rest.substring(0, end)
        if (!number.all { it in '0'..'9' } || (number.toULongOrNull() ?: 0uL) == 0uL) return false
        val reason = rest.substring(end + 2)
        if (reason in headerReasons) return true
        for (suffix in listOf(" is managed by the application or transport", " is a duplicate header name")) {
            if (!reason.endsWith(suffix)) continue
            val name = reason.substring(0, reason.length - suffix.length)
            return name.isNotEmpty() && name.all(::isToken)
        }
        return false
    }

    private fun isToken(c: Char): Boolean = c.code in 0x21..0x7e && c !in "\"(),/:;<=>?@[\\]{}"
}
