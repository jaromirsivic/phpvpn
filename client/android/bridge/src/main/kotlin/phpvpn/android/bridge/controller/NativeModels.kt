package phpvpn.android.bridge.controller

import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.SecretText

/*
 * Kotlin faces of the portable native models that Android uses through JNI
 * instead of rewriting them (plan-android-v001, "Architektura"). WP04
 * implements these interfaces 1:1 over handle-based JNI calls in
 * client/apps/android-jni/jni_models.cpp; the shapes follow
 * client/apps/tray/save_response.h, client/apps/tray/contracts_save_machine.h and
 * client/src/core/presentation/contracts_model.h. A null [ErrorCode] result is
 * the C++ `Status` success. Secrets cross as [SecretText] (UTF-16, wiped by
 * the receiver after use) and never as log text.
 */

/** Local save identity, never part of the IPC schema (`tray::SaveOperation`). */
data class SaveOperation(
    val windowGeneration: Long,
    val draftGeneration: Long,
    val operationGeneration: Long,
    /** Absolute deadline on the caller's monotonic millisecond clock. */
    val deadlineMs: Long,
)

/** `tray::SaveOutcome`. */
enum class SaveOutcome { CONFIRMED, NOT_SENT, REJECTED, UNKNOWN }

/**
 * `tray::SaveResult`. [error] is the classified code ([ErrorCode.NONE] when
 * confirmed); [validationDescription] is set only for a curated HTTP header
 * validation message of `config_invalid_value` (screen 39 may show it).
 */
data class SaveResult(
    val operation: SaveOperation,
    val outcome: SaveOutcome,
    val error: ErrorCode,
    val validationDescription: String = "",
)

/**
 * `classifySaveResponse` (apps/tray/save_response.cpp), native in WP04. Only
 * the listed pre-publication errors prove a rejection; everything else that is
 * not a correlated acknowledgement stays [SaveOutcome.UNKNOWN].
 */
fun interface SaveResponseClassifier {
    fun classify(
        operation: SaveOperation,
        requestId: String,
        response: ByteArray?,
        writeAttempted: Boolean,
        completedAtMs: Long,
    ): SaveResult
}

/** `ContractColumn`. */
enum class ContractColumn { ENABLED, KEY, RANDOM, PASSWORD }

/** `ContractFocus`: the contract row by remote IPv4 and the column. */
data class ContractFocus(val remoteIp: String, val column: ContractColumn)

/**
 * One row of `ContractsModel::rows()`. [keyText] is a copy the caller wipes;
 * the native model keeps its own `SecureBuffer`.
 */
class ContractRowView(
    val remoteIp: String,
    val enabled: Boolean,
    val keyText: SecretText,
    val keyValid: Boolean,
    val dirty: Boolean,
    val keyErrorVisible: Boolean,
) {
    override fun toString(): String = "ContractRowView($remoteIp, enabled=$enabled, keyValid=$keyValid, dirty=$dirty)"
}

/**
 * `ContractTarget`: value captured when a child (password, random key
 * confirmation) opens; stale after the model's generation changes.
 */
interface ContractTargetHandle {
    val remoteIp: String
    val contractId: String
}

/** `ContractsModel` (core/presentation/contracts_model.h). Single owner, UI thread. */
interface ContractsModelHandle {
    fun rows(): List<ContractRowView>

    fun row(remoteIp: String): ContractRowView?

    val localIp: String
    val configPath: String

    /** Informational only. */
    val revisionToken: String
    val dirty: Boolean
    val blockingErrors: Int
    val firstBlockingFocus: ContractFocus?
    val keyTextBytes: Long
    val canPrepareSave: Boolean
    val frozen: Boolean
    val closed: Boolean
    val focus: ContractFocus?
    val saveGeneration: Long

    fun setEnabled(remoteIp: String, enabled: Boolean): ErrorCode?

    /** The model copies [key]; the caller wipes its own instance. */
    fun setKey(remoteIp: String, key: SecretText): ErrorCode?

    fun setFocus(focus: ContractFocus?): ErrorCode?

    fun target(remoteIp: String): ContractTargetHandle?

    /** Random key with the native crypto provider when [confirmed]. */
    fun confirmRandom(target: ContractTargetHandle, confirmed: Boolean): ErrorCode?

    /** Key from `PasswordKeyModel`; the caller wipes [key]. */
    fun applyPasswordKey(target: ContractTargetHandle, key: SecretText): ErrorCode?

    fun resumeEditing(saveGeneration: Long): Boolean
}

/** `ContractsStage`. */
enum class ContractsStage { LOADING, EDITING, INVALID, SAVING, SAVED, SAVE_UNKNOWN, CLOSED }

/** `ContractsEvent`. */
enum class ContractsEvent { NONE, SAVED, MATCHED, REJECTED, UNKNOWN, DIFFERENT, CHECK_FAILED }

/** `ContractsLifecycleSnapshot` for Exit and Back (screen 32). */
data class ContractsLifecycleSnapshot(
    val open: Boolean = false,
    val dirty: Boolean = false,
    val saving: Boolean = false,
    val unknown: Boolean = false,
    val checking: Boolean = false,
    val windowGeneration: Long = 0,
    val draftGeneration: Long = 0,
    val operationGeneration: Long = 0,
    val checkGeneration: Long = 0,
    val deadlineMs: Long = 0,
)

/** The `set-config` payload a successful `dispatch` hands to the sender; the sender wipes [document]. */
class PreparedSave(val operation: SaveOperation, val document: SecretText)

/**
 * `ContractsSaveMachine` (apps/tray/contracts_save_machine.h). The C++ sender
 * callbacks are split into return values so that no Kotlin lambda crosses
 * JNI: [dispatch] returns the payload the C++ `SaveSender` would receive (null
 * when the machine sends nothing), [check] returns the generation the
 * `CheckSender` would receive. The reply of the check is passed raw to
 * [checkFinished], which parses it natively with `WizardIpc::parseReply`
 * (null response = transport failure, `ipc_unavailable`).
 */
interface ContractsSaveMachineHandle : AutoCloseable {
    fun load(document: SecretText, configPath: String, revisionToken: String): ErrorCode?

    val model: ContractsModelHandle?
    val stage: ContractsStage
    val saving: Boolean

    fun snapshot(checking: Boolean): ContractsLifecycleSnapshot

    fun changed()

    fun prepare(confirmedRepeat: Boolean, checking: Boolean): ErrorCode?

    /** Deadline is [nowMs] plus kContractsSaveDeadlineMs inside the machine. */
    fun dispatch(nowMs: Long): PreparedSave?

    fun saveFinished(result: SaveResult): ContractsEvent

    fun check(checking: Boolean): Long?

    fun checkFinished(checkGeneration: Long, requestId: String, response: ByteArray?): ContractsEvent

    fun canRepeat(checking: Boolean): Boolean

    /** UTF-8 notice of the machine (English; the UI maps events to resources). */
    val notice: String

    override fun close()
}
