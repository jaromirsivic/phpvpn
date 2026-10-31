package phpvpn.android.service

import android.content.Context
import android.net.VpnService
import android.util.Log
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.withTimeoutOrNull
import phpvpn.android.bridge.ipc.IpcTimeouts
import phpvpn.android.bridge.repository.CommandResult
import phpvpn.android.bridge.repository.ConnectionRepository
import phpvpn.android.ui.state.AlwaysOnState
import phpvpn.android.ui.state.AlwaysOnStatus
import phpvpn.android.ui.state.ConnectActions

/**
 * Process-wide link between the UI and [PhpvpnVpnService]: the replies of
 * the commands the UI sent through an intent (a ticket per command) and
 * whether a service instance lives. Everything runs in one process (plan
 * point 8), so the reply needs no binder.
 */
object VpnServiceCommands {
    private val nextTicket = AtomicLong(1)
    private val pending = ConcurrentHashMap<Long, CompletableDeferred<CommandResult>>()
    private val _running = MutableStateFlow(false)

    /** True between onCreate and onDestroy of the service. */
    val running: StateFlow<Boolean> = _running.asStateFlow()

    /** No ticket: a command of the system (Always-on, boot, R9) or of the notification. */
    const val NO_TICKET = 0L

    fun open(): Pair<Long, CompletableDeferred<CommandResult>> {
        val ticket = nextTicket.getAndIncrement()
        val reply = CompletableDeferred<CommandResult>()
        pending[ticket] = reply
        return ticket to reply
    }

    fun complete(ticket: Long, result: CommandResult) {
        if (ticket != NO_TICKET) pending.remove(ticket)?.complete(result)
    }

    fun cancel(ticket: Long) {
        pending.remove(ticket)?.cancel()
    }

    internal fun serviceRunning(running: Boolean) {
        _running.value = running
    }
}

/**
 * The [ConnectActions] of the app (plan step 1): every Connect starts the
 * foreground service with ACTION_CONNECT, which registers its TunnelHost
 * before it sends `connect`; the UI never calls the repository's Connect.
 * Disconnect goes to the running service (ACTION_DISCONNECT, as from the
 * notification); without a service it is the repository's Disconnect, which
 * also tells the truth when no VPN is up. Results keep the repository meaning:
 * no reply in time is `Unconfirmed`, never success.
 */
class ServiceConnectActions(
    private val context: Context,
    private val connection: ConnectionRepository,
) : ConnectActions {
    override suspend fun connect(): CommandResult {
        val (ticket, reply) = VpnServiceCommands.open()
        // The foreground type systemExempted requires the VPN consent (app op
        // activate_vpn); a Connect without it (25 after "Forget VPN") runs the
        // service in the background and ends in tunnel_permission_missing (37).
        // prepare() belongs to Connect anyway (life cycle step 1).
        val consent = VpnService.prepare(context) == null
        try {
            val intent = PhpvpnVpnService.connectIntent(context, ticket, foreground = consent)
            if (consent) context.startForegroundService(intent) else context.startService(intent)
        } catch (error: IllegalStateException) {
            // ForegroundServiceStartNotAllowedException: the app is not in the
            // foreground and has no exemption. Nothing was sent.
            VpnServiceCommands.cancel(ticket)
            Log.w(TAG, "the VPN service could not be started for Connect")
            return CommandResult.Unconfirmed
        }
        return await(ticket, reply)
    }

    override suspend fun disconnect(): CommandResult {
        if (!VpnServiceCommands.running.value) return connection.disconnect()
        val (ticket, reply) = VpnServiceCommands.open()
        try {
            context.startService(PhpvpnVpnService.disconnectIntent(context, ticket))
        } catch (error: IllegalStateException) {
            VpnServiceCommands.cancel(ticket)
            return connection.disconnect()
        }
        return await(ticket, reply)
    }

    /** Exit (35, step 8): the service stops after the confirmed disconnected state. */
    override suspend fun awaitServiceStopped(timeoutMs: Long) {
        withTimeoutOrNull(timeoutMs) { VpnServiceCommands.running.first { !it } }
        // The stop itself is asynchronous; give stopForeground a frame.
        delay(STOP_SETTLE_MS)
    }

    private suspend fun await(ticket: Long, reply: CompletableDeferred<CommandResult>): CommandResult =
        withTimeoutOrNull(IpcTimeouts.COMMAND_MS + SERVICE_START_MARGIN_MS) { reply.await() } ?: run {
            VpnServiceCommands.cancel(ticket)
            CommandResult.Unconfirmed
        }

    private companion object {
        const val TAG = "phpvpn"

        /** The service start before the 30 s command budget begins. */
        const val SERVICE_START_MARGIN_MS = 5_000L
        const val STOP_SETTLE_MS = 50L
    }
}

/** `isAlwaysOn()` and `isLockdownEnabled()` as the service last saw them (plan step 6). */
object ServiceAlwaysOnStatus : AlwaysOnStatus {
    private val _state = MutableStateFlow<AlwaysOnState?>(null)
    override val state: StateFlow<AlwaysOnState?> = _state.asStateFlow()

    internal fun publish(state: AlwaysOnState) {
        _state.value = state
    }
}
