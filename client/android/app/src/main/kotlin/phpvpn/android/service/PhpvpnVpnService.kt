package phpvpn.android.service

import android.app.Notification
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.VpnService
import android.os.SystemClock
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import phpvpn.android.AppContainer
import phpvpn.android.appContainer
import phpvpn.android.bridge.NativeBridge
import phpvpn.android.bridge.NativeRuntime
import phpvpn.android.bridge.TunnelHost
import phpvpn.android.bridge.VpnHosts
import phpvpn.android.bridge.VpnInterfaces
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.IpcOutcome
import phpvpn.android.bridge.model.StatusModel
import phpvpn.android.bridge.model.TrayStatus
import phpvpn.android.bridge.repository.CommandResult
import phpvpn.android.ui.state.AlwaysOnState
import phpvpn.android.ui.state.ConfigSummaryState
import phpvpn.android.ui.state.DisconnectReason

/**
 * The VPN service of the app (plan-android-v001, step 5 and "Životní cyklus
 * připojení na Androidu"): the [TunnelHost] of the native driver, the
 * foreground service with the notification (09) for every state other than a
 * confirmed idle one, and the entry of every Connect (UI, boot, Always-on,
 * R9 restart).
 *
 * State machine (one main-thread owner, see the WP05 report):
 * - created: the TunnelHost is registered before anything else, so no
 *   Connect can reach loadAndVerify without it (WP04 finding 1);
 * - ACTION_CONNECT (UI, boot): foreground "Connecting", `wantConnected`,
 *   `connect` through the repository; the reply goes back to the UI ticket;
 * - `android.net.VpnService` (Always-on): Connect when get-config-state is valid;
 * - empty intent (START_STICKY after the process died, R9): Connect only with
 *   the flag, the consent and a valid configuration, three times per 10 min;
 *   otherwise stop without ever entering the foreground and say why on 02;
 * - ACTION_DISCONNECT (notification, UI), onRevoke: `disconnect`;
 * - every status read decides: keep (and rewrite the notification on a
 *   change) or, on a confirmed `disconnected` or final `error`, remove the
 *   notification and stop. A timeout never stops it.
 */
class PhpvpnVpnService : VpnService(), TunnelHost {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val kick = Channel<Unit>(Channel.CONFLATED)
    private lateinit var intents: IntentStore
    private lateinit var notifications: VpnNotifications

    // Main thread only.
    private var operation: LocalOperation? = null
    private var followed: LocalOperation? = null

    // Changes whenever an own command starts or ends: a status read that
    // overlapped it may predate the command and must not decide (it would
    // stop a service whose Connect was just accepted).
    private var commandEpoch = 0
    private var followDeadline = 0L
    private var foreground = false
    private var stopping = false
    private var pollJob: Job? = null
    private var lastStatus: TrayStatus? = null
    private var lastKey: NotificationSchedule.Key? = null
    private var lastAlwaysOnState: Any? = null
    private var notificationText: String? = null
    private var lastStartId = 0

    @Volatile
    private var lastEstablishError = TunnelHost.ESTABLISH_OK

    private val container: AppContainer get() = appContainer

