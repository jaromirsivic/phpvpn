package phpvpn.android.debug

import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.bridge.ipc.FakeIpcTransport.Reply
import phpvpn.android.bridge.ipc.HttpHeaders
import phpvpn.android.bridge.ipc.IpcJson
import phpvpn.android.bridge.ipc.JsonChecks

/**
 * Debug and test only: a scripted in-memory stand-in for the Wizard part of
 * the service, answering IPC v1 over a [FakeIpcTransport] with replies that
 * pass the strict client validation. Tests set the outcome of each check,
 * of Finish and of the receipt, the VPN state for the gate and the session
 * errors, then drive the real UI or ViewModel. Values here are synthetic.
 * Shared by the JVM tests (debug variant) and the instrumented tests.
 */
class FakeWizardService(val transport: FakeIpcTransport = FakeIpcTransport()) {
    /** Outcome of one check kind (url, secret, access). */
    sealed interface Check {
        data object Succeed : Check

        /** Never finishes: the attempt runs into its budget (timeout). */
        data object Pending : Check

        /** Finishes with another connection epoch: a foreign Connect (11). */
        data object Interrupt : Check

        data class Fail(
            val code: String,
            val field: String?,
            val httpStatus: Int? = null,
            val diagnostic: Boolean = false,
            val description: String = "synthetic failure",
        ) : Check
    }

    /** Outcome of Finish. */
    sealed interface Finish {
        data object Confirmed : Finish

        data class Failed(val code: String) : Finish

        /** The wizard-finish reply is lost: the result is unknown (33). */
        data object ReplyLost : Finish
    }

    class Fields(
        var serverUrl: String = PLACEHOLDER_URL,
        var serverSecret: String = DEFAULT_SECRET,
        var virtualIp: String = "",
        var accessKey: String = "",
        var httpHeaders: String = HttpHeaders.DEFAULT,
        var useDefaultSecret: Boolean = false,
        var autoDetectIp: Boolean = true,
        var useDefaultKey: Boolean = true,
    )

    var serviceInstanceId = "00000000-0000-4000-8000-0000000000a1"
    var sessionId = "00000000-0000-4000-8000-0000000000b1"
    var mode = "create"
    var availability = "missing"
    var fields: Fields? = Fields()
    var vpnCidr = "10.99.99.0/24"
    var contractCount = 0
    var existingPeers: List<String> = emptyList()
    var responseTimeoutSeconds = 30
    var vpnState = "disconnected"
    var vpnPhase = "idle"
    var epoch = 0uL

    /** After this many get-status reads during a disconnect, the VPN is idle. */
    var disconnectReads = 0

    val checks = mutableMapOf<String, Check>("url" to Check.Succeed, "secret" to Check.Succeed, "access" to Check.Succeed)
    var suggestedIp: String? = "10.99.99.10"
    var discardedRecords = 0
    var finish: Finish = Finish.Confirmed

    /** State that wizard-get-commit reports: confirmed, failed, unknown or expired (service restart). */
    var commitState = "confirmed"
    var keepaliveExpired = false
    var openFails = false

    /** A Connect diagnostic in get-status (30/31 with the Connect source). */
    var connectDiagnostic = false

    private val operations = HashMap<String, Operation>()
    private var idCounter = 0x100
    private var commitId: String? = null
    private var pendingDisconnect = -1

    private class Operation(val id: String, val kind: String, val attemptId: String?, val sequence: ULong?)

    init {
        install()
    }

    fun requests(command: String) = transport.requests(command)

