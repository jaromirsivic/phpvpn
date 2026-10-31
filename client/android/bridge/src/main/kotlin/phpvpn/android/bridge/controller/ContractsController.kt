package phpvpn.android.bridge.controller

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.IpcCommand
import phpvpn.android.bridge.ipc.IpcOutcome
import phpvpn.android.bridge.ipc.IpcTimeouts
import phpvpn.android.bridge.ipc.MonotonicClock

/** What the contracts screens (19 to 22, 41, 33, 46) render. */
data class ContractsUiState(
    val stage: ContractsStage = ContractsStage.CLOSED,
    val lastEvent: ContractsEvent = ContractsEvent.NONE,
    val checking: Boolean = false,
    val canRepeat: Boolean = false,
    /** The configuration cannot be opened: availability other than valid, or a failed read. */
    val unavailable: ConfigAvailability? = null,
    val failureCode: String? = null,
    val configPath: String? = null,
    /** Bumps on every change so the UI re-reads the native model. */
    val revision: Long = 0,
)

/**
 * Communication contracts over the native `ContractsSaveMachine` (WP04 JNI
 * handle): this class only runs the IPC side of `ContractsSaveController`
 * (apps/tray/contracts_save_controller.cpp). Opening uses one
 * get-config-document; Save sends the machine's payload with set-config and
 * classifies it natively; an unknown result is checked by get-config-document
 * whose raw reply the machine parses itself. All decisions stay in the machine.
 */
class ContractsController(
    private val client: IpcClient,
    private val machine: ContractsSaveMachineHandle,
    private val classifier: SaveResponseClassifier,
    private val clock: MonotonicClock = MonotonicClock.SYSTEM,
) {
    private val mutex = Mutex()
    private val _state = MutableStateFlow(ContractsUiState())
    val state: StateFlow<ContractsUiState> = _state.asStateFlow()
    private var checking = false

    val model: ContractsModelHandle? get() = machine.model

    suspend fun open() = mutex.withLock {
        _state.value = ContractsUiState(stage = ContractsStage.LOADING, revision = _state.value.revision + 1)
        when (val outcome = client.getConfigDocument()) {
            is IpcOutcome.Success -> {
                val result = outcome.value
                val document = result.document
                if (result.availability != ConfigAvailability.VALID || document == null) {
                    document?.wipe()
                    publish(unavailable = result.availability, configPath = result.configPath, stage = ContractsStage.CLOSED)
                    return@withLock
                }
                try {
                    val error = machine.load(document, result.configPath, result.revisionToken ?: "")
                    if (error != null) publish(failureCode = error.wireName, configPath = result.configPath, stage = ContractsStage.CLOSED)
                    else publish(configPath = result.configPath)
                } finally {
                    document.wipe()
                }
            }
            is IpcOutcome.ServiceError -> publish(failureCode = outcome.code, configPath = outcome.wizard?.configPath, stage = ContractsStage.CLOSED)
            IpcOutcome.TimedOut -> publish(failureCode = ErrorCode.TIMEOUT.wireName, stage = ContractsStage.CLOSED)
            is IpcOutcome.Rejected -> publish(failureCode = outcome.code.wireName, stage = ContractsStage.CLOSED)
        }
    }

    /** The UI changed the draft through [model]. */
    suspend fun changed() = mutex.withLock {
        machine.changed()
        publish()
    }

    /** Save (or the explicit repeat after 46 with [confirmedRepeat]). Returns the terminal event. */
    suspend fun save(confirmedRepeat: Boolean = false): ContractsEvent = mutex.withLock {
        machine.prepare(confirmedRepeat, checking)?.let {
            publish(failureCode = it.wireName)
            return@withLock ContractsEvent.NONE
        }
        val prepared = machine.dispatch(clock.nowMs())
        if (prepared == null) {
            publish()
            return@withLock ContractsEvent.NONE
        }
        publish()
        val exchange = try {
            client.setConfigRaw(prepared.document, IpcTimeouts.CONTRACTS_SAVE_DEADLINE_MS)
        } finally {
            prepared.document.wipe()
        }
        val result = try {
            classifier.classify(prepared.operation, exchange.requestId, exchange.response, exchange.writeAttempted, exchange.completedAtMs)
        } finally {
            exchange.response?.fill(0)
        }
        val event = machine.saveFinished(result)
        publish(event = event)
        event
    }

    /** 33 → 46: get-config-document parsed by the machine; never writes. */
    suspend fun checkSaveResult(): ContractsEvent = mutex.withLock {
        val generation = machine.check(checking) ?: return@withLock ContractsEvent.NONE
        checking = true
        publish()
        val exchange = client.rawPlain(IpcCommand.GET_CONFIG_DOCUMENT)
        val event = try {
            machine.checkFinished(generation, exchange.requestId, exchange.response)
        } finally {
            exchange.response?.fill(0)
            checking = false
        }
        publish(event = event)
        event
    }

    suspend fun close() = mutex.withLock {
        machine.close()
        _state.value = ContractsUiState(stage = ContractsStage.CLOSED, revision = _state.value.revision + 1)
    }

    fun lifecycleSnapshot(): ContractsLifecycleSnapshot = machine.snapshot(checking)

    private fun publish(
        event: ContractsEvent = _state.value.lastEvent,
        unavailable: ConfigAvailability? = null,
        failureCode: String? = null,
        configPath: String? = _state.value.configPath,
        stage: ContractsStage = machine.stage,
    ) {
        _state.value = ContractsUiState(
            stage = stage,
            lastEvent = event,
            checking = checking,
            canRepeat = machine.canRepeat(checking),
            unavailable = unavailable,
            failureCode = failureCode,
            configPath = configPath,
            revision = _state.value.revision + 1,
        )
    }
}
