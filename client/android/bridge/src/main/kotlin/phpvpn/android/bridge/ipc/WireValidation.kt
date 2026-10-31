package phpvpn.android.bridge.ipc

import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.JsonChecks.bool
import phpvpn.android.bridge.ipc.JsonChecks.isNull
import phpvpn.android.bridge.ipc.JsonChecks.nullableString
import phpvpn.android.bridge.ipc.JsonChecks.oneOf
import phpvpn.android.bridge.ipc.JsonChecks.shape
import phpvpn.android.bridge.ipc.JsonChecks.string
import phpvpn.android.bridge.ipc.JsonChecks.uint64

/**
 * Checks of the native document content that the Kotlin client cannot fully
 * repeat: the C++ `documentMatches` and the get-default-config template check
 * run the complete configuration validator (`parseAndValidateConfig`,
 * `analyzeWizardDocument`). WP04 may inject a JNI implementation; the default
 * [StructuralConfigDocumentCheck] is deliberately lenient for `invalid` text so
 * that a broken document always stays editable.
 */
interface ConfigDocumentCheck {
    fun matches(document: String, availability: ConfigAvailability): Boolean

    fun validTemplate(document: String): Boolean
}

object StructuralConfigDocumentCheck : ConfigDocumentCheck {
    override fun matches(document: String, availability: ConfigAvailability): Boolean = when (availability) {
        ConfigAvailability.VALID -> structurallyValid(document)
        ConfigAvailability.REPAIRABLE -> StrictJson.parse(document) is JsonObject
        ConfigAvailability.INVALID -> true
        else -> false
    }

    override fun validTemplate(document: String): Boolean = structurallyValid(document)

    /** The members every complete configuration has (config.h, default template). */
    fun structurallyValid(document: String): Boolean {
        val root = StrictJson.parse(document) as? JsonObject ?: return false
        val setup = root["masterSetup"] as? JsonObject ?: return false
        if (root["communicationContracts"] !is JsonObject) return false
        val url = string(setup["serverUrl"]) ?: return false
        if (!url.startsWith("http://") && !url.startsWith("https://")) return false
        if (string(setup["serverSecret"]) == null) return false
        val headers = string(setup["httpHeaders"]) ?: return false
        if (!HttpHeaders.isValid(headers)) return false
        val (network, prefix) = WireFormats.parseCidr(string(setup["vpnCidr"])) ?: return false
        val ip = WireFormats.parseIpv4(string(setup["virtualIp"])) ?: return false
        val broadcast = network or ((1L shl (32 - prefix)) - 1)
        if (ip < network + 2 || ip > broadcast - 1) return false
        return WireFormats.isLowercaseHex64(string(setup["accessKey"]))
    }
}

/** Outcome of the Wizard reply check: a result object, a structured error, or a rejection. */
sealed interface WizardReplyCheck {
    data class Result(val value: JsonElement) : WizardReplyCheck

    data class Error(val error: JsonObject, val legacyUnsupported: Boolean) : WizardReplyCheck

    data class Rejected(val code: ErrorCode) : WizardReplyCheck
}

/**
 * The closed-schema checks of the desktop parsers, ported 1:1 so Android
 * accepts and rejects exactly the same replies:
 * - [status]: `tray::parseStatusResponse` (apps/tray/status_model.cpp);
 * - [wizardReply] and the helpers: `WizardIpc::parseReply`, `parseView`,
 *   `parseFields`, `parseContext`, `parseError`, `parseOperation`,
 *   `parseHttpDiagnostic` (core/ipc/wizard_ipc.cpp);
 * - [serverInfo]: `parseServerInfoResult` (core/presentation/server_info_model.cpp)
 *   without the native plaintext model, which WP04 provides.
 * Where a desktop parser ignores members (get-info plaintext) the client ignores
 * them as well; everywhere else unknown members are rejected.
 */
object WireValidation {
    private val malformed = ErrorCode.IPC_MALFORMED

    private fun uuid(value: JsonElement?) = WireFormats.isUuidV4(string(value))

    private fun nonEmpty(value: JsonElement?) = string(value)?.isNotEmpty() == true

    /** `token`: a non-empty string without NUL. */
    private fun token(value: JsonElement?): Boolean = string(value)?.let { it.isNotEmpty() && '\u0000' !in it } ?: false

    /** `versionOne` of the tray: the integer 1. */
    private fun versionOne(value: JsonElement?) = JsonChecks.int64(value) == 1L

    private fun absolutePath(value: JsonElement?) = WireFormats.isAbsoluteConfigPath(string(value))

