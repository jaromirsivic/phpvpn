package phpvpn.android.bridge.jni

import phpvpn.android.bridge.controller.ContractColumn
import phpvpn.android.bridge.controller.ContractFocus
import phpvpn.android.bridge.controller.ContractRowView
import phpvpn.android.bridge.controller.ContractTargetHandle
import phpvpn.android.bridge.controller.ContractsEvent
import phpvpn.android.bridge.controller.ContractsLifecycleSnapshot
import phpvpn.android.bridge.controller.ContractsModelHandle
import phpvpn.android.bridge.controller.ContractsSaveMachineHandle
import phpvpn.android.bridge.controller.ContractsStage
import phpvpn.android.bridge.controller.PreparedSave
import phpvpn.android.bridge.controller.SaveOperation
import phpvpn.android.bridge.controller.SaveOutcome
import phpvpn.android.bridge.controller.SaveResponseClassifier
import phpvpn.android.bridge.controller.SaveResult
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.SecretText

/** Access to a native ContractsModel source handle under the owner's lock. */
internal interface ContractsModelAccess {
    fun <T> withModel(block: (Long) -> T): T
}

/**
 * [ContractsModelHandle] over the native `ContractsModel`
 * (core/presentation/contracts_model.h) 1:1. Either standalone ([create],
 * close it when done) or the model of a [NativeContractsSaveMachine], which
 * owns it. Row keys are fresh [SecretText] copies the caller wipes.
 */
