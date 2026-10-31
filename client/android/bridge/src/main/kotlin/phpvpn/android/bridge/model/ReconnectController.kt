package phpvpn.android.bridge.model

import phpvpn.android.bridge.ipc.ConnectionPhase
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.Tristate

/**
 * Port of `tray::ReconnectController` (apps/tray/reconnect_controller.h), the
 * policy of screen 25 "Changes saved": a token belongs to one confirmed save,
 * Disconnect must complete (same connection identity, idle, no error) before
 * the queued Connect, and any foreign transition cancels the sequence.
 */
class ReconnectController {
    enum class Phase { NONE, READING_SAVED_STATUS, CONFIRMING, REVALIDATING, DISCONNECTING, CONNECT_QUEUED }

    var phase: Phase = Phase.NONE
        private set
    var token: Long = 0
        private set
    private var identity = TrayStatus()

    fun current(token: Long): Boolean = phase != Phase.NONE && token == this.token

    fun begin(): Boolean {
        if (phase != Phase.NONE) return false
        token++
        phase = Phase.READING_SAVED_STATUS
        return true
    }

    fun cancel() {
        token++
        phase = Phase.NONE
        identity = TrayStatus()
    }

    fun confirm(status: TrayStatus): Boolean {
        if (phase != Phase.READING_SAVED_STATUS || !status.valid || status.state != ConnectionState.CONNECTED ||
            status.connectionEpoch == 0uL || status.connectAttemptId == null || status.connectionStartedAtUtcMs == null
        ) return false
        identity = status
        phase = Phase.CONFIRMING
        return true
    }

    fun consent() {
        if (phase == Phase.CONFIRMING) phase = Phase.REVALIDATING
    }

    fun revalidate(status: TrayStatus): Boolean {
        if (phase != Phase.REVALIDATING || !sameConnection(status) || status.state != ConnectionState.CONNECTED ||
            status.connectionStartedAtUtcMs != identity.connectionStartedAtUtcMs
        ) return false
        phase = Phase.DISCONNECTING
        return true
    }

    fun idle(status: TrayStatus): Boolean =
        sameConnection(status) && status.state == ConnectionState.DISCONNECTED && status.phase == ConnectionPhase.IDLE &&
            status.tunnelReady == Tristate.NO && !status.hasError && status.connectionStartedAtUtcMs == null

    /** Only the Disconnect reply and its own subsequent reads may advance this. */
    fun observeDisconnect(status: TrayStatus, now: Long = 0, deadline: Long = Long.MAX_VALUE): Boolean {
        if (phase != Phase.DISCONNECTING) return false
        if (now >= deadline || !sameConnection(status) || status.hasError || status.state == ConnectionState.ERROR ||
            (status.state != ConnectionState.DISCONNECTED && status.connectionStartedAtUtcMs != identity.connectionStartedAtUtcMs)
        ) {
            cancel()
            return false
        }
        if (idle(status)) {
            phase = Phase.CONNECT_QUEUED
            return true
        }
        if (status.state == ConnectionState.DISCONNECTED ||
            (status.state != ConnectionState.CONNECTED && status.phase != ConnectionPhase.STOPPING)
        ) cancel()
        return false
    }

    fun consume(token: Long, status: TrayStatus): Boolean {
        if (!current(token) || phase != Phase.CONNECT_QUEUED) return false
        val allowed = idle(status)
        cancel()
        return allowed
    }

    private fun sameConnection(status: TrayStatus): Boolean =
        status.valid && status.connectionEpoch == identity.connectionEpoch && status.connectAttemptId == identity.connectAttemptId
}
