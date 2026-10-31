@file:OptIn(kotlinx.coroutines.ExperimentalCoroutinesApi::class)

package phpvpn.android.ui.state

import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.model.StatisticsField
import phpvpn.android.bridge.repository.ServerInfoHook
import phpvpn.android.bridge.repository.ServerInfoRepository
import phpvpn.android.bridge.repository.StatisticsRepository
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.testing.IpcReplies
import phpvpn.android.testing.respondText
import phpvpn.android.testing.respondTimeout

/** Traffic (26/27/43), Server (28/29/44/45), Settings (10), 36/00 and Exit (35) state holders. */
class DestinationViewModelsTest {
    @get:Rule
    val main = MainDispatcherRule()

    // ------------------------------------------------------------ Traffic

    @Test
    fun trafficWaitsThenShowsTheTenValuesThenMarksThemStale() = runTest(main.dispatcher) {
        val transport = FakeIpcTransport()
        val counters = IpcReplies.Counters(312, 1284, 5_310_000, 23_700_000, 8204, 14826, 4_160_000, 18_420_000)
        transport.respondText("get-status", IpcReplies.connected(counters))
        val connection = connectionRepository(client(transport))
        val viewModel = TrafficViewModel(StatisticsRepository(connection, backgroundScope))
        assertEquals(TrafficUiState.Waiting, viewModel.state.value)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()
        val values = viewModel.state.value as TrafficUiState.Values
        assertFalse(values.stale)
        assertTrue(values.connected)
        assertEquals(StatisticsField.entries.toSet(), values.values.keys)
        assertEquals(1596, values[StatisticsField.TOTAL_POLLS].toInt())
        assertEquals(29_010_000, values[StatisticsField.TOTAL_SIZE].toInt())
        assertEquals(18_420_000, values[StatisticsField.BYTES_RECEIVED].toInt())
        transport.respondTimeout("get-status")
        connection.refresh()
        runCurrent()
        val stale = viewModel.state.value as TrafficUiState.Values
        assertTrue(stale.stale)
        assertEquals(values.values, stale.values)
    }

    @Test
    fun trafficWithoutAnyValidSnapshotStaysWaitingNotZero() = runTest(main.dispatcher) {
        val transport = FakeIpcTransport()
        transport.respondTimeout("get-status")
        val connection = connectionRepository(client(transport))
        val viewModel = TrafficViewModel(StatisticsRepository(connection, backgroundScope))
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()
        advanceTimeBy(6_000)
        assertEquals(TrafficUiState.Waiting, viewModel.state.value)
    }

    // ------------------------------------------------------------ Server

    @Test
    fun serverLoadsListThenKeepsTheLastSuccessOnFailure() = runTest(main.dispatcher) {
        val transport = FakeIpcTransport()
        transport.respondText("get-server-info", IpcReplies.serverInfoSample())
        transport.respondText("get-config-document", IpcReplies.configDocument())
        val client = client(transport)
        val config = ConfigSummaryRepository(client)
        val repository = ServerInfoRepository(client, backgroundScope, ServerInfoHook { KotlinServerInfoModel.build(it.result) is ServerInfoModelResult.Ok })
        var now = 1_000L
        val viewModel = ServerViewModel(repository, KotlinServerInfoModel, config, wallClock = { now })
        backgroundScope.launch { viewModel.state.collect {} }
        config.refresh()
        runCurrent()
        val list = viewModel.state.value
        assertEquals(ServerVariant.LIST, list.variant)
        assertEquals("vpn.example.com", list.serverHost)
        assertEquals(listOf("10.80.0.2", "10.80.0.3", "10.80.0.4"), list.pageRows.map { it.peer })
        assertEquals(listOf(ServerClientState.VALID, ServerClientState.INVALID, ServerClientState.OFFLINE), list.pageRows.map { it.state })
        assertEquals(2, list.model!!.onlineClients)
        assertEquals(1, list.model.validOnlineClients)
        assertEquals(1_000L, list.refreshedAtUtcMs)
        // 29 shows exactly the same snapshot, verbatim.
        assertTrue(list.model.infoPlaintext.contains("\"serverVersion\":\"1.0.0-synthetic\""))

        now = 9_000L
        transport.respondTimeout("get-server-info")
        viewModel.refresh()
        runCurrent()
        val stale = viewModel.state.value
        assertEquals(ServerVariant.STALE, stale.variant)
        assertEquals(list.model, stale.model)
        assertEquals(1_000L, stale.refreshedAtUtcMs)
        assertEquals(ErrorCode.TIMEOUT.wireName, stale.failure?.code)
    }

