package phpvpn.android.service

import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.ipc.ConnectionPhase
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.Tristate
import phpvpn.android.bridge.model.TrayStatus
import phpvpn.android.ui.state.DisconnectReason

/**
 * WP05 pure decisions of PhpvpnVpnService: the R9 state file and restart
 * decision (plan step 9), when the foreground service stops (steps 4, 7),
 * the notification content (09) and its polling schedule ("Stav pro UI").
 */
class ServicePolicyTest {
    private fun status(state: ConnectionState, phase: ConnectionPhase, tunnel: Tristate = Tristate.NO, error: String = "") =
        TrayStatus(state = state, phase = phase, tunnelReady = tunnel, hasError = error.isNotEmpty(), errorCode = error, valid = true)

    private val connected = status(ConnectionState.CONNECTED, ConnectionPhase.RUNNING, Tristate.YES)
    private val connectedNoTunnel = status(ConnectionState.CONNECTED, ConnectionPhase.RUNNING, Tristate.NO)
    private val reconnecting = status(ConnectionState.CONNECTED, ConnectionPhase.RECONNECTING, Tristate.YES)
    private val connecting = status(ConnectionState.CONNECTING, ConnectionPhase.HEALTH)
    private val recovering = status(ConnectionState.RECOVERING, ConnectionPhase.IDLE, error = "communication_failed")
    private val stopping = status(ConnectionState.CONNECTED, ConnectionPhase.STOPPING, Tristate.YES)
    private val disconnected = status(ConnectionState.DISCONNECTED, ConnectionPhase.IDLE)
    private val failed = status(ConnectionState.ERROR, ConnectionPhase.IDLE, error = "tunnel_permission_missing")

    // --- intent.json ----------------------------------------------------------

    @Test
    fun intentRecordRoundTripsAndHasAStableForm() {
        val record = IntentRecord(true, listOf(1_000L, 2_000L), DisconnectReason.PROCESS_RESTARTED)
        val text = IntentRecordCodec.encode(record)
        assertEquals(
            """{"version":1,"wantConnected":true,"restartsElapsedMs":[1000,2000],"notice":"process_restarted"}""",
            text,
        )
        assertEquals(record, IntentRecordCodec.decode(text))
        assertEquals(IntentRecord(notice = DisconnectReason.REVOKED), IntentRecordCodec.decode(IntentRecordCodec.encode(IntentRecord(notice = DisconnectReason.REVOKED))))
        assertEquals(IntentRecord(), IntentRecordCodec.decode(IntentRecordCodec.encode(IntentRecord())))
    }

    @Test
    fun anUnreadableIntentFileNeverReconnects() {
        val safe = IntentRecord()
        for (text in listOf(
            "",
            "{",
            "null",
            """{"version":2,"wantConnected":true,"restartsElapsedMs":[],"notice":null}""",
            """{"version":1,"wantConnected":"true","restartsElapsedMs":[],"notice":null}""",
            """{"version":1,"wantConnected":true,"restartsElapsedMs":[],"notice":"other"}""",
            """{"version":1,"wantConnected":true,"restartsElapsedMs":[],"notice":null,"extra":1}""",
            """{"version":1,"wantConnected":true}""",
            """{"version":1,"wantConnected":true,"restartsElapsedMs":[${(1..17).joinToString(",")}],"notice":null}""",
        )) {
            assertEquals(text, safe, IntentRecordCodec.decode(text))
        }
    }

    // --- R9 sticky restart ----------------------------------------------------

    private class Probes(var consent: Boolean = true, var config: Boolean = true) {
        var consentCalls = 0
        var configCalls = 0

        fun decide(record: IntentRecord, now: Long) = runBlocking {
            StickyRestartPolicy.decide(record, now, { consentCalls++; consent }, { configCalls++; config })
        }
    }

