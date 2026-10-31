@file:OptIn(kotlinx.coroutines.ExperimentalCoroutinesApi::class)

package phpvpn.android.ui.state

import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.bridge.repository.ConnectionRepository
import phpvpn.android.testing.IpcReplies
import phpvpn.android.testing.respondText
import phpvpn.android.testing.respondTimeout

/** ConnectionViewModel over the real repository and a scripted transport, on virtual time. */
class ConnectionViewModelTest {
    @get:Rule
    val main = MainDispatcherRule()

    private class Harness(
        val transport: FakeIpcTransport,
        val repository: ConnectionRepository,
        val viewModel: ConnectionViewModel,
        val consent: FakeConsent,
        val events: ConnectionUiEvents,
        val effects: MutableList<ConnectionEffect>,
    ) {
        val state get() = viewModel.screen.value.state
    }

    private fun TestScope.harness(granted: Boolean = true, collect: Boolean = true): Harness {
        val transport = FakeIpcTransport()
        transport.respondText("get-status", IpcReplies.disconnected())
        transport.respondText("get-config-document", IpcReplies.configDocument())
        val client = client(transport)
        val repository = connectionRepository(client)
        val consent = FakeConsent(granted)
        val events = ConnectionUiEvents()
        val viewModel = ConnectionViewModel(
            repository,
            RepositoryConnectActions(repository),
            consent,
            events,
            ConfigSummaryRepository(client),
            wallClock = { 5_000L },
        )
        val effects = mutableListOf<ConnectionEffect>()
        backgroundScope.launch { viewModel.effects.toList(effects) }
        if (collect) backgroundScope.launch { viewModel.screen.collect {} }
        runCurrent()
        return Harness(transport, repository, viewModel, consent, events, effects)
    }

    @Test
    fun pollsOnlyWhileTheScreenCollects() = runTest(main.dispatcher) {
        val h = harness(collect = false)
        advanceTimeBy(30_000)
        assertEquals(0, h.transport.requests("get-status").size)
        val job = backgroundScope.launch { h.viewModel.screen.collect {} }
        runCurrent()
        assertEquals(1, h.transport.requests("get-status").size)
        assertEquals(ConnectionUiState.Disconnected(null), h.state)
        job.cancel()
        advanceTimeBy(30_000)
        assertEquals(1, h.transport.requests("get-status").size)
    }

    @Test
    fun serverCardComesFromTheConfigurationWithoutSecrets() = runTest(main.dispatcher) {
        val h = harness()
        val config = h.viewModel.screen.value.config!!
        assertEquals(ConfigAvailability.VALID, config.availability)
        assertEquals("https://vpn.example.com/phpvpn.php", config.serverUrl)
        assertEquals("10.80.0.12", config.virtualIp)
        assertEquals("10.80.0.0/24", config.vpnCidr)
        assertEquals("vpn.example.com", config.serverHost)
        assertTrue("CHANGE_THIS" !in config.toString())
    }

    @Test
    fun connectWithConsentRunsThroughConnectingToConnected() = runTest(main.dispatcher) {
        val h = harness(granted = true)
        h.transport.respondText("connect", IpcReplies.connecting())
        h.transport.respondText("get-status", IpcReplies.connecting())
        h.viewModel.connect()
        runCurrent()
        assertEquals(1, h.transport.requests("connect").size)
        assertTrue(h.state is ConnectionUiState.Connecting)
        // Polling is 500 ms during the attempt.
        val before = h.transport.requests("get-status").size
        advanceTimeBy(501)
        runCurrent()
        assertTrue(h.transport.requests("get-status").size > before)
        h.transport.respondText("get-status", IpcReplies.connected())
        advanceTimeBy(501)
        runCurrent()
        assertTrue(h.state is ConnectionUiState.Connected)
        // The finished progress is closed: after the read already scheduled
        // at 500 ms, polling is back to the 5 s schedule.
        assertNull(h.repository.progress.value?.takeUnless { it.view.closeRequested })
        advanceTimeBy(600)
        runCurrent()
        val settled = h.transport.requests("get-status").size
        advanceTimeBy(4_000)
        assertEquals(settled, h.transport.requests("get-status").size)
        assertTrue(h.effects.isEmpty())
    }