    // ------------------------------------------------------------ get-status

    private val states = arrayOf("disconnected", "connecting", "connected", "recovering", "error")
    private val phases = arrayOf(
        "idle", "health", "info", "access-key-check", "communication-contracts-check", "starting-tunnel",
        "running", "reconnecting", "stopping",
    )
    private val tristates = arrayOf("unknown", "yes", "no")
    private val checkpoints = arrayOf("pending", "success", "failed")
    val counterNames = listOf(
        "dataPollsTotal", "dataPollsEmpty", "dataPollsWithPayload", "serializedDataRequestBodyBytes",
        "serializedDataResponseBodyBytes", "ipv4PacketsSent", "ipv4PacketsReceived", "ipv4BytesSent", "ipv4BytesReceived",
    )

    /** Complete v1 status envelope, never a partial update. */
    fun status(root: JsonElement?): Boolean {
        if (root !is JsonObject || root.size != 3 || !versionOne(root["ipcVersion"]) || !uuid(root["requestId"])) return false
        val status = root["status"] as? JsonObject ?: return false
        return statusObject(status)
    }

    fun statusObject(status: JsonObject): Boolean {
        val diagnostic = status["httpDiagnostic"]
        if (status.size != (if (status.containsKey("httpDiagnostic")) 27 else 26)) return false
        if (uint64(status["connectionEpoch"]) == null || !versionOne(status["ipcVersion"]) ||
            !nonEmpty(status["clientVersion"]) || !nonEmpty(status["stateChangedAt"]) ||
            uint64(status["recoveryCount"]) == null || !oneOf(status["state"], *states) ||
            !oneOf(status["phase"], *phases) || !oneOf(status["serverReachable"], *tristates) ||
            !oneOf(status["serverVerified"], *tristates) || !oneOf(status["tunnelReady"], *tristates)
        ) return false
        for (name in listOf("serverUrlCheck", "serverSecretCheck", "accessKeyCheck", "communicationContractsCheck")) {
            if (!oneOf(status[name], *checkpoints)) return false
        }
        if (WireFormats.parseIso8601(string(status["stateChangedAt"])) == null) return false
        if (!status.containsKey("connectionStartedAt")) return false
        val started = status["connectionStartedAt"]
        val hasStarted = !isNull(started)
        if (hasStarted && WireFormats.parseIso8601(string(started)) == null) return false
        val state = string(status["state"])
        if ((state == "connected" && !hasStarted) || ((state == "disconnected" || state == "connecting") && hasStarted)) return false
        val counters = counterNames.map { uint64(status[it]) ?: return false }
        val empty = counters[1]
        val payload = counters[2]
        if (empty > ULong.MAX_VALUE - payload || counters[0] != empty + payload) return false
        if (!status.containsKey("lastError")) return false
        val error = status["lastError"]
        var hasError = false
        var recoverable = false
        var errorCode = ""
        if (!isNull(error)) {
            if (error !is JsonObject || error.size != 3 || !nonEmpty(error["code"]) || !nonEmpty(error["description"]) ||
                bool(error["recoverable"]) == null
            ) return false
            hasError = true
            errorCode = string(error["code"])!!
            recoverable = bool(error["recoverable"])!!
        }
        if (!status.containsKey("connectAttemptId")) return false
        val attempt = status["connectAttemptId"]
        if (!isNull(attempt) && !uuid(attempt)) return false
        val epoch = uint64(status["connectionEpoch"])!!
        if ((epoch != 0uL) != !isNull(attempt)) return false
        if (status.containsKey("httpDiagnostic")) {
            if (epoch == 0uL || state != "error" || string(status["phase"]) != "idle" || !hasError || recoverable ||
                string(status["accessKeyCheck"]) != "pending" || hasStarted
            ) return false
            if (!httpDiagnostic(diagnostic)) return false
            val url = string(status["serverUrlCheck"])
            val secret = string(status["serverSecretCheck"])
            if (string((diagnostic as JsonObject)["action"]) == "health") {
                if (!((url == "failed" && secret == "pending") || (url == "success" && secret == "failed"))) return false
            } else if (url != "success" || secret != "failed") {
                return false
            }
            if (errorCode != "endpoint_unavailable_or_invalid" && errorCode != "server_verification_failed" &&
                errorCode != "communication_failed"
            ) return false
        }
        return true
    }

    // ------------------------------------------------------------ original envelopes

