package phpvpn.android.bridge.repository

import java.math.BigInteger
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.Fixtures
import phpvpn.android.bridge.SequentialIds
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.ConnectionPhase
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.MonotonicClock
import phpvpn.android.bridge.model.ProgressModel
import phpvpn.android.bridge.model.ProgressStage
import phpvpn.android.bridge.model.StatisticsField
import phpvpn.android.bridge.model.StatisticsFreshness
import phpvpn.android.bridge.model.StatisticsModel
import phpvpn.android.bridge.respondFixture

/** ConnectionRepository, StatisticsRepository and ServerInfoRepository on virtual time. */
class RepositoriesTest {
    private fun TestScope.clock() = MonotonicClock { testScheduler.currentTime }

    private fun TestScope.connection(transport: FakeIpcTransport) =
        ConnectionRepository(IpcClient(transport, SequentialIds(), clock = clock()), backgroundScope, clock())

    private fun FakeIpcTransport.statusCount() = requests("get-status").size

    @Test
    fun connection_pollsOnlyWhileCollectedEveryFiveSecondsWhenIdle() = runTest {
        val transport = FakeIpcTransport()
        transport.respondFixture("get-status", "status-disconnected.json")
        val repository = connection(transport)
        advanceTimeBy(20_000)
        assertEquals(0, transport.statusCount())
        val collector = backgroundScope.launch { repository.status.collect {} }
        runCurrent()
        assertEquals(1, transport.statusCount())
        assertEquals(400, transport.requests.single().timeoutMs)
        advanceTimeBy(4_999); runCurrent()
        assertEquals(1, transport.statusCount())
        advanceTimeBy(1); runCurrent()
        assertEquals(2, transport.statusCount())
        val state = repository.status.value as ConnectionStatus.Available
        assertTrue(state.status.offline)
        collector.cancel()
        runCurrent()
        advanceTimeBy(30_000)
        assertEquals(2, transport.statusCount())
    }

    @Test
    fun connection_pollsEveryHalfSecondDuringTransitions() = runTest {
        for (fixture in listOf("status-stopping.json", "status-reconnecting.json", "status-fixture.json")) {
            val transport = FakeIpcTransport()
            transport.respondStatus(fixture)
            val repository = connection(transport)
            backgroundScope.launch { repository.status.collect {} }
            runCurrent()
            advanceTimeBy(500); runCurrent()
            assertEquals(fixture, 2, transport.statusCount())
            advanceTimeBy(1_000); runCurrent()
            assertEquals(fixture, 4, transport.statusCount())
        }
    }

    @Test
    fun connection_timeoutIsStatusUnavailableWithLastKnownHint() = runTest {
        val transport = FakeIpcTransport()
        transport.enqueueFixtureOnce("get-status", "status-connected.json")
        transport.respond("get-status") { FakeIpcTransport.Reply.Delayed(1_000, FakeIpcTransport.Reply.Text(Fixtures.text("status-connected.json"))) }
        val repository = connection(transport)
        backgroundScope.launch { repository.status.collect {} }
        runCurrent()
        val known = (repository.status.value as ConnectionStatus.Available).status
        assertEquals(ConnectionState.CONNECTED, known.state)
        advanceTimeBy(5_000); runCurrent()
        advanceTimeBy(400); runCurrent()
        val unavailable = repository.status.value as ConnectionStatus.Unavailable
        assertEquals(known, unavailable.lastKnown)
        // A malformed reply is equally "unavailable", never a new state.
        transport.respond("get-status") { FakeIpcTransport.Reply.Text("{}") }
        repository.refresh(); runCurrent()
        assertTrue(repository.status.value is ConnectionStatus.Unavailable)
    }

    @Test
    fun connection_refreshTriggersAnImmediateExtraRead() = runTest {
        val transport = FakeIpcTransport()
        transport.respondFixture("get-status", "status-disconnected.json")
        val repository = connection(transport)
        backgroundScope.launch { repository.status.collect {} }
        runCurrent()
        advanceTimeBy(1_000)
        repository.refresh(); runCurrent()
        assertEquals(2, transport.statusCount())
        // The next periodic read follows 5 s after the extra one.
        advanceTimeBy(4_999); runCurrent()
        assertEquals(2, transport.statusCount())
        advanceTimeBy(1); runCurrent()
        assertEquals(3, transport.statusCount())
    }