    private fun install() {
        transport.respond("wizard-open") { r ->
            if (openFails) text(r, errorEnvelope(r, "config_unreadable", null)) else text(r, result(r, view()))
        }
        transport.respond("wizard-resume") { r -> text(r, result(r, view())) }
        transport.respond("wizard-reset") { r ->
            mode = "reset"
            fields = Fields()
            contractCount = 0
            existingPeers = emptyList()
            text(r, result(r, view(resetView = true)))
        }
        transport.respond("wizard-keepalive") { r ->
            if (keepaliveExpired) text(r, errorEnvelope(r, "wizard_session_expired", null))
            else text(r, result(r, buildJsonObject { put("alive", true); put("leaseDurationMs", 60_000) }))
        }
        transport.respond("wizard-close") { r -> text(r, result(r, buildJsonObject { put("closed", true) })) }
        transport.respond("wizard-cancel-attempt") { r ->
            val sequence = JsonChecks.uint64(r.json["attemptSequence"]) ?: 1uL
            text(r, result(r, buildJsonObject { put("attemptSequence", sequence.toLong()); put("attemptState", "cancelled") }))
        }
        for (kind in listOf("url", "secret", "access")) {
            transport.respond("wizard-check-$kind") { r -> started(r, kind) }
        }
        transport.respond("wizard-finish") { r ->
            commitId = JsonChecks.string(r.json["commitId"])
            if (finish == Finish.ReplyLost) Reply.Timeout else started(r, "finish")
        }
        transport.respond("wizard-get-operation") { r ->
            val operation = operations[JsonChecks.string(r.json["operationId"])] ?: return@respond text(r, errorEnvelope(r, "invalid_argument", null))
            text(r, result(r, operationResult(operation)))
        }
        transport.respond("wizard-get-commit") { r ->
            when (commitState) {
                "expired" -> text(r, errorEnvelope(r, "wizard_session_expired", null))
                "confirmed" -> text(r, result(r, buildJsonObject {
                    put("commitState", "confirmed")
                    put("receipt", receipt())
                    put("error", JsonNull)
                }))
                "failed" -> text(r, result(r, buildJsonObject {
                    put("commitState", "failed")
                    put("receipt", JsonNull)
                    put("error", wizardError("config_write_failed", null, null))
                }))
                else -> text(r, result(r, buildJsonObject {
                    put("commitState", commitState)
                    put("receipt", JsonNull)
                    put("error", JsonNull)
                }))
            }
        }
        transport.respond("get-config-state") { r ->
            text(r, result(r, buildJsonObject {
                put("availability", "valid")
                put("reasonCode", JsonNull)
                put("configPath", CONFIG_PATH)
                put("revisionToken", "revision-after-restart")
                put("serviceInstanceId", "00000000-0000-4000-8000-0000000000a2")
                put("connectionEpoch", epoch.toLong())
                put("vpnState", vpnState)
                put("vpnPhase", vpnPhase)
            }))
        }
        transport.respond("get-status") { r ->
            if (pendingDisconnect > 0) {
                pendingDisconnect--
            } else if (pendingDisconnect == 0) {
                vpnState = "disconnected"
                vpnPhase = "idle"
                pendingDisconnect = -1
            }
            text(r, statusEnvelope(r))
        }
        transport.respond("disconnect") { r ->
            if (disconnectReads > 0) {
                vpnPhase = "stopping"
                pendingDisconnect = disconnectReads
            } else {
                vpnState = "disconnected"
                vpnPhase = "idle"
            }
            text(r, statusEnvelope(r))
        }
        transport.respond("dismiss-connect-diagnostic") { r ->
            connectDiagnostic = false
            text(r, buildJsonObject { put("ipcVersion", 1); put("requestId", r.requestId); put("result", "ok") })
        }
    }

    private fun started(r: FakeIpcTransport.Recorded, kind: String): Reply {
        val id = nextId()
        operations[id] = Operation(
            id,
            kind,
            JsonChecks.string(r.json["clientAttemptId"]),
            JsonChecks.uint64(r.json["attemptSequence"]),
        )
        return text(r, result(r, buildJsonObject { put("operationId", id); put("operationState", "pending") }))
    }