    /** `{ipcVersion:1, requestId, error:{code, description, recoverable}}` correlated with [requestId]. */
    fun classicError(root: JsonElement?, requestId: String): Boolean {
        if (!shape(root, "ipcVersion", "requestId", "error")) return false
        root as JsonObject
        if (!versionOne(root["ipcVersion"]) || string(root["requestId"]) != requestId) return false
        val error = root["error"]
        return shape(error, "code", "description", "recoverable") && nonEmpty((error as JsonObject)["code"]) &&
            string(error["description"]) != null && bool(error["recoverable"]) != null
    }

    fun acknowledgement(root: JsonElement?, requestId: String): Boolean =
        shape(root, "ipcVersion", "requestId", "result") && versionOne((root as JsonObject)["ipcVersion"]) &&
            string(root["requestId"]) == requestId && string(root["result"]) == "ok"

    private fun queue(value: JsonElement?): Boolean {
        if (!shape(
                value, "itemCount", "occupiedBytes", "limitBytes", "peerQueueCount", "activeSubBucketCount",
                "inFlightBytes", "queuedItemCount",
            )
        ) return false
        value as JsonObject
        return listOf("itemCount", "peerQueueCount", "activeSubBucketCount", "queuedItemCount").all { uint64(value[it]) != null } &&
            listOf("occupiedBytes", "limitBytes", "inFlightBytes").all { JsonChecks.int64(value[it]) != null }
    }

    private val diagnosticsCounterNames = counterNames + listOf(
        "packetsDropped", "batchesRejected", "responsesRejected", "peakResponseCiphertextBytes",
        "queueExpiredPackets", "outboundQueueExpiredPackets", "inboundQueueExpiredPackets",
    )

    /** get-diagnostics: the closed object of `IpcProtocol::buildDiagnosticsResponse`. */
    fun diagnostics(root: JsonElement?, requestId: String): Boolean {
        if (!shape(root, "ipcVersion", "requestId", "diagnostics")) return false
        root as JsonObject
        if (!versionOne(root["ipcVersion"]) || string(root["requestId"]) != requestId) return false
        val d = root["diagnostics"]
        if (!shape(
                d, "state", "phase", "clientVersion", "serviceUptimeSeconds", "configurationValid",
                "configurationWarnings", "lastHealthVerified", "lastInfoVerified", "lastDataVerified", "lastRecoveryAt",
                "recoveryCount", "lastSoftRecoveryAt", "softRecoveryCount", "tunnelReady", "outboundQueue", "inboundQueue",
                "counters", "lastError",
            )
        ) return false
        d as JsonObject
        val warnings = d["configurationWarnings"] as? JsonArray ?: return false
        val counters = d["counters"]
        val error = d["lastError"]
        return oneOf(d["state"], *states) && oneOf(d["phase"], *phases) && string(d["clientVersion"]) != null &&
            JsonChecks.int64(d["serviceUptimeSeconds"]) != null && bool(d["configurationValid"]) != null &&
            warnings.all { string(it) != null } &&
            listOf("lastHealthVerified", "lastInfoVerified", "lastDataVerified", "tunnelReady").all { oneOf(d[it], *tristates) } &&
            string(d["lastRecoveryAt"]) != null && string(d["lastSoftRecoveryAt"]) != null &&
            uint64(d["recoveryCount"]) != null && uint64(d["softRecoveryCount"]) != null &&
            queue(d["outboundQueue"]) && queue(d["inboundQueue"]) &&
            shape(counters, *diagnosticsCounterNames.toTypedArray()) &&
            diagnosticsCounterNames.all { uint64((counters as JsonObject)[it]) != null } &&
            (isNull(error) || (shape(error, "code", "description", "recoverable") && nonEmpty((error as JsonObject)["code"]) &&
                string(error["description"]) != null && bool(error["recoverable"]) != null))
    }

    /** get-server-info result: exactly the three members, valid address and local masked contracts. */
    fun serverInfo(result: JsonElement?): Boolean {
        if (!shape(result, "infoPlaintext", "localAddress", "localMaskedCommunicationContracts")) return false
        result as JsonObject
        if (string(result["infoPlaintext"]) == null) return false
        val owner = WireFormats.parseIpv4(string(result["localAddress"])) ?: return false
        if ((owner and 255) !in 2..254) return false
        val contracts = result["localMaskedCommunicationContracts"] as? JsonArray ?: return false
        if (contracts.size > 252) return false
        val seen = HashSet<Long>()
        for (item in contracts) {
            if (!shape(item, "peer", "hash")) return false
            item as JsonObject
            if (!WireFormats.isLowercaseHex64(string(item["hash"]))) return false
            val peer = WireFormats.parseIpv4(string(item["peer"])) ?: return false
            if ((peer and 255) !in 2..254 || peer == owner || (peer shr 8) != (owner shr 8) || !seen.add(peer)) return false
        }
        return true
    }