    @Test
    fun connection_connectProgressSucceedsAndClosesAfterThreeSeconds() = runTest {
        val transport = FakeIpcTransport()
        transport.respondFixture("get-status", "status-connected.json")
        transport.respondFixture("connect", "status-connected.json")
        val repository = connection(transport)
        backgroundScope.launch { repository.status.collect {} }
        runCurrent()
        val result = repository.connect()
        assertTrue(result is CommandResult.Accepted)
        assertEquals(30_000, transport.requests("connect").single().timeoutMs)
        val progress = repository.progress.value!!
        assertFalse(progress.disconnect)
        assertEquals(ProgressStage.SUCCEEDED, progress.view.stage)
        // While the progress is open the status is read every 500 ms.
        val before = transport.statusCount()
        runCurrent()
        advanceTimeBy(500); runCurrent()
        assertTrue(transport.statusCount() >= before + 2)
        repository.progressSuccessPresented()
        advanceTimeBy(2_999); repository.progressTick()
        assertFalse(repository.progress.value!!.view.closeRequested)
        advanceTimeBy(1); repository.progressTick()
        assertTrue(repository.progress.value!!.view.closeRequested)
    }

    @Test
    fun connection_connectErrorFreezesTheAllowlistedFailure() = runTest {
        val transport = FakeIpcTransport()
        val failed = Fixtures.text("status-diagnostic-base.json").replace(Fixtures.STATUS_ID, "\$ID")
            .replace("\"connectAttemptId\":\"\$ID\"", "\"connectAttemptId\":\"${Fixtures.STATUS_ID}\"")
        transport.respond("connect") { FakeIpcTransport.Reply.Text(failed) }
        transport.respond("get-status") { FakeIpcTransport.Reply.Text(failed) }
        val repository = connection(transport)
        assertTrue(repository.connect() is CommandResult.Accepted)
        val progress = repository.progress.value!!
        assertEquals(ProgressStage.FAILED, progress.view.stage)
        assertEquals("endpoint_unavailable_or_invalid", progress.view.errorCode)
        assertEquals(7uL, progress.connectionEpoch)
        transport.respondFixture("dismiss-connect-diagnostic", "ack.json")
        repository.dismissDiagnostic(7uL, Fixtures.STATUS_ID)
        assertEquals(1, transport.requests("dismiss-connect-diagnostic").size)
        assertTrue(repository.progress.value!!.view.closeRequested)
    }

    @Test
    fun connection_disconnectTimeoutIsUnconfirmedNeverSuccess() = runTest {
        val transport = FakeIpcTransport()
        transport.respond("disconnect") { FakeIpcTransport.Reply.Timeout }
        transport.respondFixture("get-status", "status-connected.json")
        val repository = connection(transport)
        assertEquals(CommandResult.Unconfirmed, repository.disconnect())
        val progress = repository.progress.value!!
        assertTrue(progress.disconnect)
        assertEquals(ProgressStage.FAILED, progress.view.stage)
        assertEquals(ProgressModel.DISCONNECT_UNCONFIRMED, progress.view.errorCode)
        // A confirmed idle disconnection succeeds.
        transport.respondFixture("disconnect", "status-disconnected.json")
        assertTrue(repository.disconnect() is CommandResult.Accepted)
        assertEquals(ProgressStage.SUCCEEDED, repository.progress.value!!.view.stage)
        transport.respondFixture("disconnect", "busy-classic.json")
        val busy = repository.disconnect() as CommandResult.Failed
        assertTrue(busy.error.busy)
    }

    @Test
    fun statistics_waitingCurrentAndLastKnown() = runTest {
        val transport = FakeIpcTransport()
        transport.respondStatus("status-fixture.json")
        val connection = connection(transport)
        val statistics = StatisticsRepository(connection, backgroundScope)
        assertEquals(StatisticsFreshness.WAITING, statistics.statistics.value.freshness)
        assertNull(statistics.statistics.value.values)
        backgroundScope.launch { statistics.statistics.collect {} }
        runCurrent()
        val current = statistics.statistics.value
        assertEquals(StatisticsFreshness.CURRENT, current.freshness)
        assertEquals(BigInteger.valueOf(3), current.values!![StatisticsField.TOTAL_POLLS])
        assertEquals(BigInteger.valueOf(579), current.values!![StatisticsField.TOTAL_SIZE])
        assertEquals(10, current.values!!.size)
        transport.respond("get-status") { FakeIpcTransport.Reply.Timeout }
        advanceTimeBy(1_000); runCurrent()
        val stale = statistics.statistics.value
        assertEquals(StatisticsFreshness.LAST_KNOWN, stale.freshness)
        assertEquals(current.values, stale.values)
        assertEquals(ConnectionPhase.HEALTH, (connection.status.value as ConnectionStatus.Unavailable).lastKnown!!.phase)
    }