    @Test
    fun connectWithoutConsentAsksAndDenialLeadsTo37() = runTest(main.dispatcher) {
        val h = harness(granted = false)
        h.events.report(DisconnectReason.REVOKED)
        h.viewModel.connect()
        runCurrent()
        assertEquals(listOf<ConnectionEffect>(ConnectionEffect.RequestConsent), h.effects)
        assertEquals(0, h.transport.requests("connect").size)
        // The reason is cleared by the new Connect.
        assertNull(h.events.reason.value)
        h.viewModel.onConsentResult(false)
        runCurrent()
        assertEquals(ConnectionEffect.PermissionDenied, h.effects.last())
        assertEquals(0, h.transport.requests("connect").size)
        h.transport.respondText("connect", IpcReplies.connecting())
        h.viewModel.onConsentResult(true)
        runCurrent()
        assertEquals(1, h.transport.requests("connect").size)
    }

    @Test
    fun disconnectWaitsForConfirmationThenShowsDisconnected() = runTest(main.dispatcher) {
        val h = harness()
        h.transport.respondText("get-status", IpcReplies.connected())
        h.viewModel.checkStatus()
        runCurrent()
        assertTrue(h.state is ConnectionUiState.Connected)
        h.transport.respondText("disconnect", IpcReplies.stopping())
        h.transport.respondText("get-status", IpcReplies.stopping())
        h.viewModel.disconnect()
        runCurrent()
        assertEquals(ConnectionUiState.Disconnecting, h.state)
        h.transport.respondText("get-status", IpcReplies.disconnected())
        advanceTimeBy(501)
        runCurrent()
        assertEquals(ConnectionUiState.Disconnected(null), h.state)
    }

    @Test
    fun disconnectTimeoutIs38AndCheckStatusLeavesIt() = runTest(main.dispatcher) {
        val h = harness()
        h.transport.respondText("get-status", IpcReplies.connected())
        h.viewModel.checkStatus()
        runCurrent()
        h.transport.respondTimeout("disconnect")
        h.viewModel.disconnect()
        runCurrent()
        assertEquals(ConnectionUiState.DisconnectUnconfirmed, h.state)
        h.viewModel.leaveUnconfirmed()
        runCurrent()
        assertTrue(h.state is ConnectionUiState.Connected)
    }

    @Test
    fun statusTimeoutIs08WithTheLastKnownState() = runTest(main.dispatcher) {
        val h = harness()
        h.transport.respondText("get-status", IpcReplies.connected())
        h.viewModel.checkStatus()
        runCurrent()
        h.transport.respondTimeout("get-status")
        h.viewModel.checkStatus()
        runCurrent()
        val state = h.state as ConnectionUiState.Unavailable
        assertTrue(state.lastKnown!!.status.healthyConnected)
        assertEquals(5_000L, state.lastKnown.seenAtUtcMs)
        // A later read can confirm any state; nothing is started.
        h.transport.respondText("get-status", IpcReplies.disconnected())
        h.viewModel.checkStatus()
        runCurrent()
        assertEquals(ConnectionUiState.Disconnected(null), h.state)
        assertEquals(0, h.transport.requests("connect").size)
    }

    @Test
    fun refusedConnectExplainsTheCodeOrOpensConfigAttention() = runTest(main.dispatcher) {
        val h = harness()
        h.transport.respondText("connect", IpcReplies.error("busy", recoverable = true))
        h.viewModel.connect()
        runCurrent()
        assertEquals(ErrorCode.BUSY.messageKey, h.viewModel.screen.value.refusedConnect)
        h.transport.respondText("connect", IpcReplies.error("config_invalid_json"))
        h.viewModel.connect()
        runCurrent()
        assertNull(h.viewModel.screen.value.refusedConnect)
        assertEquals(ConnectionEffect.ConfigAttention, h.effects.last())
        h.transport.respondText("connect", IpcReplies.error("tunnel_permission_missing"))
        h.viewModel.connect()
        runCurrent()
        assertEquals(ConnectionEffect.PermissionDenied, h.effects.last())
    }

    @Test
    fun failedConnectShowsTheErrorScreenWithAndWithoutDiagnostic() = runTest(main.dispatcher) {
        val h = harness()
        h.transport.respondText("connect", IpcReplies.connecting(List(4) { "pending" }))
        h.transport.respondText("get-status", IpcReplies.failed())
        h.viewModel.connect()
        runCurrent()
        advanceTimeBy(501)
        runCurrent()
        val plain = h.state as ConnectionUiState.Failed
        assertEquals(false, plain.diagnostic)
        h.transport.respondText("get-status", IpcReplies.failed(diagnostic = true))
        h.viewModel.connect()
        runCurrent()
        advanceTimeBy(501)
        runCurrent()
        assertEquals(true, (h.state as ConnectionUiState.Failed).diagnostic)
    }
}