    // ------------------------------------------------------------ http-octets-v1

    private fun diagnosticMessage(value: JsonElement?): Boolean {
        if (!shape(value, "originalSizeBytes", "prefix", "separator", "suffix")) return false
        value as JsonObject
        val size = uint64(value["originalSizeBytes"], positive = true) ?: return false
        val prefix = string(value["prefix"]) ?: return false
        val separator = string(value["separator"]) ?: return false
        val suffix = string(value["suffix"]) ?: return false
        val truncated = size > HttpDiagnosticMessage.HTTP_DIAGNOSTIC_LIMIT.toULong()
        if (separator != (if (truncated) HttpDiagnosticMessage.SEPARATOR else "")) return false
        val prefixLength = if (truncated) HttpDiagnosticMessage.HTTP_DIAGNOSTIC_PREFIX.toULong() else size
        val suffixLength = if (truncated) HttpDiagnosticMessage.HTTP_DIAGNOSTIC_TAIL.toULong() else 0uL
        return octets(prefix, prefixLength) && octets(suffix, suffixLength)
    }

    /** Every character is one octet U+0000..U+00FF and the count is exact. */
    private fun octets(text: String, expected: ULong): Boolean =
        expected <= HttpDiagnosticMessage.HTTP_DIAGNOSTIC_LIMIT.toULong() && text.length.toULong() == expected &&
            text.all { it.code <= 0xFF }

    fun httpDiagnostic(value: JsonElement?): Boolean {
        if (!shape(value, "format", "action", "responseComplete", "requestHeadersSource", "responseHeadersSource", "request", "response")) {
            return false
        }
        value as JsonObject
        return oneOf(value["format"], HttpDiagnostic.FORMAT) && oneOf(value["action"], "health", "info") &&
            bool(value["responseComplete"]) != null &&
            oneOf(value["requestHeadersSource"], "winhttp", "merged", "application") &&
            oneOf(value["responseHeadersSource"], "winhttp", "available", "unavailable") &&
            diagnosticMessage(value["request"]) && diagnosticMessage(value["response"])
    }

    // ------------------------------------------------------------ Wizard objects

    private val fieldNames = arrayOf(
        "serverUrl", "serverSecret", "virtualIp", "accessKey", "httpHeaders",
        "useDefaultServerSecretInWizard", "autoDetectIpAddressInWizard", "useDefaultAccessKeyInWizard",
    )
    private val flagNames = arrayOf("useDefaultServerSecretInWizard", "autoDetectIpAddressInWizard", "useDefaultAccessKeyInWizard")

    private fun withMember(value: JsonObject, name: String, member: JsonElement) = JsonObject(value + (name to member))

    /** `parseFields`: null when valid, else ipc_malformed or wizard_unsupported (historical shapes). */
    fun fields(value: JsonElement?): ErrorCode? {
        if (shape(value, "serverUrl", "serverSecret", "virtualIp", "accessKey")) {
            return if ((value as JsonObject).values.all { string(it) != null }) ErrorCode.WIZARD_UNSUPPORTED else malformed
        }
        if (shape(value, "serverUrl", "serverSecret", "virtualIp", "accessKey", *flagNames)) {
            val candidate = withMember(value as JsonObject, "httpHeaders", JsonPrimitive(HttpHeaders.DEFAULT))
            return if (fields(candidate) == null) ErrorCode.WIZARD_UNSUPPORTED else malformed
        }
        if (!shape(value, *fieldNames)) return malformed
        value as JsonObject
        for (name in listOf("serverUrl", "serverSecret", "virtualIp", "accessKey", "httpHeaders")) {
            if (string(value[name]) == null) return malformed
        }
        if (!HttpHeaders.isValid(string(value["httpHeaders"])!!)) return malformed
        if (flagNames.any { bool(value[it]) == null }) return malformed
        return null
    }

    private val contextLimits = listOf(
        "maxOutboundQueueBytes", "maxInboundQueueBytes", "minOutboundPerPeerQueueBytes", "minInboundPerPeerQueueBytes",
        "maxQueuedPacketAgeMs", "maxResponseBodyBytes", "maxNdjsonLineBytes", "maxDataRecordsPerResponse",
        "responseTimeoutSeconds", "timeSyncIntervalSeconds",
    )

