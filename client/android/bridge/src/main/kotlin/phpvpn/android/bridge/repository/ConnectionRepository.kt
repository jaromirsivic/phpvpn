package phpvpn.android.bridge.repository

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeoutOrNull
import phpvpn.android.bridge.ipc.HttpDiagnostic
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.IpcOutcome
import phpvpn.android.bridge.ipc.IpcTimeouts
import phpvpn.android.bridge.ipc.MonotonicClock
import phpvpn.android.bridge.model.ProgressModel
import phpvpn.android.bridge.model.ProgressView
import phpvpn.android.bridge.model.TrayStatus

/** What the Connection destination shows (screens 01 to 08). */
sealed interface ConnectionStatus {
    /** Before the first read: nothing is claimed yet. */
    data object Loading : ConnectionStatus

    /** A confirmed, complete get-status snapshot. */
    data class Available(val status: TrayStatus) : ConnectionStatus

    /**
     * get-status did not answer within 400 ms or answered invalidly (screen 08).
     * Neither connection nor disconnection is confirmed; [lastKnown] is only a hint.
     */
    data class Unavailable(val lastKnown: TrayStatus?) : ConnectionStatus
}

/** Own Connect or Disconnect in progress (screens 04, 06, 07, 38). */
data class OperationProgress(
    val disconnect: Boolean,
    val view: ProgressView,
    val httpDiagnostic: HttpDiagnostic?,
    val connectionEpoch: ULong,
    val connectAttemptId: String?,
)

/** Reply of an own command. A timeout is [Unconfirmed], never success. */
sealed interface CommandResult {
    data class Accepted(val status: TrayStatus) : CommandResult

    data class Failed(val error: IpcOutcome.ServiceError) : CommandResult

    data object Unconfirmed : CommandResult
}

/**
 * Status source of the UI and the notification.
 *
 * Polls `get-status` (400 ms budget) only while [status] is collected: every
 * 500 ms while an own Connect/Disconnect progress is open or the status is a
 * transition (connecting, recovering, reconnecting, stopping), otherwise every
 * 5 s. A command reply and [refresh] trigger an immediate extra read; neither
 * restarts a second loop. No timer runs without a collector.
 */
class ConnectionRepository(
    private val client: IpcClient,
    scope: CoroutineScope,
    private val clock: MonotonicClock = MonotonicClock.SYSTEM,
) {
    private val mutex = Mutex()
    private val current = MutableStateFlow<ConnectionStatus>(ConnectionStatus.Loading)
    private val refreshRequests = Channel<Unit>(Channel.CONFLATED)
    private var lastKnown: TrayStatus? = null
    private var progressModel: ProgressModel? = null
    private val _progress = MutableStateFlow<OperationProgress?>(null)

    val progress: StateFlow<OperationProgress?> = _progress.asStateFlow()

    val status: StateFlow<ConnectionStatus> = flow {
        coroutineScope {
            launch { pollLoop() }
            current.collect { emit(it) }
        }
    }.stateIn(scope, SharingStarted.WhileSubscribed(), ConnectionStatus.Loading)

    /** The current value without starting the polling (for the service). */
    val latest: ConnectionStatus get() = current.value

    /** Extra read, for example "Check status again" on screen 08. */
    fun refresh() {
        refreshRequests.trySend(Unit)
    }

    /** One read with the status budget; also usable without a collector. */
    suspend fun readOnce(): ConnectionStatus {
        val outcome = client.getStatus(IpcTimeouts.STATUS_REQUEST_MS)
        return mutex.withLock {
            val next = (outcome as? IpcOutcome.Success)?.let { TrayStatus.from(it.value) }
            apply(next ?: TrayStatus())
        }
    }

    suspend fun connect(): CommandResult {
        mutex.withLock { startProgress(disconnect = false) }
        val outcome = client.connect()
        val result = handleCommand(outcome, disconnect = false)
        refresh()
        return result
    }

    suspend fun disconnect(): CommandResult {
        mutex.withLock { startProgress(disconnect = true) }
        val outcome = client.disconnect()
        val result = handleCommand(outcome, disconnect = true)
        refresh()
        return result
    }

    /** Explicit invalidation of the Connect error detail (error panel closed). */
    suspend fun dismissDiagnostic(connectionEpoch: ULong, connectAttemptId: String): IpcOutcome<Unit> {
        mutex.withLock {
            progressModel?.let { model ->
                if (model.connectionEpoch == connectionEpoch && model.connectAttemptId == connectAttemptId) {
                    model.onClose()
                    publishProgress()
                }
            }
        }
        return client.dismissConnectDiagnostic(connectionEpoch, connectAttemptId)
    }

    /** The success heading was drawn; the progress closes 3 s later. */
    suspend fun progressSuccessPresented() = mutex.withLock {
        progressModel?.onSuccessPresented(clock.nowMs())
        publishProgress()
    }

    suspend fun progressTick() = mutex.withLock {
        progressModel?.onTick(clock.nowMs())
        publishProgress()
    }

    /** Local close of the progress presentation; no command is sent. */
    suspend fun closeProgress() = mutex.withLock {
        progressModel?.onClose()
        progressModel = null
        _progress.value = null
    }

    private suspend fun handleCommand(outcome: IpcOutcome<phpvpn.android.bridge.ipc.StatusSnapshot>, disconnect: Boolean): CommandResult =
        mutex.withLock {
            when (outcome) {
                is IpcOutcome.Success -> {
                    val status = TrayStatus.from(outcome.value)
                    apply(status)
                    CommandResult.Accepted(status)
                }
                is IpcOutcome.ServiceError -> {
                    if (disconnect) progressModel?.onCommandFailure() else progressModel?.onSnapshot(TrayStatus(), clock.nowMs())
                    publishProgress()
                    CommandResult.Failed(outcome)
                }
                else -> {
                    if (disconnect) progressModel?.onCommandFailure() else progressModel?.onSnapshot(TrayStatus(), clock.nowMs())
                    publishProgress()
                    CommandResult.Unconfirmed
                }
            }
        }

    private fun startProgress(disconnect: Boolean) {
        progressModel = ProgressModel(disconnect)
        publishProgress()
    }

    private fun apply(status: TrayStatus): ConnectionStatus {
        progressModel?.onSnapshot(status, clock.nowMs())
        publishProgress()
        val next = if (status.valid) {
            lastKnown = status
            ConnectionStatus.Available(status)
        } else {
            ConnectionStatus.Unavailable(lastKnown)
        }
        current.value = next
        return next
    }

    private fun publishProgress() {
        val model = progressModel
        if (model == null) return
        _progress.value = OperationProgress(model.isDisconnect, model.view, model.httpDiagnostic, model.connectionEpoch, model.connectAttemptId)
        // The last published view carries closeRequested; the model is retired.
        if (model.view.closeRequested) progressModel = null
    }

    private fun interval(): Long {
        val open = progressModel?.view?.closeRequested == false
        val transitional = lastKnown?.transitional == true
        return if (open || transitional) IpcTimeouts.PROGRESS_INTERVAL_MS else IpcTimeouts.STATUS_INTERVAL_MS
    }

    private suspend fun pollLoop() {
        while (true) {
            readOnce()
            val wait = mutex.withLock { interval() }
            withTimeoutOrNull(wait) { refreshRequests.receive() }
        }
    }
}