class NativeContractsModel internal constructor(private val access: ContractsModelAccess, private val owned: NativeResource?) :
    ContractsModelHandle,
    AutoCloseable {
    private class State(values: LongArray?) {
        val present = values != null
        val v = values ?: LongArray(14)
    }

    private fun state(): State = State(access.withModel { ModelNatives.contractsState(it) })

    private fun focusOf(present: Long, ip: Long, column: Long): ContractFocus? =
        if (present == 0L || column < 0) null else ContractFocus(Ipv4Text.toText(ip.toInt()), ContractColumn.entries[column.toInt()])

    override fun rows(): List<ContractRowView> = access.withModel { handle ->
        val addresses = ModelNatives.contractsRowAddresses(handle)
        val flags = ModelNatives.contractsRowFlags(handle)
        addresses.indices.map { index -> rowView(handle, addresses[index], flags[index]) }
    }

    override fun row(remoteIp: String): ContractRowView? {
        val ip = Ipv4Text.toInt(remoteIp) ?: return null
        return access.withModel { handle ->
            val addresses = ModelNatives.contractsRowAddresses(handle)
            val index = addresses.indexOf(ip)
            if (index < 0) null else rowView(handle, ip, ModelNatives.contractsRowFlags(handle)[index])
        }
    }

    private fun rowView(handle: Long, ip: Int, flags: Int): ContractRowView {
        val key = ModelNatives.contractsRowKey(handle, ip) ?: CharArray(0)
        return ContractRowView(
            remoteIp = Ipv4Text.toText(ip),
            enabled = flags and 1 != 0,
            keyText = SecretText.adopt(key),
            keyValid = flags and 2 != 0,
            dirty = flags and 4 != 0,
            keyErrorVisible = flags and 8 != 0,
        )
    }

    override val localIp: String get() = state().let { if (it.present) Ipv4Text.toText(it.v[13].toInt()) else "" }
    override val configPath: String get() = text(0)
    override val revisionToken: String get() = text(1)
    override val dirty: Boolean get() = state().v[0] != 0L
    override val blockingErrors: Int get() = state().v[1].toInt()
    override val firstBlockingFocus: ContractFocus? get() = state().v.let { focusOf(it[7], it[8], it[9]) }
    override val keyTextBytes: Long get() = state().v[2]
    override val canPrepareSave: Boolean get() = state().v[3] != 0L
    override val frozen: Boolean get() = state().v[4] != 0L
    override val closed: Boolean get() = state().let { !it.present || it.v[5] != 0L }
    override val focus: ContractFocus? get() = state().v.let { focusOf(it[10], it[11], it[12]) }
    override val saveGeneration: Long get() = state().v[6]

    private fun text(which: Int): String = access.withModel { ModelNatives.contractsText(it, which) }?.decodeToString() ?: ""

    override fun setEnabled(remoteIp: String, enabled: Boolean): ErrorCode? {
        val ip = Ipv4Text.toInt(remoteIp) ?: return ErrorCode.INVALID_ARGUMENT
        return nativeError(access.withModel { ModelNatives.contractsSetEnabled(it, ip, enabled) })
    }

    override fun setKey(remoteIp: String, key: SecretText): ErrorCode? {
        val ip = Ipv4Text.toInt(remoteIp) ?: return ErrorCode.INVALID_ARGUMENT
        return nativeError(access.withModel { handle -> key.read { chars -> ModelNatives.contractsSetKey(handle, ip, chars) } })
    }

    override fun setFocus(focus: ContractFocus?): ErrorCode? {
        if (focus == null) return nativeError(access.withModel { ModelNatives.contractsSetFocus(it, 0, -1) })
        val ip = Ipv4Text.toInt(focus.remoteIp) ?: return ErrorCode.INVALID_ARGUMENT
        return nativeError(access.withModel { ModelNatives.contractsSetFocus(it, ip, focus.column.ordinal) })
    }

    override fun target(remoteIp: String): ContractTargetHandle? {
        val ip = Ipv4Text.toInt(remoteIp) ?: return null
        return access.withModel { handle ->
            val id = ModelNatives.contractsTarget(handle, ip)
            if (id == 0L) {
                null
            } else {
                val contractId = ModelNatives.contractsTargetContractId(handle, id)?.decodeToString() ?: ""
                NativeContractTarget(this, id, Ipv4Text.toText(ip), contractId)
            }
        }
    }

    override fun confirmRandom(target: ContractTargetHandle, confirmed: Boolean): ErrorCode? {
        val native = ownTarget(target) ?: return ErrorCode.INVALID_ARGUMENT
        return nativeError(access.withModel { ModelNatives.contractsConfirmRandom(it, native.id, confirmed) })
    }

    override fun applyPasswordKey(target: ContractTargetHandle, key: SecretText): ErrorCode? {
        val native = ownTarget(target) ?: return ErrorCode.INVALID_ARGUMENT
        return nativeError(access.withModel { handle -> key.read { chars -> ModelNatives.contractsApplyPasswordKey(handle, native.id, chars) } })
    }

    override fun resumeEditing(saveGeneration: Long): Boolean = access.withModel { ModelNatives.contractsResumeEditing(it, saveGeneration) }

    // A target of another model is refused like a stale one (ContractsModel::accepts).
    private fun ownTarget(target: ContractTargetHandle): NativeContractTarget? = (target as? NativeContractTarget)?.takeIf { it.owner === this }

    /** Frees a standalone model (no-op for the model of a machine, which owns it). */
    override fun close() {
        owned?.close()
    }

    companion object {
        /** `ContractsModel::create`; throws [NativeModelException] with the refusal code. */
        fun create(document: SecretText, configPath: String = "", revisionToken: String = ""): NativeContractsModel {
            val error = arrayOfNulls<ByteArray>(1)
            val handle = document.read { chars -> ModelNatives.contractsCreate(chars, utf8(configPath), utf8(revisionToken), error) }
            if (handle == 0L) throw NativeModelException(nativeError(error[0]) ?: ErrorCode.INTERNAL)
            val resource = NativeResource(handle) { ModelNatives.contractsDestroy(it) }
            return NativeContractsModel(
                object : ContractsModelAccess {
                    override fun <T> withModel(block: (Long) -> T): T = resource.withHandle(block)
                },
                resource,
            )
        }
    }
}

/**
 * The C++ `ContractTarget` captured when a child opens, held natively by its
 * model (weak identity and generation); stale after the model changed.
 */
class NativeContractTarget internal constructor(
    internal val owner: NativeContractsModel,
    internal val id: Long,
    override val remoteIp: String,
    override val contractId: String,
) : ContractTargetHandle {
    override fun toString(): String = "ContractTarget($contractId)"
}

/**
 * [ContractsSaveMachineHandle] over `tray::ContractsSaveMachine`
 * (apps/tray/contracts_save_machine.h) 1:1, the same machine the Win32 and GTK
 * trays drive. [close] is the C++ `close()` (the model and its secrets are
 * wiped, the machine can load again); [release] frees the native object.
 */