    private fun operationResult(operation: Operation): JsonObject {
        val finishKind = operation.kind == "finish"
        var state = "succeeded"
        var error: JsonElement = JsonNull
        var payload: JsonElement = JsonNull
        var diagnostic: JsonObject? = null
        var operationEpoch = epoch
        if (finishKind) {
            when (val outcome = finish) {
                Finish.Confirmed, Finish.ReplyLost -> payload = receipt()
                is Finish.Failed -> {
                    state = "failed"
                    error = wizardError(outcome.code, null, null)
                }
            }
        } else {
            when (val outcome = checks.getValue(operation.kind)) {
                Check.Succeed -> payload = when (operation.kind) {
                    "url" -> buildJsonObject { put("verified", true) }
                    "secret" -> buildJsonObject { put("verified", true); put("suggestedIp", suggestedIp?.let { JsonPrimitive(it) } ?: JsonNull) }
                    else -> buildJsonObject { put("verified", true); put("discardedRecordCount", discardedRecords) }
                }
                Check.Pending -> state = "running"
                Check.Interrupt -> {
                    // A foreign Connect was accepted meanwhile: new epoch, VPN running.
                    state = "interrupted"
                    epoch += 1uL
                    vpnState = "connected"
                    vpnPhase = "running"
                    operationEpoch = epoch
                    checks[operation.kind] = Check.Succeed
                }
                is Check.Fail -> {
                    state = "failed"
                    error = wizardError(outcome.code, outcome.field, outcome.httpStatus, outcome.description)
                    if (outcome.diagnostic) diagnostic = httpDiagnostic()
                }
            }
        }
        return buildJsonObject {
            put("operationId", operation.id)
            put("kind", operation.kind)
            put("operationState", state)
            put("connectionEpoch", operationEpoch.toLong())
            put("clientAttemptId", operation.attemptId?.let { JsonPrimitive(it) } ?: JsonNull)
            put("attemptSequence", operation.sequence?.let { JsonPrimitive(it.toLong()) } ?: JsonNull)
            put("error", error)
            put("payload", payload)
            if (diagnostic != null) put("httpDiagnostic", diagnostic)
        }
    }

    private fun receipt(): JsonObject = buildJsonObject {
        put("commitId", commitId ?: "00000000-0000-4000-8000-0000000000c1")
        put("revisionToken", "opaque-revision")
        put("commitState", "confirmed")
    }

    private fun view(resetView: Boolean = false): JsonObject {
        val f = fields
        val readable = availability == "valid" || availability == "repairable" || availability == "invalid"
        return buildJsonObject {
            put("sessionId", sessionId)
            put("serviceInstanceId", serviceInstanceId)
            put("revisionToken", if (readable) JsonPrimitive("revision-1") else JsonNull)
            put("availability", availability)
            put("mode", if (resetView) "reset" else mode)
            if (f == null) {
                put("fields", JsonNull)
                put("context", JsonNull)
            } else {
                put("fields", buildJsonObject {
                    put("serverUrl", f.serverUrl)
                    put("serverSecret", f.serverSecret)
                    put("virtualIp", f.virtualIp)
                    put("accessKey", f.accessKey)
                    put("httpHeaders", f.httpHeaders)
                    put("useDefaultServerSecretInWizard", f.useDefaultSecret)
                    put("autoDetectIpAddressInWizard", f.autoDetectIp)
                    put("useDefaultAccessKeyInWizard", f.useDefaultKey)
                })
                put("context", context())
            }
            put("contractCount", if (f == null) 0 else contractCount)
            put("existingDefaultContractPeers", JsonArray(if (f == null) emptyList() else existingPeers.map { JsonPrimitive(it) }))
            put("connectionEpoch", epoch.toLong())
            put("vpnState", vpnState)
            put("vpnPhase", vpnPhase)
        }
    }

    private fun context(): JsonObject = buildJsonObject {
        put("httpHeaders", HttpHeaders.DEFAULT)
        put("vpnCidr", vpnCidr)
        put("trustAnyTlsCertificate", true)
        put("maxOutboundQueueBytes", 134_217_728)
        put("maxInboundQueueBytes", 134_217_728)
        put("minOutboundPerPeerQueueBytes", 262_144)
        put("minInboundPerPeerQueueBytes", 262_144)
        put("maxQueuedPacketAgeMs", 5_000)
        put("maxResponseBodyBytes", 67_108_864)
        put("maxNdjsonLineBytes", 67_108_864)
        put("maxDataRecordsPerResponse", 65_535)
        put("responseTimeoutSeconds", responseTimeoutSeconds)
        put("timeSyncIntervalSeconds", 300)
    }

    private fun wizardError(code: String, field: String?, httpStatus: Int?, description: String = "synthetic failure"): JsonObject =
        buildJsonObject {
            put("code", code)
            put("description", description)
            put("recoverable", false)
            put("field", field?.let { JsonPrimitive(it) } ?: JsonNull)
            put("httpStatus", httpStatus?.let { JsonPrimitive(it) } ?: JsonNull)
            put("suggestedAction", "synthetic action")
        }