    @Test
    fun statistics_formattingMatchesTheTray() {
        assertEquals("999 B", StatisticsModel.formatBytes(BigInteger.valueOf(999)))
        assertEquals("1.00 kB", StatisticsModel.formatBytes(BigInteger.valueOf(1000)))
        assertEquals("1.01 kB", StatisticsModel.formatBytes(BigInteger.valueOf(1005)))
        assertEquals("1.00 MB", StatisticsModel.formatBytes(BigInteger.valueOf(999_995)))
        val sum = BigInteger(ULong.MAX_VALUE.toString()) * BigInteger.TWO
        assertEquals("36893.49 PB", StatisticsModel.formatBytes(sum))
        assertEquals("1,234,567", StatisticsModel.englishInteger(BigInteger.valueOf(1_234_567)))
        val model = StatisticsModel()
        assertEquals("-", model.value(StatisticsField.BYTES_SENT))
    }

    @Test
    fun serverInfo_keepsLastSuccessOnFailureAndAutoRefreshesAfterCompletion() = runTest {
        val transport = FakeIpcTransport()
        transport.respond("get-server-info") {
            FakeIpcTransport.Reply.Delayed(200, FakeIpcTransport.Reply.Text(Fixtures.text("server-info.json")))
        }
        val repository = ServerInfoRepository(IpcClient(transport, SequentialIds()), backgroundScope)
        backgroundScope.launch { repository.state.collect {} }
        runCurrent()
        assertFalse(repository.latest.autoRefresh)
        advanceTimeBy(20_000)
        assertEquals(0, transport.requests.size)
        repository.refresh()
        val first = repository.latest.last
        assertNotNull(first)
        assertNull(repository.latest.failure)
        transport.respond("get-server-info") { FakeIpcTransport.Reply.Timeout }
        repository.refresh()
        assertEquals(first, repository.latest.last)
        assertEquals(ErrorCode.TIMEOUT.wireName, repository.latest.failure!!.code)
        assertTrue(repository.latest.failure!!.timedOut)
        transport.respondFixture("get-server-info", "server-info-incompatible.json")
        repository.refresh()
        assertEquals("server_version_incompatible", repository.latest.failure!!.code)
        assertEquals(first, repository.latest.last)

        transport.clearRequests()
        transport.respond("get-server-info") {
            FakeIpcTransport.Reply.Delayed(200, FakeIpcTransport.Reply.Text(Fixtures.text("server-info.json")))
        }
        repository.setAutoRefresh(true)
        runCurrent()
        advanceTimeBy(4_999); runCurrent()
        assertEquals(0, transport.requests.size)
        advanceTimeBy(1); runCurrent()
        assertEquals(1, transport.requests.size)
        // The next refresh is due 5 s after the previous one completed (200 ms later).
        advanceTimeBy(5_199); runCurrent()
        assertEquals(1, transport.requests.size)
        advanceTimeBy(1); runCurrent()
        assertEquals(2, transport.requests.size)
        repository.setAutoRefresh(false)
        advanceTimeBy(60_000)
        assertEquals(2, transport.requests.size)
    }

    @Test
    fun serverInfo_nativeHookMayRejectAndAutoRefreshStopsWithoutCollector() = runTest {
        val transport = FakeIpcTransport()
        transport.respondFixture("get-server-info", "server-info.json")
        var accept = true
        val repository = ServerInfoRepository(IpcClient(transport, SequentialIds()), backgroundScope) { accept }
        repository.refresh()
        val first = repository.latest.last!!
        accept = false
        repository.refresh()
        assertEquals(first, repository.latest.last)
        assertEquals(ErrorCode.RESPONSE_INVALID.wireName, repository.latest.failure!!.code)
        repository.setAutoRefresh(true)
        advanceTimeBy(30_000)
        assertEquals(2, transport.requests.size) // no collector, no timer
        assertEquals(ServerInfoState(), repository.state.first().copy(last = null, failure = null, autoRefresh = false))
    }
}

/** tray_fixture.h carries a fixed requestId; correlate it with the request. */
private fun FakeIpcTransport.respondStatus(fixture: String) = respond("get-status") {
    FakeIpcTransport.Reply.Text(Fixtures.text(fixture).replaceFirst(Fixtures.STATUS_ID, "\$ID"))
}

private fun FakeIpcTransport.enqueueFixtureOnce(command: String, fixture: String) =
    enqueue(command, { FakeIpcTransport.Reply.Text(Fixtures.text(fixture)) })
