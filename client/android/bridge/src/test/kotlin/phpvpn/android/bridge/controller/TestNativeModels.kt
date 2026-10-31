package phpvpn.android.bridge.controller

import kotlinx.serialization.json.JsonObject
import phpvpn.android.bridge.Fixtures
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.IPC_MAX_MESSAGE_BYTES
import phpvpn.android.bridge.ipc.JsonChecks
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.ipc.StrictJson
import phpvpn.android.bridge.ipc.WireFormats

/**
 * Test-only reference of `classifySaveResponse` (apps/tray/save_response.cpp).
 * Production uses the native classifier through JNI (WP04); this port lets the
 * JVM tests run the editor and contracts flows with the same semantics. The
 * canonical descriptions are read from error.cpp.
 */
object ReferenceSaveResponseClassifier : SaveResponseClassifier {
    private val descriptions: Map<String, String> by lazy {
        Regex("""\{ErrorCode::\w+,\s*"([^"]+)",\s*"((?:[^"\\]|\\.)*)",""").findAll(Fixtures.source("src/core/base/error.cpp"))
            .associate { it.groupValues[1] to it.groupValues[2] }
    }
    private val rejecting = listOf(
        ErrorCode.BUSY, ErrorCode.IPC_MALFORMED, ErrorCode.IPC_MESSAGE_TOO_LARGE, ErrorCode.IPC_UNSUPPORTED_VERSION,
        ErrorCode.IPC_UNKNOWN_COMMAND, ErrorCode.CONFIG_ENCODING_ERROR, ErrorCode.CONFIG_TOO_LARGE, ErrorCode.CONFIG_INVALID_JSON,
        ErrorCode.CONFIG_INVALID_VALUE, ErrorCode.CONFIG_INVALID_URL, ErrorCode.CONFIG_INVALID_CIDR, ErrorCode.CONFIG_INVALID_ADDRESS,
        ErrorCode.CONFIG_INVALID_KEY, ErrorCode.CONFIG_INVALID_LIMIT, ErrorCode.CONFIG_QUEUE_BUDGET, ErrorCode.WIZARD_CONTRACT_SELF_CONFLICT,
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
        root["result"]?.let {
            return if (JsonChecks.string(it) == "ok") SaveResult(operation, SaveOutcome.CONFIRMED, ErrorCode.NONE) else unknown
        }
        val error = root["error"] as? JsonObject ?: return unknown
        if (error.size != 3) return unknown
        val code = JsonChecks.string(error["code"]) ?: return unknown
        val description = JsonChecks.string(error["description"]) ?: return unknown
        val recoverable = JsonChecks.bool(error["recoverable"]) ?: return unknown
        for (candidate in rejecting) {
            val header = candidate == ErrorCode.CONFIG_INVALID_VALUE && description.startsWith("masterSetup.httpHeaders")
            if (code == candidate.wireName && (description == descriptions[candidate.wireName] || header) && recoverable == candidate.recoverable) {
                return SaveResult(operation, SaveOutcome.REJECTED, candidate, if (header) description else "")
            }
        }
        return unknown
    }
}

/** Scriptable fake of the native ContractsSaveMachine handle. */
class FakeContractsSaveMachine : ContractsSaveMachineHandle {
    var loaded: String? = null
    var loadError: ErrorCode? = null
    var prepareError: ErrorCode? = null
    var nextEvent: (SaveResult) -> ContractsEvent = { result ->
        when (result.outcome) {
            SaveOutcome.CONFIRMED -> ContractsEvent.SAVED
            SaveOutcome.REJECTED, SaveOutcome.NOT_SENT -> ContractsEvent.REJECTED
            SaveOutcome.UNKNOWN -> ContractsEvent.UNKNOWN
        }
    }
    var checkEvent: (ByteArray?) -> ContractsEvent = { ContractsEvent.MATCHED }
    val saveResults = mutableListOf<SaveResult>()
    val checkedResponses = mutableListOf<String?>()
    var closed = false
    private var generation = 0L
    private var currentStage = ContractsStage.CLOSED
    var payload = "{\"masterSetup\":{},\"communicationContracts\":{}}"

    override fun load(document: SecretText, configPath: String, revisionToken: String): ErrorCode? {
        loadError?.let { return it }
        loaded = document.reveal()
        currentStage = ContractsStage.EDITING
        return null
    }

    override val model: ContractsModelHandle? get() = null
    override val stage: ContractsStage get() = currentStage
    override val saving: Boolean get() = currentStage == ContractsStage.SAVING

    override fun snapshot(checking: Boolean) = ContractsLifecycleSnapshot(open = currentStage != ContractsStage.CLOSED, checking = checking)

    override fun changed() = Unit

    override fun prepare(confirmedRepeat: Boolean, checking: Boolean): ErrorCode? = prepareError

    override fun dispatch(nowMs: Long): PreparedSave? {
        if (currentStage != ContractsStage.EDITING && currentStage != ContractsStage.SAVE_UNKNOWN) return null
        currentStage = ContractsStage.SAVING
        return PreparedSave(SaveOperation(1, 1, ++generation, nowMs + 30_000), SecretText.of(payload))
    }

    override fun saveFinished(result: SaveResult): ContractsEvent {
        saveResults += result
        val event = nextEvent(result)
        currentStage = when (event) {
            ContractsEvent.SAVED -> ContractsStage.SAVED
            ContractsEvent.UNKNOWN -> ContractsStage.SAVE_UNKNOWN
            else -> ContractsStage.EDITING
        }
        return event
    }

    override fun check(checking: Boolean): Long? = if (currentStage == ContractsStage.SAVE_UNKNOWN && !checking) ++generation else null

    override fun checkFinished(checkGeneration: Long, requestId: String, response: ByteArray?): ContractsEvent {
        checkedResponses += response?.toString(Charsets.UTF_8)
        val event = checkEvent(response)
        if (event == ContractsEvent.MATCHED) currentStage = ContractsStage.SAVED
        return event
    }

    override fun canRepeat(checking: Boolean): Boolean = currentStage == ContractsStage.SAVE_UNKNOWN && !checking

    override val notice: String get() = ""

    override fun close() {
        closed = true
        currentStage = ContractsStage.CLOSED
    }
}
