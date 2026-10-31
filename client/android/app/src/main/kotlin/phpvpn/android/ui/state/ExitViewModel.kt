package phpvpn.android.ui.state

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import phpvpn.android.bridge.ipc.IpcTimeouts
import phpvpn.android.bridge.ipc.Tristate
import phpvpn.android.bridge.model.ProgressStage
import phpvpn.android.bridge.model.TrayStatus
import phpvpn.android.bridge.repository.ConnectionRepository
import phpvpn.android.bridge.repository.ConnectionStatus
import phpvpn.android.bridge.repository.OperationProgress

/** One-shot results of the Exit dialog (35). */
sealed interface ExitEffect {
    /** "Close window, stay connected": `finish()`; the service and the VPN keep running. */
    data object CloseWindow : ExitEffect

    /** Disconnect confirmed: `finishAndRemoveTask()`. The process is never killed. */
    data object CloseTask : ExitEffect

    /** Not confirmed: the dialog closes and Connection shows 07 or 38, never a fake success. */
    data object ShowConnection : ExitEffect
}

/**
 * Exit dialog (35, plan step 8). "Disconnect and close" sends one Disconnect
 * (through [ConnectActions], which WP05 wraps to stop the service) and closes
 * the task only after the confirmed disconnected state.
 */
class ExitViewModel(
    private val connection: ConnectionRepository,
    private val actions: ConnectActions,
) : ViewModel() {
    private val _disconnecting = MutableStateFlow(false)
    val disconnecting: StateFlow<Boolean> = _disconnecting.asStateFlow()
    private val effectChannel = Channel<ExitEffect>(Channel.BUFFERED)
    val effects: Flow<ExitEffect> = effectChannel.receiveAsFlow()

    fun closeWindow() {
        effectChannel.trySend(ExitEffect.CloseWindow)
    }

    fun disconnectAndClose() {
        if (_disconnecting.value) return
        _disconnecting.value = true
        viewModelScope.launch {
            val alreadyOffline = (connection.latest as? ConnectionStatus.Available)?.status?.let(::confirmedOffline) == true
            if (!alreadyOffline) actions.disconnect()
            // Collecting the status keeps the 500 ms polling of the open
            // Disconnect progress running until the outcome is known.
            val outcome = if (alreadyOffline) Outcome.CONFIRMED else withTimeoutOrNull(IpcTimeouts.COMMAND_MS.toLong()) {
                combine(connection.status, connection.progress, ::classify).first { it != Outcome.PENDING }
            } ?: Outcome.UNCONFIRMED
            _disconnecting.value = false
            effectChannel.trySend(if (outcome == Outcome.CONFIRMED) ExitEffect.CloseTask else ExitEffect.ShowConnection)
        }
    }

    private enum class Outcome { PENDING, CONFIRMED, UNCONFIRMED }

    private fun classify(status: ConnectionStatus, progress: OperationProgress?): Outcome {
        val view = progress?.takeIf { it.disconnect && !it.view.closeRequested }?.view
        if (view != null && (view.stage == ProgressStage.FAILED || view.errorCode.isNotEmpty())) return Outcome.UNCONFIRMED
        if (view?.stage == ProgressStage.SUCCEEDED) return Outcome.CONFIRMED
        val current = (status as? ConnectionStatus.Available)?.status
        return if (view == null && current != null && confirmedOffline(current)) Outcome.CONFIRMED else Outcome.PENDING
    }

    /** The success condition of the disconnect progress (progress_model.cpp). */
    private fun confirmedOffline(status: TrayStatus) = status.offline && status.tunnelReady == Tristate.NO && !status.hasError
}