    /** `parseContext`, including the queue reservation and timeout bounds. */
    fun context(value: JsonElement?): Boolean {
        if (!shape(value, "httpHeaders", "vpnCidr", "trustAnyTlsCertificate", *contextLimits.toTypedArray())) return false
        value as JsonObject
        if (bool(value["trustAnyTlsCertificate"]) == null) return false
        val (_, prefix) = WireFormats.parseCidr(string(value["vpnCidr"])) ?: return false
        if (!HttpHeaders.isValid(string(value["httpHeaders"]) ?: return false)) return false
        val limits = HashMap<String, Long>()
        for (name in contextLimits) {
            val number = uint64(value[name], positive = true) ?: return false
            if (number > Long.MAX_VALUE.toULong()) return false
            limits[name] = number.toLong()
        }
        if (limits["responseTimeoutSeconds"]!! > MAX_HTTP_TIMEOUT_SECONDS ||
            limits["maxNdjsonLineBytes"]!! > limits["maxResponseBodyBytes"]!!
        ) return false
        val peers = WireFormats.clientAddressCount(prefix) - 1
        // Divide before multiplying: hostile limits cannot wrap the reservation.
        return limits["minOutboundPerPeerQueueBytes"]!! <= limits["maxOutboundQueueBytes"]!! / peers &&
            limits["minInboundPerPeerQueueBytes"]!! <= limits["maxInboundQueueBytes"]!! / peers
    }

    private val viewNames = arrayOf(
        "sessionId", "serviceInstanceId", "revisionToken", "availability", "mode", "fields", "context",
        "contractCount", "existingDefaultContractPeers", "connectionEpoch", "vpnState", "vpnPhase",
    )
    private val availabilities = arrayOf("missing", "unreadable", "encoding_error", "too_large", "invalid", "repairable", "valid")
    private val readable = arrayOf("valid", "repairable", "invalid")

    /** `parseView`: null when valid, else ipc_malformed or wizard_unsupported. */
    fun view(value: JsonElement?): ErrorCode? {
        if (value !is JsonObject) return malformed
        val fieldsValue = value["fields"]
        if (fieldsValue != null && shape(fieldsValue, "serverUrl", "serverSecret", "virtualIp", "accessKey", *flagNames)) {
            val upgraded = withMember(fieldsValue as JsonObject, "httpHeaders", JsonPrimitive(HttpHeaders.DEFAULT))
            return if (view(withMember(value, "fields", upgraded)) == null) ErrorCode.WIZARD_UNSUPPORTED else malformed
        }
        val historical = viewNames.filter { it != "existingDefaultContractPeers" }.toTypedArray()
        if (shape(value, *historical)) {
            // Diagnose only a fully valid historical view. Never downgrade or use it.
            var candidate = value
            if (fieldsValue != null && !isNull(fieldsValue)) {
                if (fields(fieldsValue) != ErrorCode.WIZARD_UNSUPPORTED) return malformed
                var upgraded = fieldsValue as JsonObject
                for (flag in flagNames) upgraded = withMember(upgraded, flag, JsonPrimitive(false))
                upgraded = withMember(upgraded, "httpHeaders", JsonPrimitive(HttpHeaders.DEFAULT))
                candidate = withMember(candidate, "fields", upgraded)
            }
            candidate = withMember(candidate, "existingDefaultContractPeers", JsonArray(emptyList()))
            return if (view(candidate) == null) ErrorCode.WIZARD_UNSUPPORTED else malformed
        }
        if (!shape(value, *viewNames) || !uuid(value["sessionId"]) || !uuid(value["serviceInstanceId"]) ||
            !nullableString(value["revisionToken"]) || !oneOf(value["availability"], *availabilities) ||
            uint64(value["contractCount"]) == null || uint64(value["connectionEpoch"]) == null
        ) return malformed
        if (!oneOf(value["mode"], "create", "edit", "repair", "reset") || !oneOf(value["vpnState"], *states) ||
            !oneOf(value["vpnPhase"], *phases)
        ) return malformed
        val hasFields = !isNull(fieldsValue)
        if (hasFields) fields(fieldsValue)?.let { return it }
        val contextValue = value["context"]
        val hasContext = !isNull(contextValue)
        if (hasContext && !context(contextValue)) return malformed
        if (hasFields != hasContext) return malformed
        val availability = string(value["availability"])!!
        if (if (availability in readable) !token(value["revisionToken"]) else !isNull(value["revisionToken"])) return malformed
        val contractCount = uint64(value["contractCount"])!!
        val peers = value["existingDefaultContractPeers"] as? JsonArray ?: return malformed
        if (peers.size > 14 || peers.size.toULong() > contractCount) return malformed
        var previous = -1L
        for (peerValue in peers) {
            val cidr = if (hasContext) WireFormats.parseCidr(string((contextValue as JsonObject)["vpnCidr"])) else null
            if (string(peerValue) == null || cidr == null || !WireFormats.supportsDefaultContracts(cidr.first, cidr.second)) {
                return malformed
            }
            val peer = WireFormats.parseIpv4(string(peerValue)) ?: return malformed
            if ((peer and 0xFFFFFF00L) != cidr.first || (peer and 255) < 2 || (peer and 255) > 15 || peer <= previous) {
                return malformed
            }
            previous = peer
        }
        val mode = string(value["mode"])
        if (!hasFields && (mode != "reset" || contractCount != 0uL || availability in arrayOf("missing", "valid", "repairable"))) {
            return malformed
        }
        if (mode == "create" && availability != "missing") return malformed
        if (mode == "edit" && availability != "valid") return malformed
        if (mode == "repair" && availability != "repairable") return malformed
        if ((mode == "create" || mode == "reset") && contractCount != 0uL) return malformed
        return null
    }