    override fun onCreate() {
        super.onCreate()
        // Before anything else (point 18): a Connect never runs without the host.
        VpnHosts.registerTunnelHost(this)
        NativeRuntime.ensureStarted(this)
        VpnServiceCommands.serviceRunning(true)
        intents = IntentStore.get(this)
        notifications = VpnNotifications(this)
        notifications.createChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        lastStartId = startId
        publishAlwaysOn()
        // A new command cancels a stop that has not happened yet (stopSelf(startId) ignores it then).
        if (intent?.action == ACTION_CONNECT || intent?.action == ACTION_DISCONNECT || intent?.action == SERVICE_INTERFACE) stopping = false
        when (intent?.action) {
            ACTION_CONNECT -> {
                val ticket = intent.getLongExtra(EXTRA_TICKET, VpnServiceCommands.NO_TICKET)
                // startForegroundService: the foreground must start at once. Without the
                // consent (the Connect of 25 after "Forget VPN") Android refuses the
                // systemExempted type, so the sender used startService and the engine
                // ends in tunnel_permission_missing (37) without a foreground phase.
                if (!intent.getBooleanExtra(EXTRA_FOREGROUND, true) || enterForeground(LocalOperation.CONNECT)) {
                    connect(ticket, userRequest = true)
                } else {
                    VpnServiceCommands.complete(ticket, CommandResult.Unconfirmed)
                    stopService()
                }
            }
            ACTION_DISCONNECT -> disconnect(intent.getLongExtra(EXTRA_TICKET, VpnServiceCommands.NO_TICKET))
            SERVICE_INTERFACE -> alwaysOn()
            null -> stickyRestart()
            else -> if (operation == null && !foreground) stopService()
        }
        return START_STICKY
    }

    // --- Entries --------------------------------------------------------------

    private fun connect(ticket: Long, userRequest: Boolean) {
        operation = LocalOperation.CONNECT
        commandEpoch++
        follow(LocalOperation.CONNECT)
        // R9 (a): the flag of an accepted Connect; a user's Connect starts a new restart budget.
        intents.update { record ->
            record.copy(wantConnected = true, restartsElapsedMs = if (userRequest) emptyList() else record.restartsElapsedMs)
        }
        container.connectionEvents.clear()
        ensurePolling()
        scope.launch {
            refreshNotificationText()
            kick.trySend(Unit)
        }
        scope.launch {
            val result = container.connection.connect()
            VpnServiceCommands.complete(ticket, result)
            if (result is CommandResult.Failed) Log.i(TAG, "Connect refused: ${result.error.code}")
            operation = null
            commandEpoch++
            refreshNotificationText()
            kick.trySend(Unit)
        }
    }

    private fun disconnect(ticket: Long) {
        operation = LocalOperation.DISCONNECT
        commandEpoch++
        follow(LocalOperation.DISCONNECT)
        // The user's intent is known at the request; a kill before the
        // confirmation must not reconnect (R9 clears it again when confirmed).
        intents.update { it.copy(wantConnected = false, restartsElapsedMs = emptyList()) }
        ensurePolling()
        scope.launch {
            val result = container.connection.disconnect()
            VpnServiceCommands.complete(ticket, result)
            operation = null
            commandEpoch++
            kick.trySend(Unit)
        }
    }

    /** Step 6: started by Android for Always-on; Connect only with a valid configuration. */
    private fun alwaysOn() {
        if (!enterForeground(LocalOperation.CONNECT)) return stopService()
        operation = LocalOperation.CONNECT
        commandEpoch++
        scope.launch {
            if (configurationValid()) {
                Log.i(TAG, "Always-on start: connecting")
                connect(VpnServiceCommands.NO_TICKET, userRequest = true)
            } else {
                Log.w(TAG, "Always-on start without a valid configuration: not connecting")
                operation = null
                commandEpoch++
                ensurePolling()
            }
        }
    }

    /** R9 (step 9): the empty intent of START_STICKY after the process died. */
    private fun stickyRestart() {
        if (foreground || operation != null) return
        scope.launch {
            val decision = StickyRestartPolicy.decide(
                intents.read(),
                SystemClock.elapsedRealtime(),
                consentGranted = { prepare(this@PhpvpnVpnService) == null },
                configurationValid = { configurationValid() },
            )
            intents.update { decision.record }
            when (decision) {
                is RestartDecision.Reconnect -> {
                    Log.i(TAG, "restarted after the process ended: reconnecting (restart ${decision.record.restartsElapsedMs.size})")
                    if (enterForeground(LocalOperation.CONNECT)) connect(VpnServiceCommands.NO_TICKET, userRequest = false) else stopService()
                }
                is RestartDecision.GiveUp -> {
                    Log.i(TAG, "restarted after the process ended: not reconnecting (${decision.reason.name.lowercase()})")
                    if (decision.reason != GiveUpReason.NOT_WANTED) container.connectionEvents.report(DisconnectReason.PROCESS_RESTARTED)
                    stopService()
                }
            }
        }
    }