    /** A small synthetic exchange: the request is reconstructed, the response is HTML instead of JSON. */
    fun httpDiagnostic(): JsonObject = buildJsonObject {
        val request = "POST /phpvpn.php?action=health HTTP/?\r\nCache-Control: no-cache\r\n\r\n{}"
        val response = "HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/html\r\n\r\n<html>synthetic\u0001</html>"
        put("format", "http-octets-v1")
        put("action", "health")
        put("responseComplete", false)
        put("requestHeadersSource", "application")
        put("responseHeadersSource", "available")
        put("request", message(request))
        put("response", message(response))
    }

    private fun message(text: String): JsonObject = buildJsonObject {
        put("originalSizeBytes", text.length)
        put("prefix", text)
        put("separator", "")
        put("suffix", "")
    }

    private fun statusEnvelope(r: FakeIpcTransport.Recorded): JsonObject {
        val connected = vpnState == "connected"
        val diagnostic = connectDiagnostic && vpnState == "error"
        return buildJsonObject {
            put("ipcVersion", 1)
            put("requestId", r.requestId)
            put("status", buildJsonObject {
                put("connectionEpoch", epoch.toLong())
                put("connectAttemptId", if (epoch == 0uL) JsonNull else JsonPrimitive(CONNECT_ATTEMPT))
                if (diagnostic) put("httpDiagnostic", httpDiagnostic())
                put("state", vpnState)
                put("phase", vpnPhase)
                put("stateChangedAt", "2026-10-31T08:00:00.000Z")
                put("connectionStartedAt", if (connected || vpnState == "recovering") JsonPrimitive("2026-10-31T08:00:00.000Z") else JsonNull)
                put("serverUrlCheck", if (connected) "success" else if (diagnostic) "failed" else "pending")
                put("serverSecretCheck", if (connected) "success" else "pending")
                put("accessKeyCheck", if (connected) "success" else "pending")
                put("communicationContractsCheck", if (connected) "success" else "pending")
                put("serverReachable", if (connected) "yes" else "unknown")
                put("serverVerified", if (connected) "yes" else "unknown")
                put("tunnelReady", if (connected) "yes" else "no")
                put("lastError", if (diagnostic) buildJsonObject {
                    put("code", "endpoint_unavailable_or_invalid")
                    put("description", "synthetic")
                    put("recoverable", false)
                } else JsonNull)
                for (name in listOf(
                    "dataPollsTotal", "dataPollsEmpty", "dataPollsWithPayload", "serializedDataRequestBodyBytes",
                    "serializedDataResponseBodyBytes", "ipv4PacketsSent", "ipv4PacketsReceived", "ipv4BytesSent", "ipv4BytesReceived",
                )) put(name, 0)
                put("recoveryCount", 0)
                put("clientVersion", "1.0.3")
                put("ipcVersion", 1)
            })
        }
    }

    private fun result(r: FakeIpcTransport.Recorded, value: JsonElement): JsonObject = buildJsonObject {
        put("ipcVersion", 1)
        put("requestId", r.requestId)
        put("result", value)
    }

    private fun errorEnvelope(r: FakeIpcTransport.Recorded, code: String, field: String?): JsonObject = buildJsonObject {
        put("ipcVersion", 1)
        put("requestId", r.requestId)
        put("error", wizardError(code, field, null))
    }

    private fun text(r: FakeIpcTransport.Recorded, value: JsonElement): Reply =
        Reply.Text(IpcJson.encodeToString(JsonElement.serializer(), value).replace("\$ID", r.requestId))

    private fun nextId(): String = "00000000-0000-4000-8000-%012x".format(idCounter++)

    companion object {
        const val PLACEHOLDER_URL = "http://MODIFY-THIS-URL/phpvpn.php"
        const val DEFAULT_SECRET = "CHANGE_THIS_SERVER_SECRET"
        const val CONFIG_PATH = "/data/user/0/io.github.jaromirsivic.phpvpn/no_backup/phpvpn/config/config.json"
        const val CONNECT_ATTEMPT = "00000000-0000-4000-8000-0000000000d1"
    }
}