    private val errorNames = setOf("code", "description", "recoverable", "field", "httpStatus", "suggestedAction", "serverUrl", "configPath")

    /** `parseError`: the closed structured Wizard error. */
    fun error(value: JsonElement?): Boolean {
        if (value !is JsonObject) return false
        val expected = 6 + (if (value.containsKey("serverUrl")) 1 else 0) + (if (value.containsKey("configPath")) 1 else 0)
        if (value.size != expected) return false
        if (listOf("code", "description", "recoverable", "field", "httpStatus", "suggestedAction").any { !value.containsKey(it) }) return false
        if (value.keys.any { it !in errorNames }) return false
        if (string(value["code"]) == null || string(value["description"]) == null || bool(value["recoverable"]) == null ||
            string(value["suggestedAction"]) == null
        ) return false
        val field = value["field"]
        if (!isNull(field) && !oneOf(field, "serverUrl", "serverSecret", "virtualIp", "accessKey", "configuration")) return false
        val status = value["httpStatus"]
        if (!isNull(status)) {
            val number = uint64(status) ?: return false
            if (number < 100uL || number > 599uL) return false
        }
        for (name in listOf("serverUrl", "configPath")) {
            if (value.containsKey(name) && string(value[name]) == null) return false
        }
        return !value.containsKey("configPath") || absolutePath(value["configPath"])
    }

    fun receipt(value: JsonElement?): Boolean =
        shape(value, "commitId", "revisionToken", "commitState") && uuid((value as JsonObject)["commitId"]) &&
            token(value["revisionToken"]) && oneOf(value["commitState"], "confirmed")

    private val kinds = arrayOf("url", "secret", "access", "finish")
    private val operationStates = arrayOf("pending", "running", "cancelling", "succeeded", "failed", "cancelled", "interrupted")
    private val operationNames = arrayOf(
        "operationId", "kind", "operationState", "connectionEpoch", "clientAttemptId", "attemptSequence", "error", "payload",
    )

