package phpvpn.android.standin

import kotlinx.serialization.SerializationException
import phpvpn.android.bridge.controller.ContractsEvent
import phpvpn.android.bridge.controller.ContractsLifecycleSnapshot
import phpvpn.android.bridge.controller.ContractsModelHandle
import phpvpn.android.bridge.controller.ContractsSaveMachineHandle
import phpvpn.android.bridge.controller.ContractsStage
import phpvpn.android.bridge.controller.PreparedSave
import phpvpn.android.bridge.controller.SaveOperation
import phpvpn.android.bridge.controller.SaveOutcome
import phpvpn.android.bridge.controller.SaveResult
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.ConfigDocumentResult
import phpvpn.android.bridge.ipc.IpcCommand
import phpvpn.android.bridge.ipc.IpcJson
import phpvpn.android.bridge.ipc.IpcTimeouts
import phpvpn.android.bridge.ipc.JsonChecks
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.ipc.StrictJson
import phpvpn.android.bridge.ipc.StructuralConfigDocumentCheck
import phpvpn.android.bridge.ipc.WireValidation
import phpvpn.android.bridge.ipc.WizardReplyCheck

/**
 * Kotlin stand-in of `ContractsSaveMachine` (apps/tray/contracts_save_machine.cpp)
 * over [StandInContractsModel]: stages, generations, the single prepared
 * candidate, the 30 s deadline and the check of an unknown result. The
 * English [notice] mirrors the native one for the ported tests; error
 * descriptions are reduced to the wire name because the UI never shows the
 * notice (it maps [ContractsEvent] and codes to string resources).
 */
