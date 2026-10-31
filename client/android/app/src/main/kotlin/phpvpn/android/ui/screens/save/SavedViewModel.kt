package phpvpn.android.ui.screens.save

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.IpcTimeouts
import phpvpn.android.bridge.ipc.MonotonicClock
import phpvpn.android.bridge.model.ReconnectController
import phpvpn.android.bridge.model.TrayStatus
import phpvpn.android.bridge.repository.CommandResult
import phpvpn.android.bridge.repository.ConnectionRepository
import phpvpn.android.bridge.repository.ConnectionStatus

/** What screen 25 offers after a confirmed save. */
enum class SavedConnection {
    /** Reading the status once (no configuration read). */
    CHECKING,

    /** Connected: Reconnect (disconnect, then connect) or use on the next connection. */
    ACTIVE,

    /** Not connected: Connect or not now. */
    INACTIVE,

    /** In a transition (connecting, recovering, stopping): only the next connection. */
    BUSY,

    /** The status could not be read: reconnect manually. */
    UNAVAILABLE,
}

/** Progress of the Reconnect choice. */
enum class ReconnectPhase { IDLE, DISCONNECTING, FAILED }

/**
 * Screen 25 "Changes saved" (tray_application.cpp `requestReconnectAfterSave`
 * with [ReconnectController]): one get-status after the confirmed save; a
 * connected VPN offers Reconnect, where the Disconnect must complete (same
 * connection, idle, no error, within 30 s) before the single Connect; an
 * inactive VPN offers Connect. Leaving the screen cancels a pending sequence.
 */
class SavedViewModel(
    private val connection: ConnectionRepository,
    private val clock: MonotonicClock = MonotonicClock.SYSTEM,
) : ViewModel() {
    private val reconnect = ReconnectController()

    var connectionState by mutableStateOf(SavedConnection.CHECKING)
        private set

    var phase by mutableStateOf(ReconnectPhase.IDLE)
        private set

    /** Set when the flow is complete and the app should show the Connection destination. */
    var finished by mutableStateOf(false)
        private set

    init {
        viewModelScope.launch { readStatus() }
    }

    private suspend fun readStatus() {
        if (!reconnect.begin()) return
        val status = (connection.readOnce() as? ConnectionStatus.Available)?.status
        connectionState = when {
            status == null -> SavedConnection.UNAVAILABLE
            status.state == ConnectionState.CONNECTED && reconnect.confirm(status) -> SavedConnection.ACTIVE
            status.state == ConnectionState.DISCONNECTED || status.state == ConnectionState.ERROR -> SavedConnection.INACTIVE
            else -> SavedConnection.BUSY
        }
        if (connectionState != SavedConnection.ACTIVE) reconnect.cancel()
    }

    /** Reconnect: the user's consent, the same connection again, Disconnect, wait for idle, then Connect. */
    fun reconnect() {
        if (connectionState != SavedConnection.ACTIVE || phase == ReconnectPhase.DISCONNECTING) return
        phase = ReconnectPhase.DISCONNECTING
        viewModelScope.launch {
            phase = if (runReconnect()) ReconnectPhase.IDLE else ReconnectPhase.FAILED
            if (phase == ReconnectPhase.IDLE) finished = true
        }
    }

    private suspend fun runReconnect(): Boolean {
        val token = reconnect.token
        reconnect.consent()
        val current = (connection.readOnce() as? ConnectionStatus.Available)?.status
        if (current == null || !reconnect.revalidate(current)) return fail()
        val deadline = clock.nowMs() + DISCONNECT_DEADLINE_MS
        var status: TrayStatus = when (val result = connection.disconnect()) {
            is CommandResult.Accepted -> result.status
            else -> return fail()
        }
        while (true) {
            if (reconnect.observeDisconnect(status, clock.nowMs(), deadline)) break
            if (!reconnect.current(token)) return fail()
            delay(IpcTimeouts.PROGRESS_INTERVAL_MS)
            // An unanswered read proves nothing; only the deadline ends the wait then.
            var next = (connection.readOnce() as? ConnectionStatus.Available)?.status
            while (next == null) {
                if (clock.nowMs() >= deadline) return fail()
                delay(IpcTimeouts.PROGRESS_INTERVAL_MS)
                next = (connection.readOnce() as? ConnectionStatus.Available)?.status
            }
            status = next
        }
        // The queued Connect runs only if the status is still the confirmed idle of this connection.
        val idle = (connection.readOnce() as? ConnectionStatus.Available)?.status ?: return fail()
        if (!reconnect.consume(token, idle)) return fail()
        return connection.connect() !is CommandResult.Failed
    }

    private fun fail(): Boolean {
        reconnect.cancel()
        return false
    }

    /** Connect while the VPN is not active. */
    fun connect() {
        if (connectionState != SavedConnection.INACTIVE) return
        viewModelScope.launch {
            connection.connect()
            finished = true
        }
    }

    override fun onCleared() {
        reconnect.cancel()
    }

    companion object {
        /** `disconnectDeadline_` of the tray: 30 s for the Disconnect to complete. */
        const val DISCONNECT_DEADLINE_MS = 30_000L
    }
}