    /** Step 5: another VPN took the slot. An orderly Disconnect, never a reconnect. */
    override fun onRevoke() {
        // Not always called on the main thread.
        scope.launch {
            Log.i(TAG, "the VPN was revoked by Android (another VPN app or the settings)")
            intents.update { it.copy(wantConnected = false, restartsElapsedMs = emptyList()) }
            container.connectionEvents.report(DisconnectReason.REVOKED)
            disconnect(VpnServiceCommands.NO_TICKET)
        }
    }

    override fun onDestroy() {
        VpnServiceCommands.serviceRunning(false)
        scope.cancel()
        // Destroyed by Android while the engine still works: an orderly
        // Disconnect with a bound wait, so no tunnel outlives its host.
        if (!stopping && (operation != null || engineActive(lastStatus))) {
            runBlocking(Dispatchers.IO) {
                withTimeoutOrNull(DESTROY_DISCONNECT_MS) {
                    container.connection.disconnect()
                    while (engineActive(readStatus())) delay(DESTROY_POLL_MS)
                }
            }
        }
        VpnHosts.unregisterTunnelHost()
        super.onDestroy()
    }

    // --- TunnelHost (native threads) -----------------------------------------

    override fun establish(specJson: ByteArray): Int {
        val result = VpnInterfaces.establish(this, specJson) { builder ->
            builder.setConfigureIntent(VpnNotifications.openApp(this))
        }
        lastEstablishError = result.error
        return result.descriptor
    }

    override fun lastEstablishError(): Int = lastEstablishError

    override fun waitVpnNetworkGone(timeoutMs: Int): Boolean = VpnInterfaces.waitVpnNetworkGone(this, timeoutMs)

    // --- Foreground, polling and notification -------------------------------

    /** 500 ms polling until the outcome of the own command is settled (tray progress window). */
    private fun follow(command: LocalOperation) {
        followed = command
        followDeadline = SystemClock.elapsedRealtime() + NotificationSchedule.OWN_COMMAND_MS
    }

    private fun enterForeground(initial: LocalOperation): Boolean {
        if (foreground) return true
        val model = NotificationModel.of(lastStatus, initial) ?: NotificationModel(NotificationKind.CONNECTING)
        return try {
            startForeground(VpnNotifications.NOTIFICATION_ID, notifications.build(model, notificationText), ServiceInfo.FOREGROUND_SERVICE_TYPE_SYSTEM_EXEMPTED)
            foreground = true
            lastKey = null
            true
        } catch (error: IllegalStateException) {
            // ForegroundServiceStartNotAllowedException: never a crash.
            Log.w(TAG, "the foreground service could not start: ${error.javaClass.simpleName}")
            false
        } catch (error: SecurityException) {
            // systemExempted needs the VPN consent (app op activate_vpn) on API 34+.
            Log.w(TAG, "the foreground service could not start: the VPN consent is missing")
            false
        }
    }

    private fun ensurePolling() {
        if (pollJob?.isActive == true) {
            kick.trySend(Unit)
            return
        }
        pollJob = scope.launch {
            while (isActive) {
                val epoch = commandEpoch
                val status = readStatus()
                // Read across a command start or reply: read again at once.
                if (epoch != commandEpoch) continue
                onStatus(status)
                if (stopping) break
                val wait = NotificationSchedule.delayMs(status, operation, followed)
                withTimeoutOrNull(wait) { kick.receive() }
            }
        }
    }