class NativeContractsSaveMachine : ContractsSaveMachineHandle {
    private val resource = NativeResource(ModelNatives.machineCreate()) { ModelNatives.machineDestroy(it) }
    private val modelView = NativeContractsModel(
        object : ContractsModelAccess {
            override fun <T> withModel(block: (Long) -> T): T = resource.withHandle { block(ModelNatives.machineModel(it)) }
        },
        null,
    )

    override fun load(document: SecretText, configPath: String, revisionToken: String): ErrorCode? = nativeError(
        resource.withHandle { handle -> document.read { chars -> ModelNatives.machineLoad(handle, chars, utf8(configPath), utf8(revisionToken)) } },
    )

    override val model: ContractsModelHandle? get() = if (resource.withHandle { ModelNatives.machineHasModel(it) }) modelView else null

    override val stage: ContractsStage get() = ContractsStage.entries[resource.withHandle { ModelNatives.machineStage(it) }]

    override val saving: Boolean get() = resource.withHandle { ModelNatives.machineSaving(it) }

    override fun snapshot(checking: Boolean): ContractsLifecycleSnapshot {
        val v = resource.withHandle { ModelNatives.machineSnapshot(it, checking) } ?: return ContractsLifecycleSnapshot()
        return ContractsLifecycleSnapshot(v[0] != 0L, v[1] != 0L, v[2] != 0L, v[3] != 0L, v[4] != 0L, v[5], v[6], v[7], v[8], v[9])
    }

    override fun changed() = resource.withHandle { ModelNatives.machineChanged(it) }

    override fun prepare(confirmedRepeat: Boolean, checking: Boolean): ErrorCode? =
        nativeError(resource.withHandle { ModelNatives.machinePrepare(it, confirmedRepeat, checking) })

    override fun dispatch(nowMs: Long): PreparedSave? {
        val operation = LongArray(4)
        val payload = resource.withHandle { ModelNatives.machineDispatch(it, nowMs, operation) } ?: return null
        return PreparedSave(SaveOperation(operation[0], operation[1], operation[2], operation[3]), SecretText.adopt(payload))
    }

    override fun saveFinished(result: SaveResult): ContractsEvent {
        val operation = result.operation.toArray()
        val event = resource.withHandle {
            ModelNatives.machineSaveFinished(it, operation, result.outcome.ordinal, utf8(result.error.wireName), utf8(result.validationDescription))
        }
        return ContractsEvent.entries[event]
    }

    override fun check(checking: Boolean): Long? = resource.withHandle { ModelNatives.machineCheck(it, checking) }.takeIf { it >= 0 }

    override fun checkFinished(checkGeneration: Long, requestId: String, response: ByteArray?): ContractsEvent =
        ContractsEvent.entries[resource.withHandle { ModelNatives.machineCheckFinished(it, checkGeneration, utf8(requestId), response) }]

    override fun canRepeat(checking: Boolean): Boolean = resource.withHandle { ModelNatives.machineCanRepeat(it, checking) }

    override val notice: String get() = resource.withHandle { ModelNatives.machineNotice(it) }.decodeToString()

    override fun close() {
        if (!resource.isClosed) resource.withHandle { ModelNatives.machineClose(it) }
    }

    /** Closes the machine and frees the native object; the handle is unusable afterwards. */
    fun release() {
        close()
        resource.close()
    }
}

internal fun SaveOperation.toArray(): LongArray = longArrayOf(windowGeneration, draftGeneration, operationGeneration, deadlineMs)

/** `tray::classifySaveResponse` (apps/tray/save_response.cpp), the native classifier of set-config replies. */
object NativeSaveResponseClassifier : SaveResponseClassifier {
    override fun classify(
        operation: SaveOperation,
        requestId: String,
        response: ByteArray?,
        writeAttempted: Boolean,
        completedAtMs: Long,
    ): SaveResult {
        val parts = ModelNatives.classifySave(operation.toArray(), utf8(requestId), response, writeAttempted, completedAtMs)
            ?: return SaveResult(operation, SaveOutcome.UNKNOWN, ErrorCode.RESPONSE_INVALID)
        val outcome = SaveOutcome.entries.getOrElse(parts[0].decodeToString().toIntOrNull() ?: -1) { SaveOutcome.UNKNOWN }
        val error = ErrorCode.fromWire(parts[1].decodeToString()) ?: ErrorCode.RESPONSE_INVALID
        return SaveResult(operation, outcome, error, parts[2].decodeToString())
    }
}