class StandInContractsSaveMachine(
    private val sizer: ContractsEnvelopeSizer = StandInEnvelopeSizer,
) : ContractsSaveMachineHandle {
    private var contracts: StandInContractsModel? = null
    private var currentStage = ContractsStage.CLOSED
    private var windowGeneration = 0L
    private var draftGeneration = 0L
    private var operationGeneration = 0L
    private var checkGeneration = 0L
    private var operation: SaveOperation? = null
    private var repeatAllowed = false
    private var prepared = false
    private var currentNotice = ""

    /** The concrete model (the screens use only [model]). */
    val standInModel: StandInContractsModel? get() = contracts

    override val model: ContractsModelHandle? get() = contracts

    override val stage: ContractsStage
        get() = if (currentStage == ContractsStage.EDITING && (contracts?.blockingErrors ?: 0) > 0) ContractsStage.INVALID else currentStage

    override val saving: Boolean get() = currentStage == ContractsStage.SAVING

    override val notice: String get() = currentNotice

    override fun load(document: SecretText, configPath: String, revisionToken: String): ErrorCode? {
        if (contracts != null) return ErrorCode.BUSY
        currentStage = ContractsStage.LOADING
        val created = document.read { StandInContractsModel.create(String(it), configPath, revisionToken) }
        if (created is StandInResult.Failed) {
            currentStage = ContractsStage.CLOSED
            return created.code
        }
        contracts = (created as StandInResult.Ok).value
        windowGeneration++
        draftGeneration++
        currentStage = ContractsStage.EDITING
        currentNotice = ""
        repeatAllowed = false
        prepared = false
        return null
    }

    override fun snapshot(checking: Boolean): ContractsLifecycleSnapshot = ContractsLifecycleSnapshot(
        open = contracts != null,
        dirty = (contracts?.dirty ?: false) && currentStage != ContractsStage.SAVED,
        saving = currentStage == ContractsStage.SAVING,
        unknown = currentStage == ContractsStage.SAVE_UNKNOWN,
        checking = checking,
        windowGeneration = windowGeneration,
        draftGeneration = draftGeneration,
        operationGeneration = operationGeneration,
        checkGeneration = checkGeneration,
        deadlineMs = operation?.deadlineMs ?: 0,
    )

    override fun changed() {
        if (currentStage == ContractsStage.EDITING) {
            draftGeneration++
            currentNotice = ""
        }
    }

    override fun prepare(confirmedRepeat: Boolean, checking: Boolean): ErrorCode? {
        val model = contracts ?: return ErrorCode.BUSY
        if (prepared) return ErrorCode.BUSY
        if (confirmedRepeat) {
            if (!canRepeat(checking) || !model.resumeEditing(model.saveGeneration)) return ErrorCode.BUSY
            currentStage = ContractsStage.EDITING
        }
        if (currentStage != ContractsStage.EDITING) return ErrorCode.BUSY
        val candidate = model.prepareSave(sizer)
        if (candidate is StandInResult.Failed) {
            currentNotice = candidate.code.wireName
            return candidate.code
        }
        prepared = true
        repeatAllowed = false
        return null
    }

    override fun dispatch(nowMs: Long): PreparedSave? {
        val model = contracts ?: return null
        val candidate = model.candidate
        if (!prepared || candidate == null) return null
        prepared = false
        val next = SaveOperation(windowGeneration, draftGeneration, ++operationGeneration, nowMs + IpcTimeouts.CONTRACTS_SAVE_DEADLINE_MS)
        operation = next
        currentStage = ContractsStage.SAVING
        currentNotice = "Saving configuration..."
        return PreparedSave(next, candidate.text())
    }

    // A request that could not be sent comes back through saveFinished as
    // NOT_SENT (writeAttempted = false), which resumes editing like the C++
    // sender returning false.

    override fun saveFinished(result: SaveResult): ContractsEvent {
        val model = contracts ?: return ContractsEvent.NONE
        if (currentStage != ContractsStage.SAVING || operation == null || result.operation != operation) return ContractsEvent.NONE
        operation = null
        return when (result.outcome) {
            SaveOutcome.CONFIRMED -> {
                currentStage = ContractsStage.SAVED
                currentNotice = "Changes saved. Checking connection status..."
                ContractsEvent.SAVED
            }
            SaveOutcome.UNKNOWN -> {
                currentStage = ContractsStage.SAVE_UNKNOWN
                currentNotice = UNKNOWN_TEXT
                ContractsEvent.UNKNOWN
            }
            SaveOutcome.REJECTED, SaveOutcome.NOT_SENT -> {
                model.resumeEditing(model.saveGeneration)
                currentStage = ContractsStage.EDITING
                currentNotice = result.error.wireName
                ContractsEvent.REJECTED
            }
        }
    }

    override fun check(checking: Boolean): Long? {
        if (contracts == null || currentStage != ContractsStage.SAVE_UNKNOWN || checking) return null
        val generation = ++checkGeneration
        repeatAllowed = false
        currentNotice = "Checking current configuration..."
        return generation
    }

    override fun checkFinished(checkGeneration: Long, requestId: String, response: ByteArray?): ContractsEvent {
        val model = contracts ?: return ContractsEvent.NONE
        if (currentStage != ContractsStage.SAVE_UNKNOWN || checkGeneration != this.checkGeneration) return ContractsEvent.NONE
        this.checkGeneration++
        repeatAllowed = false
        val reply = parseConfigDocument(requestId, response)
        val result = when (reply) {
            is ParsedReply.Failed -> {
                currentNotice = reply.code
                return ContractsEvent.CHECK_FAILED
            }
            is ParsedReply.Document -> reply.result
        }
        try {
            when (result.availability) {
                ConfigAvailability.VALID -> {
                    val document = result.document
                    val candidate = model.candidate ?: return ContractsEvent.CHECK_FAILED
                    val match = document?.read { candidate.matchesDocument(String(it)) } ?: StandInResult.Failed(ErrorCode.RESPONSE_INVALID)
                    if (match is StandInResult.Failed) {
                        currentNotice = match.code.wireName
                        return ContractsEvent.CHECK_FAILED
                    }
                    if ((match as StandInResult.Ok).value) {
                        currentNotice = "The current configuration matches your changes."
                        return ContractsEvent.MATCHED
                    }
                }
                ConfigAvailability.MISSING -> Unit
                else -> {
                    currentNotice = "The current configuration is ${result.availability.wireName}. Resolve this condition before saving again.\n" +
                        result.configPath
                    return ContractsEvent.CHECK_FAILED
                }
            }
        } finally {
            result.wipe()
        }
        repeatAllowed = true
        currentNotice = "The current configuration differs. Your draft has been kept."
        return ContractsEvent.DIFFERENT
    }

    override fun canRepeat(checking: Boolean): Boolean = currentStage == ContractsStage.SAVE_UNKNOWN && repeatAllowed && !checking

    override fun close() {
        operation = null
        contracts?.close()
        contracts = null
        windowGeneration++
        checkGeneration++
        currentStage = ContractsStage.CLOSED
        prepared = false
        repeatAllowed = false
        currentNotice = ""
    }

    private sealed interface ParsedReply {
        class Document(val result: ConfigDocumentResult) : ParsedReply

        class Failed(val code: String) : ParsedReply
    }

    /** `WizardIpc::parseReply` for get-config-document, then the typed result. */
    private fun parseConfigDocument(requestId: String, response: ByteArray?): ParsedReply {
        if (response == null) return ParsedReply.Failed(ErrorCode.IPC_UNAVAILABLE.wireName)
        val root = StrictJson.parse(response) ?: return ParsedReply.Failed(ErrorCode.IPC_MALFORMED.wireName)
        return when (val check = WireValidation.wizardReply(IpcCommand.GET_CONFIG_DOCUMENT, requestId, root, StructuralConfigDocumentCheck)) {
            is WizardReplyCheck.Result -> try {
                ParsedReply.Document(IpcJson.decodeFromJsonElement(ConfigDocumentResult.serializer(), check.value))
            } catch (_: SerializationException) {
                ParsedReply.Failed(ErrorCode.IPC_MALFORMED.wireName)
            } catch (_: IllegalArgumentException) {
                ParsedReply.Failed(ErrorCode.IPC_MALFORMED.wireName)
            }
            is WizardReplyCheck.Error -> ParsedReply.Failed(JsonChecks.string(check.error["code"]) ?: ErrorCode.RESPONSE_INVALID.wireName)
            is WizardReplyCheck.Rejected -> ParsedReply.Failed(check.code.wireName)
        }
    }

    override fun toString(): String = "StandInContractsSaveMachine(stage=$currentStage)"

    private companion object {
        const val UNKNOWN_TEXT =
            "The save result is unknown. The file may already have changed. Check the current configuration before saving again."
    }
}
