package phpvpn.android.bridge.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.errors.ErrorTextKey
import phpvpn.android.bridge.ipc.CheckpointResult
import phpvpn.android.bridge.ipc.ConnectionPhase
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.IpcTimeouts
import phpvpn.android.bridge.ipc.Tristate

/**
 * 1:1 port of client/tests/unit/test_tray_progress.cpp. Where the C++ test
 * compares the English description of the allowlist, this port compares the
 * allowlisted code and its resource key; the received description never
 * reaches the view in either implementation.
 */
class TrayProgressTest {
    private fun connecting() = TrayStatus(valid = true, state = ConnectionState.CONNECTING)

    @Test
    fun TrayProgress_ReconstructsSkippedPhasesButSucceedsOnlyWhenConnected() {
        val model = ProgressModel()
        assertEquals(ProgressStage.CONNECTING, model.view.stage)
        var status = connecting().copy(serverUrl = CheckpointResult.SUCCESS, serverSecret = CheckpointResult.SUCCESS)
        model.onSnapshot(status, 500)
        assertEquals(CheckpointResult.SUCCESS, model.view.serverSecret)
        assertEquals(CheckpointResult.PENDING, model.view.accessKey)
        status = status.copy(accessKey = CheckpointResult.SUCCESS, state = ConnectionState.RECOVERING)
        model.onSnapshot(status, 1000)
        assertEquals(ProgressStage.CONNECTING, model.view.stage)
        status = status.copy(state = ConnectionState.CONNECTED, phase = ConnectionPhase.RUNNING, tunnelReady = Tristate.YES)
        model.onSnapshot(status, 1500)
        assertEquals(ProgressStage.SUCCEEDED, model.view.stage)
        model.onTick(9000)
        assertFalse(model.view.closeRequested)
        model.onSuccessPresented(1500)
        model.onTick(4499)
        assertFalse(model.view.closeRequested)
        model.onTick(4500)
        assertTrue(model.view.closeRequested)
    }

    @Test
    fun TrayProgress_ReplacesWholeCheckpointTripleIncludingNewPending() {
        val model = ProgressModel()
        var status = connecting().copy(serverUrl = CheckpointResult.SUCCESS, serverSecret = CheckpointResult.SUCCESS)
        model.onSnapshot(status, 0)
        model.onSnapshot(TrayStatus(), 50)
        assertEquals(CheckpointResult.SUCCESS, model.view.serverSecret)
        status = status.copy(serverUrl = CheckpointResult.PENDING, serverSecret = CheckpointResult.PENDING)
        model.onSnapshot(status, 100)
        assertEquals(CheckpointResult.PENDING, model.view.serverUrl)
        assertEquals(CheckpointResult.PENDING, model.view.serverSecret)
        status = status.copy(state = ConnectionState.RECOVERING, accessKey = CheckpointResult.FAILED)
        model.onSnapshot(status, 200)
        assertEquals(ProgressStage.CONNECTING, model.view.stage)
    }

    @Test
    fun TrayProgress_AllSafeFailureCategoriesFreezeUntilManualClose() {
        data class Failure(val code: String, val text: String, val failedIndex: Int, val key: ErrorTextKey)
        val failures = listOf(
            Failure("endpoint_unavailable_or_invalid", "The server endpoint is not reachable or not valid.", 0, ErrorTextKey.CONNECTION_ENDPOINT_UNAVAILABLE_OR_INVALID_MESSAGE),
            Failure("server_verification_failed", "The server could not be verified with the configured SERVER_SECRET.", 1, ErrorTextKey.CONNECTION_SERVER_VERIFICATION_FAILED_MESSAGE),
            Failure("access_key_rejected", "The server rejected the configured ACCESS_KEY.", 2, ErrorTextKey.CONNECTION_ACCESS_KEY_REJECTED_MESSAGE),
            Failure("tunnel_start_failed", "The local tunnel could not be started.", 3, ErrorTextKey.CONNECTION_TUNNEL_START_FAILED_MESSAGE),
            Failure("communication_failed", "Communication with the server could not be completed.", 2, ErrorTextKey.CONNECTION_COMMUNICATION_FAILED_MESSAGE),
            Failure("communication_timeout", "The server did not respond in time. Verification could not be completed.", 0, ErrorTextKey.CONNECTION_COMMUNICATION_TIMEOUT_MESSAGE),
            Failure("communication_timeout", "The server did not respond in time. Verification could not be completed.", 1, ErrorTextKey.CONNECTION_COMMUNICATION_TIMEOUT_MESSAGE),
            Failure("communication_timeout", "The server did not respond in time. Verification could not be completed.", 2, ErrorTextKey.CONNECTION_COMMUNICATION_TIMEOUT_MESSAGE),
            Failure("server_version_incompatible", "The server uses a different major version than this client.", 0, ErrorTextKey.CONNECTION_SERVER_VERSION_INCOMPATIBLE_MESSAGE),
        )
        for (failure in failures) {
            val model = ProgressModel()
            val checks = (0 until 3).map {
                when {
                    it < failure.failedIndex -> CheckpointResult.SUCCESS
                    it == failure.failedIndex -> CheckpointResult.FAILED
                    else -> CheckpointResult.PENDING
                }
            }
            var status = connecting().copy(
                state = ConnectionState.RECOVERING, hasError = true, errorCode = failure.code, errorDescription = failure.text,
                serverUrl = checks[0], serverSecret = checks[1], accessKey = checks[2],
            )
            model.onSnapshot(status, 500)
            assertEquals(ProgressStage.FAILED, model.view.stage)
            assertEquals(failure.code, model.view.errorCode)
            assertEquals(failure.key, model.view.errorText!!.description)
            assertEquals(status.serverUrl, model.view.serverUrl)
            assertEquals(status.serverSecret, model.view.serverSecret)
            assertEquals(status.accessKey, model.view.accessKey)
            model.onTick(99_999_999)
            assertFalse(model.view.closeRequested)
            status = status.copy(state = ConnectionState.CONNECTED, hasError = false)
            model.onSnapshot(status, 100_000_000)
            assertEquals(ProgressStage.FAILED, model.view.stage)
            assertEquals(failure.key, model.view.errorText!!.description)
            // The view holds only an allowlisted code and resource keys, never received text.
            for (forbidden in listOf("https://", "ciphertext", "C:\\", "DNS", "TLS", "10.0.")) {
                assertFalse(model.view.toString().contains(forbidden))
            }
            model.onClose()
            assertTrue(model.view.closeRequested)
        }
    }