    /** `parseOperation`: the typed OperationResult with its closed payload per kind. */
    fun operation(value: JsonElement?): Boolean {
        if (value !is JsonObject) return false
        val hasDiagnostic = value.containsKey("httpDiagnostic")
        val validShape = if (hasDiagnostic) shape(value, *operationNames, "httpDiagnostic") else shape(value, *operationNames)
        if (!validShape || !uuid(value["operationId"]) || uint64(value["connectionEpoch"]) == null) return false
        val kind = string(value["kind"]).takeIf { it in kinds } ?: return false
        val state = string(value["operationState"]).takeIf { it in operationStates } ?: return false
        if (kind == "finish") {
            if (!isNull(value["clientAttemptId"]) || !isNull(value["attemptSequence"])) return false
        } else if (!uuid(value["clientAttemptId"]) || uint64(value["attemptSequence"], positive = true) == null) {
            return false
        }
        val error = value["error"]
        val hasError = !isNull(error)
        if (hasError && !error(error)) return false
        val payload = value["payload"]
        if (hasDiagnostic) {
            if (state != "failed" || !hasError || !isNull(payload) || (kind != "url" && kind != "secret")) return false
            error as JsonObject
            if (error.containsKey("serverUrl") || error.containsKey("configPath") ||
                WireFormats.utf8Length(string(error["code"])!!) > 128 ||
                WireFormats.utf8Length(string(error["description"])!!) > 4096 ||
                WireFormats.utf8Length(string(error["suggestedAction"])!!) > 4096 ||
                string(error["field"]) != (if (kind == "url") "serverUrl" else "serverSecret")
            ) return false
            if (!httpDiagnostic(value["httpDiagnostic"])) return false
            if (kind == "url" && string((value["httpDiagnostic"] as JsonObject)["action"]) != "health") return false
        }
        if (state != "succeeded") {
            if (!isNull(payload)) return false
            if (state == "failed" && !hasError) return false
            if ((state == "pending" || state == "running") && hasError) return false
            return true
        }
        if (hasError) return false
        if (kind == "finish") return receipt(payload)
        if (payload !is JsonObject || bool(payload["verified"]) != true) return false
        return when (kind) {
            "url" -> shape(payload, "verified")
            "secret" -> shape(payload, "verified", "suggestedIp") && nullableString(payload["suggestedIp"]) &&
                (isNull(payload["suggestedIp"]) || WireFormats.parseIpv4(string(payload["suggestedIp"])) != null)
            else -> shape(payload, "verified", "discardedRecordCount") && uint64(payload["discardedRecordCount"]) != null
        }
    }

    // ------------------------------------------------------------ configuration snapshots

    private fun snapshotMetadata(value: JsonObject): Boolean {
        if (!oneOf(value["availability"], *availabilities) || !absolutePath(value["configPath"])) return false
        val readableDocument = oneOf(value["availability"], *readable)
        if (!value.containsKey("revisionToken") || !value.containsKey("reasonCode")) return false
        val revision = value["revisionToken"]
        val reason = value["reasonCode"]
        return (if (readableDocument) token(revision) else isNull(revision)) &&
            (if (oneOf(value["availability"], "valid")) isNull(reason) else token(reason))
    }

    private fun documentMatches(document: JsonElement?, declared: JsonElement?, check: ConfigDocumentCheck): Boolean {
        val text = string(document) ?: return false
        if (WireFormats.utf8Length(text) > MAX_CONFIG_FILE_BYTES) return false
        val availability = ConfigAvailability.entries.firstOrNull { it.wireName == string(declared) } ?: return false
        if (!availability.readable) return false
        return check.matches(text, availability)
    }

    // ------------------------------------------------------------ Wizard replies