    @Test
    fun withoutTheFlagNothingIsCheckedOrSaid() {
        val probes = Probes()
        val record = IntentRecord(notice = DisconnectReason.REVOKED)
        val decision = probes.decide(record, 100_000)
        assertEquals(RestartDecision.GiveUp(GiveUpReason.NOT_WANTED, record), decision)
        // prepare() would take the slot of another VPN app: never called without the flag.
        assertEquals(0, probes.consentCalls)
        assertEquals(0, probes.configCalls)
    }

    @Test
    fun flagConsentAndValidConfigurationReconnect() {
        val probes = Probes()
        val decision = probes.decide(IntentRecord(wantConnected = true, notice = DisconnectReason.PROCESS_RESTARTED), 100_000)
        assertEquals(RestartDecision.Reconnect(IntentRecord(true, listOf(100_000L), null)), decision)
        assertEquals(1, probes.consentCalls)
        assertEquals(1, probes.configCalls)
    }

    @Test
    fun missingConsentOrConfigurationEndsWithTheRestartText() {
        val noConsent = Probes(consent = false)
        val first = noConsent.decide(IntentRecord(wantConnected = true), 100_000)
        assertEquals(RestartDecision.GiveUp(GiveUpReason.NO_CONSENT, IntentRecord(notice = DisconnectReason.PROCESS_RESTARTED)), first)
        assertEquals("the configuration is not read without consent", 0, noConsent.configCalls)

        val noConfig = Probes(config = false)
        val second = noConfig.decide(IntentRecord(wantConnected = true, restartsElapsedMs = listOf(90_000L)), 100_000)
        assertEquals(RestartDecision.GiveUp(GiveUpReason.CONFIGURATION_NOT_VALID, IntentRecord(notice = DisconnectReason.PROCESS_RESTARTED)), second)
        assertFalse(second.record.wantConnected)
        assertTrue(second.record.restartsElapsedMs.isEmpty())
    }

    @Test
    fun threeRestartsWithoutConnectedInTenMinutesAreTheLimit() {
        val probes = Probes()
        var record = IntentRecord(wantConnected = true)
        val start = 1_000_000L
        for (restart in 1..3) {
            val decision = probes.decide(record, start + restart * 60_000L)
            assertTrue("restart $restart reconnects", decision is RestartDecision.Reconnect)
            record = decision.record
        }
        assertEquals(3, record.restartsElapsedMs.size)
        val fourth = probes.decide(record, start + 4 * 60_000L)
        assertEquals(RestartDecision.GiveUp(GiveUpReason.RESTART_LIMIT, IntentRecord(notice = DisconnectReason.PROCESS_RESTARTED)), fourth)
        assertEquals("the limit is checked before prepare()", 3, probes.consentCalls)
    }

    @Test
    fun restartsOutsideTheWindowOrBeforeAReebootDoNotCount() {
        val probes = Probes()
        val now = 5_000_000L
        val old = now - StickyRestartPolicy.WINDOW_MS - 1
        // Two old restarts, one "in the future" (elapsed time restarted with a reboot), one recent.
        val record = IntentRecord(wantConnected = true, restartsElapsedMs = listOf(old, old - 1, now + 50_000, now - 1_000))
        val decision = probes.decide(record, now)
        assertEquals(RestartDecision.Reconnect(IntentRecord(true, listOf(now - 1_000, now), null)), decision)
        // Exactly at the window edge a restart still counts.
        val edge = IntentRecord(wantConnected = true, restartsElapsedMs = List(3) { now - StickyRestartPolicy.WINDOW_MS })
        assertTrue(probes.decide(edge, now) is RestartDecision.GiveUp)
    }

    @Test
    fun aVanishedConnectionIsExplainedOnlyWhenNothingRuns() {
        val wanted = IntentRecord(wantConnected = true)
        assertTrue(ProcessEndCheck.endedWithTheProcess(wanted, serviceRunning = false, status = disconnected))
        assertFalse("no flag", ProcessEndCheck.endedWithTheProcess(IntentRecord(), false, disconnected))
        assertFalse("the sticky restart runs", ProcessEndCheck.endedWithTheProcess(wanted, true, disconnected))
        assertFalse("status unknown", ProcessEndCheck.endedWithTheProcess(wanted, false, null))
        assertFalse(ProcessEndCheck.endedWithTheProcess(wanted, false, TrayStatus()))
        for (busy in listOf(connected, connecting, recovering, stopping, failed)) {
            assertFalse("$busy", ProcessEndCheck.endedWithTheProcess(wanted, false, busy))
        }
    }