    @Test
    fun TrayProgress_FinalSuccessFreezesAndNewConnectResetsPresentation() {
        var model = ProgressModel()
        var status = connecting().copy(state = ConnectionState.CONNECTED, phase = ConnectionPhase.RUNNING, tunnelReady = Tristate.YES)
        model.onSnapshot(status, 1000)
        status = status.copy(state = ConnectionState.ERROR)
        model.onSnapshot(status, 1500)
        assertEquals(ProgressStage.SUCCEEDED, model.view.stage)
        model = ProgressModel()
        assertEquals(ProgressStage.CONNECTING, model.view.stage)
        assertEquals(CheckpointResult.PENDING, model.view.serverUrl)
        assertFalse(model.view.closeRequested)
    }

    @Test
    fun TrayProgress_ClosingStopsOnlyLocalReadsAndRemoteDisconnectClosesPendingWindow() {
        var model = ProgressModel()
        val progress = PollSchedule(IpcTimeouts.PROGRESS_INTERVAL_MS)
        progress.start(0)
        model.onSnapshot(connecting(), 0)
        model.onClose()
        progress.stop()
        assertTrue(model.view.closeRequested)
        assertFalse(progress.onTimer(500))
        assertFalse(progress.onTimer(5000))
        // The model and schedule expose no command or transport writer capability.
        model = ProgressModel()
        var disconnected = connecting().copy(state = ConnectionState.DISCONNECTED, phase = ConnectionPhase.IDLE)
        model.onSnapshot(disconnected, 500)
        assertTrue(model.view.closeRequested)
        model = ProgressModel(disconnect = true)
        model.onSnapshot(TrayStatus(), 500)
        model.onTick(999_999)
        assertFalse(model.view.closeRequested)
        model.onCommandFailure()
        disconnected = disconnected.copy(tunnelReady = Tristate.NO)
        model.onSnapshot(disconnected, 1_000_000)
        assertEquals(ProgressStage.FAILED, model.view.stage)
        assertNull(model.successDeadlineMs)
    }

    @Test
    fun TrayPolling_ProgressAndExtraStatisticsReadNeverMoveFiveSecondDeadlines() {
        val tray = PollSchedule(IpcTimeouts.STATUS_INTERVAL_MS)
        val progress = PollSchedule(IpcTimeouts.PROGRESS_INTERVAL_MS)
        tray.start(0)
        progress.start(1200)
        val baseRequests = mutableListOf<Long>()
        val progressRequests = mutableListOf<Long>()
        var extraStatisticsRequests = 0
        var now = 0L
        while (now <= 15000) {
            if (now == 2300L) extraStatisticsRequests++ // Same direct read used by menu.
            if (now == 7200L) progress.stop()
            if (tray.onTimer(now)) baseRequests += now
            if (progress.onTimer(now)) progressRequests += now
            now += 100
        }
        assertEquals(listOf(5000L, 10000L, 15000L), baseRequests)
        assertEquals(listOf(1700L, 2200L, 2700L, 3200L, 3700L, 4200L, 4700L, 5200L, 5700L, 6200L, 6700L), progressRequests)
        assertEquals(1, extraStatisticsRequests)
        assertEquals(20000L, tray.nextMs)
        assertFalse(progress.active)
    }

    @Test
    fun TrayPolling_StatisticsAndCommandReadsQueueBehindBusyPeriodicRead() {
        val queue = ReadRequestQueue()
        val tray = PollSchedule(IpcTimeouts.STATUS_INTERVAL_MS)
        tray.start(0)
        assertTrue(tray.onTimer(5000))
        assertTrue(queue.request()) // Worker starts for regular status.
        assertFalse(queue.request()) // Statistics opening queues a real read.
        assertFalse(queue.request()) // A command completion queues another.
        assertTrue(queue.finish()) // Worker must perform Statistics read.
        assertTrue(queue.finish()) // Worker must perform post-command read.
        assertFalse(queue.finish())
        assertEquals(10000L, tray.nextMs)
        assertTrue(queue.request()) // Next periodic read starts normally.
        queue.stop()
        assertTrue(queue.request()) // Reopening gets a fresh queue.
        assertFalse(queue.finish())
    }
}