    @Test
    fun serverEmptyListIsNotAFailureAndFailureWithoutDataOffersRetry() = runTest(main.dispatcher) {
        val transport = FakeIpcTransport()
        transport.respondText("get-server-info", IpcReplies.error("server_version_incompatible"))
        val client = client(transport)
        val repository = ServerInfoRepository(client, backgroundScope)
        val viewModel = ServerViewModel(repository, KotlinServerInfoModel, ConfigSummaryRepository(client))
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()
        val failed = viewModel.state.value
        assertEquals(ServerVariant.FAILED, failed.variant)
        assertNull(failed.model)
        assertEquals("server_version_incompatible", failed.failure?.code)

        transport.respondText("get-server-info", IpcReplies.serverInfo())
        viewModel.refresh()
        runCurrent()
        val empty = viewModel.state.value
        assertEquals(ServerVariant.EMPTY, empty.variant)
        assertNull(empty.failure)
        assertTrue(empty.pageRows.isEmpty())
    }

    @Test
    fun serverAutoRefreshAndPagination() = runTest(main.dispatcher) {
        val transport = FakeIpcTransport()
        val peers = (2..46).map { IpcReplies.Peer("10.80.0.$it", "2026-10-30T08:41:00.000Z", null) }.filter { it.address != "10.80.0.12" }
        transport.respondText("get-server-info", IpcReplies.serverInfo(peers = peers))
        val client = client(transport)
        val repository = ServerInfoRepository(client, backgroundScope)
        val viewModel = ServerViewModel(repository, KotlinServerInfoModel, ConfigSummaryRepository(client))
        val job = backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()
        val first = viewModel.state.value
        assertEquals(44, first.model!!.rows.size)
        assertEquals(3, first.pageCount)
        assertEquals(20, first.pageRows.size)
        viewModel.showPage(2)
        runCurrent()
        assertEquals(4, viewModel.state.value.pageRows.size)
        viewModel.showPage(10)
        runCurrent()
        assertEquals(2, viewModel.state.value.page)
        // Auto refresh: off by default, then every 5 s, only while collected.
        assertFalse(first.autoRefresh)
        assertEquals(1, transport.requests("get-server-info").size)
        viewModel.setAutoRefresh(true)
        runCurrent()
        advanceTimeBy(5_001)
        runCurrent()
        assertEquals(2, transport.requests("get-server-info").size)
        job.cancel()
        advanceTimeBy(20_000)
        assertEquals(2, transport.requests("get-server-info").size)
    }

    // ------------------------------------------------------------ Settings, 36, 00, start

    @Test
    fun settingsRoutesTheWizardThroughTheGateUnlessConfirmedDisconnected() = runTest(main.dispatcher) {
        val transport = FakeIpcTransport()
        val connection = connectionRepository(client(transport))
        val viewModel = SettingsViewModel(connection, AlwaysOnStatus.UNKNOWN)
        assertEquals(WizardEntry.GATE, viewModel.wizardEntry())
        transport.respondText("get-status", IpcReplies.disconnected())
        connection.readOnce()
        assertEquals(WizardEntry.URL, viewModel.wizardEntry())
        transport.respondText("get-status", IpcReplies.connected())
        connection.readOnce()
        assertEquals(WizardEntry.GATE, viewModel.wizardEntry())
        assertNull(viewModel.alwaysOn.value)
    }

    @Test
    fun attentionClassifiesEveryAvailability() = runTest(main.dispatcher) {
        val transport = FakeIpcTransport()
        val viewModel = ConfigStateViewModel(client(transport), loadOnStart = false)
        val cases = mapOf(
            "missing" to AttentionUiState.Missing::class,
            "unreadable" to AttentionUiState.Unreadable::class,
            "encoding_error" to AttentionUiState.Unreadable::class,
            "too_large" to AttentionUiState.Unreadable::class,
            "invalid" to AttentionUiState.Invalid::class,
            "repairable" to AttentionUiState.Invalid::class,
            "valid" to AttentionUiState.Valid::class,
        )
        for ((availability, expected) in cases) {
            transport.respondText("get-config-state", IpcReplies.configState(availability, if (availability == "valid") null else "config_invalid_value"))
            viewModel.reload()
            runCurrent()
            assertEquals(availability, expected, viewModel.state.value::class)
        }
        transport.respondTimeout("get-config-state")
        viewModel.reload()
        runCurrent()
        assertEquals(AttentionUiState.Unknown(ErrorCode.TIMEOUT.messageKey), viewModel.state.value)
        val unreadable = AttentionUiState.from(
            phpvpn.android.bridge.ipc.IpcOutcome.Success(
                phpvpn.android.bridge.ipc.ConfigStateResult(
                    phpvpn.android.bridge.ipc.ConfigAvailability.ENCODING_ERROR, "config_encoding_error", IpcReplies.CONFIG_PATH, null,
                    "00000000-0000-4000-8000-000000000001", 0uL,
                    phpvpn.android.bridge.ipc.ConnectionState.DISCONNECTED, phpvpn.android.bridge.ipc.ConnectionPhase.IDLE,
                ),
            ),
        )
        assertEquals(AttentionUiState.Unreadable(IpcReplies.CONFIG_PATH, ErrorCode.CONFIG_ENCODING_ERROR.messageKey), unreadable)
    }