    // --- foreground life cycle ------------------------------------------------

    @Test
    fun onlyAConfirmedIdleStatusStopsTheService() {
        assertEquals(ServiceAction.STOP, ServiceLifecycle.action(disconnected, null))
        assertEquals(ServiceAction.STOP, ServiceLifecycle.action(failed, null))
        for (busy in listOf(connected, connectedNoTunnel, reconnecting, connecting, recovering, stopping)) {
            assertEquals("$busy", ServiceAction.KEEP, ServiceLifecycle.action(busy, null))
        }
        // An unanswered or invalid read (timeout) never stops it.
        assertEquals(ServiceAction.KEEP, ServiceLifecycle.action(null, null))
        assertEquals(ServiceAction.KEEP, ServiceLifecycle.action(TrayStatus(), null))
        // Disconnected but not idle yet.
        assertEquals(ServiceAction.KEEP, ServiceLifecycle.action(status(ConnectionState.DISCONNECTED, ConnectionPhase.STOPPING), null))
        // The own command has not changed the state yet.
        assertEquals(ServiceAction.KEEP, ServiceLifecycle.action(disconnected, LocalOperation.CONNECT))
        assertEquals(ServiceAction.KEEP, ServiceLifecycle.action(failed, LocalOperation.CONNECT))
        assertEquals(ServiceAction.KEEP, ServiceLifecycle.action(connected, LocalOperation.DISCONNECT))
    }

    // --- notification ---------------------------------------------------------

    @Test
    fun notificationFollowsTheConnectionStates() {
        fun kind(s: TrayStatus?, op: LocalOperation? = null) = NotificationModel.of(s, op)?.kind
        assertEquals(NotificationKind.CONNECTED, kind(connected))
        assertEquals(NotificationKind.RECONNECTING, kind(connectedNoTunnel))
        assertEquals(NotificationKind.RECONNECTING, kind(reconnecting))
        assertEquals(NotificationKind.RECONNECTING, kind(recovering))
        assertEquals(NotificationKind.CONNECTING, kind(connecting))
        assertEquals(NotificationKind.DISCONNECTING, kind(stopping))
        assertEquals(NotificationKind.FAILED, kind(failed))
        assertEquals(NotificationKind.UNAVAILABLE, kind(null))
        assertEquals(NotificationKind.UNAVAILABLE, kind(TrayStatus()))
        assertNull("removed in disconnected", kind(disconnected))
        // Before the reply changed the state: the own command.
        assertEquals(NotificationKind.CONNECTING, kind(disconnected, LocalOperation.CONNECT))
        assertEquals(NotificationKind.CONNECTING, kind(failed, LocalOperation.CONNECT))
        assertEquals(NotificationKind.DISCONNECTING, kind(disconnected, LocalOperation.DISCONNECT))
        assertEquals(NotificationKind.CONNECTED, kind(connected, LocalOperation.DISCONNECT))
    }

    @Test
    fun disconnectActionAndIconPerKind() {
        val withAction = NotificationKind.entries.filter { NotificationModel(it).disconnectAction }.toSet()
        assertEquals(setOf(NotificationKind.CONNECTED, NotificationKind.CONNECTING, NotificationKind.RECONNECTING), withAction)
        assertEquals(NotificationIcon.CONNECTED, NotificationModel(NotificationKind.CONNECTED).icon)
        for (kind in listOf(NotificationKind.CONNECTING, NotificationKind.RECONNECTING, NotificationKind.DISCONNECTING)) {
            assertEquals(NotificationIcon.CONNECTING, NotificationModel(kind).icon)
        }
        assertEquals(NotificationIcon.DISCONNECTED, NotificationModel(NotificationKind.FAILED).icon)
        assertEquals(NotificationIcon.DISCONNECTED, NotificationModel(NotificationKind.UNAVAILABLE).icon)
    }