    private fun onStatus(status: TrayStatus?) {
        if (status?.valid == true) lastStatus = status
        followed = NotificationSchedule.follow(followed, status, operation, SystemClock.elapsedRealtime(), followDeadline)
        if (status?.healthyConnected == true) {
            // A confirmed connection ends a restart series (R9 limit).
            intents.update { if (it.restartsElapsedMs.isEmpty()) it else it.copy(restartsElapsedMs = emptyList()) }
        }
        when (ServiceLifecycle.action(status, operation)) {
            ServiceAction.STOP -> {
                Log.i(TAG, "confirmed ${status?.state?.name?.lowercase()}: the service stops")
                intents.update { it.copy(wantConnected = false, restartsElapsedMs = emptyList()) }
                stopService()
            }
            ServiceAction.KEEP -> updateNotification(status)
        }
    }

    private fun updateNotification(status: TrayStatus?) {
        if (!foreground) return
        val model = NotificationModel.of(status, operation) ?: return
        val key = NotificationSchedule.Key(model, status?.state, status?.phase, notificationText)
        if (key == lastKey) return
        lastKey = key
        if (key.state != lastAlwaysOnState) {
            lastAlwaysOnState = key.state
            publishAlwaysOn()
        }
        val notification: Notification = notifications.build(model, notificationText)
        notifications.notify(notification)
    }

    private suspend fun refreshNotificationText() {
        val summary = (container.configSummary.refresh() as? ConfigSummaryState.Loaded)?.summary
            ?: (container.configSummary.state.value as? ConfigSummaryState.Loaded)?.summary
        notificationText = notifications.text(summary?.serverHost, summary?.virtualIp)
    }

    private fun stopService() {
        stopping = true
        if (foreground) {
            stopForeground(STOP_FOREGROUND_REMOVE)
            foreground = false
        } else {
            notifications.cancel()
        }
        // Ignored by Android when a newer start command arrived meanwhile.
        stopSelf(lastStartId)
    }

    private fun publishAlwaysOn() {
        ServiceAlwaysOnStatus.publish(AlwaysOnState(isAlwaysOn, isLockdownEnabled))
        if (isLockdownEnabled) Log.w(TAG, "Always-on lockdown is enabled; phpvpn does not support it")
    }

    private suspend fun configurationValid(): Boolean {
        val outcome = container.ipcClient.getConfigState()
        return outcome is IpcOutcome.Success && outcome.value.availability == ConfigAvailability.VALID
    }

    private suspend fun readStatus(): TrayStatus? = withContext(Dispatchers.IO) {
        NativeBridge.nativeStatus()?.let { StatusModel.parseStatusResponse(it.decodeToString()) }
    }?.takeIf { it.valid }

    private fun engineActive(status: TrayStatus?): Boolean =
        status != null && ServiceLifecycle.action(status, null) == ServiceAction.KEEP

    companion object {
        const val ACTION_CONNECT = "phpvpn.android.action.CONNECT"
        const val ACTION_DISCONNECT = "phpvpn.android.action.DISCONNECT"
        private const val EXTRA_TICKET = "phpvpn.android.extra.TICKET"
        private const val EXTRA_FOREGROUND = "phpvpn.android.extra.FOREGROUND"
        private const val TAG = "phpvpn"
        private const val DESTROY_DISCONNECT_MS = 5_000L
        private const val DESTROY_POLL_MS = 100L

        /**
         * [foreground] false: sent with startService because the consent is
         * missing; the foreground service type systemExempted requires it.
         */
        fun connectIntent(context: Context, ticket: Long = VpnServiceCommands.NO_TICKET, foreground: Boolean = true): Intent =
            Intent(context, PhpvpnVpnService::class.java).setAction(ACTION_CONNECT).putExtra(EXTRA_TICKET, ticket)
                .putExtra(EXTRA_FOREGROUND, foreground)

        fun disconnectIntent(context: Context, ticket: Long = VpnServiceCommands.NO_TICKET): Intent =
            Intent(context, PhpvpnVpnService::class.java).setAction(ACTION_DISCONNECT).putExtra(EXTRA_TICKET, ticket)
    }
}