    /** Port of `WizardIpc::parseReply` for [command] correlated with [requestId]. */
    fun wizardReply(command: IpcCommand, requestId: String, root: JsonElement?, check: ConfigDocumentCheck): WizardReplyCheck {
        if (root !is JsonObject || uint64(root["ipcVersion"]) != 1uL || !uuid(root["requestId"]) ||
            string(root["requestId"]) != requestId
        ) return WizardReplyCheck.Rejected(malformed)
        if (shape(root, "ipcVersion", "requestId", "error")) {
            val legacy = root["error"]
            // The old service has exactly this three-member error envelope. Only
            // its correlated unknown-command error permits compatibility mapping.
            if (shape(legacy, "code", "description", "recoverable") && oneOf((legacy as JsonObject)["code"], "ipc_unknown_command") &&
                string(legacy["description"]) != null && bool(legacy["recoverable"]) == false
            ) return WizardReplyCheck.Error(WizardErrors.unsupportedJson(), legacyUnsupported = true)
            return if (error(legacy)) WizardReplyCheck.Error(legacy as JsonObject, false) else WizardReplyCheck.Rejected(malformed)
        }
        if (!shape(root, "ipcVersion", "requestId", "result")) return WizardReplyCheck.Rejected(malformed)
        val value = root["result"]!!
        val valid: Boolean = when (command) {
            IpcCommand.WIZARD_OPEN, IpcCommand.WIZARD_RESET, IpcCommand.WIZARD_RESUME -> {
                view(value)?.let { return WizardReplyCheck.Rejected(it) }
                value as JsonObject
                val reset = string(value["mode"]) == "reset"
                val hasFields = !isNull(value["fields"])
                when (command) {
                    IpcCommand.WIZARD_RESET -> reset && hasFields
                    IpcCommand.WIZARD_OPEN -> !(reset && hasFields)
                    else -> true
                }
            }
            IpcCommand.WIZARD_GET_OPERATION -> operation(value)
            IpcCommand.WIZARD_CHECK_URL, IpcCommand.WIZARD_CHECK_SECRET, IpcCommand.WIZARD_CHECK_ACCESS, IpcCommand.WIZARD_FINISH ->
                shape(value, "operationId", "operationState") && uuid((value as JsonObject)["operationId"]) &&
                    oneOf(value["operationState"], "pending")
            IpcCommand.WIZARD_CANCEL_OPERATION ->
                shape(value, "operationId", "operationState") && uuid((value as JsonObject)["operationId"]) &&
                    oneOf(value["operationState"], "cancelling", "cancelled", "succeeded", "failed", "interrupted")
            IpcCommand.WIZARD_CANCEL_ATTEMPT ->
                shape(value, "attemptSequence", "attemptState") && uint64((value as JsonObject)["attemptSequence"], positive = true) != null &&
                    oneOf(value["attemptState"], "cancelling", "cancelled", "closed")
            IpcCommand.WIZARD_KEEPALIVE ->
                shape(value, "alive", "leaseDurationMs") && bool((value as JsonObject)["alive"]) == true &&
                    uint64(value["leaseDurationMs"]) == IpcTimeouts.WIZARD_LEASE_MS.toULong()
            IpcCommand.WIZARD_CLOSE -> shape(value, "closed") && bool((value as JsonObject)["closed"]) == true
            IpcCommand.GET_DEFAULT_CONFIG ->
                shape(value, "document") && string((value as JsonObject)["document"])?.let {
                    WireFormats.utf8Length(it) <= MAX_CONFIG_FILE_BYTES && check.validTemplate(it)
                } == true
            IpcCommand.GET_CONFIG_DOCUMENT ->
                shape(value, "configPath", "availability", "reasonCode", "document", "revisionToken") &&
                    snapshotMetadata(value as JsonObject) &&
                    (if (oneOf(value["availability"], "missing")) isNull(value["document"])
                    else documentMatches(value["document"], value["availability"], check))
            IpcCommand.CREATE_DEFAULT_CONFIG ->
                shape(value, "created", "configPath", "availability", "document", "revisionToken") &&
                    bool((value as JsonObject)["created"]) != null && absolutePath(value["configPath"]) &&
                    token(value["revisionToken"]) && documentMatches(value["document"], value["availability"], check) &&
                    (bool(value["created"]) == false || oneOf(value["availability"], "valid"))
            IpcCommand.GET_CONFIG_STATE ->
                shape(
                    value, "availability", "reasonCode", "configPath", "revisionToken", "serviceInstanceId",
                    "connectionEpoch", "vpnState", "vpnPhase",
                ) && snapshotMetadata(value as JsonObject) && uuid(value["serviceInstanceId"]) &&
                    uint64(value["connectionEpoch"]) != null && oneOf(value["vpnState"], *states) && oneOf(value["vpnPhase"], *phases)
            IpcCommand.WIZARD_GET_COMMIT -> {
                if (!shape(value, "commitState", "receipt", "error") ||
                    !oneOf((value as JsonObject)["commitState"], "pending", "confirmed", "failed", "unknown")
                ) return WizardReplyCheck.Rejected(malformed)
                when (string(value["commitState"])) {
                    "confirmed" -> receipt(value["receipt"]) && isNull(value["error"])
                    "failed" -> error(value["error"]) && isNull(value["receipt"])
                    else -> isNull(value["receipt"]) && isNull(value["error"])
                }
            }
            else -> false
        }
        return if (valid) WizardReplyCheck.Result(value) else WizardReplyCheck.Rejected(malformed)
    }
}

/** Synthesised Wizard errors, with the exact texts of `WizardIpc::safeError`. */
object WizardErrors {
    fun unsupported(): WizardError = WizardError(
        code = ErrorCode.WIZARD_UNSUPPORTED.wireName,
        description = "The PHPVPN service and tray must use the same updated wizard schema.",
        recoverable = false,
        field = null,
        httpStatus = null,
        suggestedAction = "Update both the PHPVPN service and tray to the same version, then open Configuration Wizard again.",
    )

    internal fun unsupportedJson(): JsonObject = IpcJson.encodeToJsonElement(WizardError.serializer(), unsupported()) as JsonObject

    /** A local failure (timeout, malformed reply) in the structured shape; the text is never shown. */
    fun local(code: ErrorCode): WizardError = WizardError(code.wireName, code.wireName, code.recoverable, null, null, "")
}

/** Absent is not null: the optional members must be absent rather than null. */
internal fun JsonObject.hasExplicitNull(name: String): Boolean = containsKey(name) && this[name] is JsonNull