    @Test
    fun theNotificationIsRewrittenOnlyOnAChange() {
        val first = NotificationSchedule.Key(NotificationModel.of(connected, null), connected.state, connected.phase, "vpn.example.com · 10.99.99.2")
        // Counters and times change every read; the key does not.
        val later = connected.copy(connectionStartedAtUtcMs = 42)
        val second = NotificationSchedule.Key(NotificationModel.of(later, null), later.state, later.phase, "vpn.example.com · 10.99.99.2")
        assertEquals(first, second)
        val recoveringKey = NotificationSchedule.Key(NotificationModel.of(reconnecting, null), reconnecting.state, reconnecting.phase, first.text)
        assertTrue(first != recoveringKey)
        assertTrue(first != first.copy(text = "vpn.example.com"))
    }

    // --- polling schedule (tray: 5 s, 500 ms) ---------------------------------

    @Test
    fun pollingIsFastOnlyDuringTransitionsAndAfterOwnCommands() {
        assertEquals(5_000L, NotificationSchedule.delayMs(connected, null, null))
        assertEquals(5_000L, NotificationSchedule.delayMs(null, null, null))
        for (transition in listOf(connecting, recovering, reconnecting, stopping)) {
            assertEquals(500L, NotificationSchedule.delayMs(transition, null, null))
        }
        assertEquals(500L, NotificationSchedule.delayMs(disconnected, LocalOperation.CONNECT, null))
        assertEquals(500L, NotificationSchedule.delayMs(connected, null, LocalOperation.DISCONNECT))
        // The fast polling after an own command ends with its settled outcome or the deadline.
        val connect = LocalOperation.CONNECT
        val disconnect = LocalOperation.DISCONNECT
        fun follow(followed: LocalOperation?, s: TrayStatus?, op: LocalOperation? = null, now: Long = 0) =
            NotificationSchedule.follow(followed, s, op, now, NotificationSchedule.OWN_COMMAND_MS)
        assertEquals(connect, follow(connect, connecting))
        assertEquals(connect, follow(connect, null))
        assertEquals(connect, follow(null, connected, op = connect))
        assertNull(follow(connect, connected))
        assertNull(follow(connect, failed))
        assertNull(follow(connect, disconnected))
        assertEquals(connect, follow(connect, recovering))
        // A Disconnect reply still says "connected": stay fast until the stop is confirmed.
        assertEquals(disconnect, follow(disconnect, connected))
        assertEquals(disconnect, follow(disconnect, stopping))
        assertNull(follow(disconnect, disconnected))
        assertNull(follow(disconnect, failed))
        assertNull("bounded by the command budget", follow(disconnect, stopping, now = NotificationSchedule.OWN_COMMAND_MS))
        assertNull(follow(null, connecting))
        // No schedule step is shorter than the tray's 500 ms (no 1 s timer, no busy loop).
        val all = listOf(connected, connecting, null, disconnected).flatMap { s ->
            listOf(null, connect).flatMap { op -> listOf(null, disconnect).map { NotificationSchedule.delayMs(s, op, it) } }
        }
        assertEquals(setOf(500L, 5_000L), all.toSet())
    }

    // --- boot receiver --------------------------------------------------------

    @Test
    fun autoConnectOnStartupIsAStrictBoolean() {
        fun doc(value: String?) = if (value == null) """{"masterSetup":{"serverUrl":"x"}}""" else """{"masterSetup":{"autoConnectOnStartup":$value}}"""
        assertTrue(AutoConnectSetting.of(doc("true")))
        assertFalse(AutoConnectSetting.of(doc("false")))
        assertFalse(AutoConnectSetting.of(doc("\"true\"")))
        assertFalse(AutoConnectSetting.of(doc("1")))
        assertFalse("missing means false", AutoConnectSetting.of(doc(null)))
        assertFalse(AutoConnectSetting.of("{"))
        assertFalse(AutoConnectSetting.of("""{"autoConnectOnStartup":true}"""))
    }
}