    @Test
    fun editorEntryNeverOpensAnEmptyEditorForAMissingFile() = runTest(main.dispatcher) {
        val transport = FakeIpcTransport()
        val viewModel = ConfigStateViewModel(client(transport), loadOnStart = false)
        transport.respondText("get-config-state", IpcReplies.configState("missing", "config_not_found"))
        assertEquals(EditorEntry.CreateDefault(IpcReplies.CONFIG_PATH), viewModel.editorEntry())
        transport.respondText("get-config-state", IpcReplies.configState("invalid", "config_invalid_json"))
        assertEquals(EditorEntry.Editor, viewModel.editorEntry())
        transport.respondText("get-config-state", IpcReplies.configState("unreadable", "config_unreadable"))
        assertEquals(EditorEntry.Attention, viewModel.editorEntry())
        assertEquals(0, transport.requests("create-default-config").size)
    }

    @Test
    fun startDestinationIsDecidedOnce() = runTest(main.dispatcher) {
        val transport = FakeIpcTransport()
        transport.respondText("get-config-state", IpcReplies.configState("missing", "config_not_found"))
        val viewModel = StartDestinationViewModel(client(transport))
        assertEquals(StartTarget.WELCOME, viewModel.decide())
        assertNull(viewModel.decide())
        assertEquals(1, transport.requests("get-config-state").size)
        val invalid = FakeIpcTransport().also { it.respondText("get-config-state", IpcReplies.configState("invalid", "config_invalid_json")) }
        assertEquals(StartTarget.ATTENTION, StartDestinationViewModel(client(invalid)).decide())
        val valid = FakeIpcTransport().also { it.respondText("get-config-state", IpcReplies.configState()) }
        assertNull(StartDestinationViewModel(client(valid)).decide())
        val silent = FakeIpcTransport().also { it.respondTimeout("get-config-state") }
        assertNull(StartDestinationViewModel(client(silent)).decide())
    }

    // ------------------------------------------------------------ Exit

    @Test
    fun exitCloseWindowOnlyFinishesTheActivity() = runTest(main.dispatcher) {
        val transport = FakeIpcTransport()
        val connection = connectionRepository(client(transport))
        val viewModel = ExitViewModel(connection, RepositoryConnectActions(connection))
        val effects = mutableListOf<ExitEffect>()
        backgroundScope.launch { viewModel.effects.toList(effects) }
        viewModel.closeWindow()
        runCurrent()
        assertEquals(listOf<ExitEffect>(ExitEffect.CloseWindow), effects)
        assertEquals(0, transport.requests("disconnect").size)
    }

    @Test
    fun exitDisconnectAndCloseWaitsForTheConfirmedDisconnect() = runTest(main.dispatcher) {
        val transport = FakeIpcTransport()
        transport.respondText("get-status", IpcReplies.connected())
        val connection = connectionRepository(client(transport))
        connection.readOnce()
        val viewModel = ExitViewModel(connection, RepositoryConnectActions(connection))
        val effects = mutableListOf<ExitEffect>()
        backgroundScope.launch { viewModel.effects.toList(effects) }
        transport.respondText("disconnect", IpcReplies.stopping())
        transport.respondText("get-status", IpcReplies.stopping())
        viewModel.disconnectAndClose()
        runCurrent()
        assertTrue(viewModel.disconnecting.value)
        assertTrue(effects.isEmpty())
        transport.respondText("get-status", IpcReplies.disconnected())
        advanceTimeBy(600)
        runCurrent()
        assertEquals(listOf<ExitEffect>(ExitEffect.CloseTask), effects)
        assertEquals(1, transport.requests("disconnect").size)
    }

    @Test
    fun exitWhenAlreadyDisconnectedClosesWithoutCommandAndTimeoutNeverCloses() = runTest(main.dispatcher) {
        val transport = FakeIpcTransport()
        transport.respondText("get-status", IpcReplies.disconnected())
        val connection = connectionRepository(client(transport))
        connection.readOnce()
        val viewModel = ExitViewModel(connection, RepositoryConnectActions(connection))
        val effects = mutableListOf<ExitEffect>()
        backgroundScope.launch { viewModel.effects.toList(effects) }
        viewModel.disconnectAndClose()
        runCurrent()
        assertEquals(listOf<ExitEffect>(ExitEffect.CloseTask), effects)
        assertEquals(0, transport.requests("disconnect").size)

        val busy = FakeIpcTransport()
        busy.respondText("get-status", IpcReplies.connected())
        val connected = connectionRepository(client(busy))
        connected.readOnce()
        val other = ExitViewModel(connected, RepositoryConnectActions(connected))
        val otherEffects = mutableListOf<ExitEffect>()
        backgroundScope.launch { other.effects.toList(otherEffects) }
        busy.respondTimeout("disconnect")
        other.disconnectAndClose()
        advanceTimeBy(31_000)
        runCurrent()
        assertEquals(listOf<ExitEffect>(ExitEffect.ShowConnection), otherEffects)
        assertNotNull(connected.progress.value)
    }
}
