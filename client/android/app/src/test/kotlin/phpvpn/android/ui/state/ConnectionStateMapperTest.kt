package phpvpn.android.ui.state

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.errors.ConnectionErrorCode
import phpvpn.android.bridge.errors.ErrorTextKey
import phpvpn.android.bridge.errors.ErrorTexts
import phpvpn.android.bridge.ipc.CheckpointResult
import phpvpn.android.bridge.model.ProgressModel
import phpvpn.android.bridge.model.StatusModel
import phpvpn.android.bridge.model.TrayStatus
import phpvpn.android.bridge.repository.ConnectionStatus
import phpvpn.android.bridge.repository.OperationProgress
import phpvpn.android.testing.IpcReplies
import phpvpn.android.ui.components.CheckState

/** Mapping of get-status and the own progress to the Connection variants (01-08, 37, 38). */
class ConnectionStateMapperTest {
    private fun parse(text: String): TrayStatus =
        StatusModel.parseStatusResponse(text.replace("\$ID", "00000000-0000-4000-8000-000000000001")).also {
            assertTrue("fixture must be a valid status", it.valid)
        }

    private fun available(text: String) = ConnectionStatus.Available(parse(text))

    private fun map(status: ConnectionStatus, progress: OperationProgress? = null, reason: DisconnectReason? = null) =
        ConnectionStateMapper.map(status, progress, reason, lastKnownAtUtcMs = 1_000L)

    private fun progress(disconnect: Boolean, vararg snapshots: TrayStatus, commandFailure: Boolean = false): OperationProgress {
        val model = ProgressModel(disconnect)
        snapshots.forEach { model.onSnapshot(it, 0) }
        if (commandFailure) model.onCommandFailure()
        return OperationProgress(disconnect, model.view, model.httpDiagnostic, model.connectionEpoch, model.connectAttemptId)
    }

    @Test
    fun loadingClaimsNothing() {
        assertEquals(ConnectionUiState.Loading, map(ConnectionStatus.Loading))
    }

    @Test
    fun connectedOnlyForConfirmedRunningTunnel() {
        val counters = IpcReplies.Counters(bytesReceived = 18_420_000, packetsReceived = 14_826, bytesSent = 4_160_000, packetsSent = 8_204)
        val state = map(available(IpcReplies.connected(counters)))
        state as ConnectionUiState.Connected
        assertEquals(1_767_225_600_000L, state.startedAtUtcMs)
        assertEquals(18_420_000uL, state.counters.ipv4BytesReceived)
        assertEquals(8_204uL, state.counters.ipv4PacketsSent)
        // Connected without a ready tunnel is not 01.
        val notReady = map(available(IpcReplies.status("connected", "running", checks = IpcReplies.ALL_PASSED, tunnelReady = "unknown")))
        assertTrue(notReady is ConnectionUiState.Reconnecting)
    }

    @Test
    fun reconnectingAndRecoveringAreDistinctFromConnected() {
        assertEquals(ConnectionUiState.Reconnecting(null), map(available(IpcReplies.reconnecting())))
        val recovering = map(
            available(IpcReplies.status("recovering", "health", checks = IpcReplies.ALL_PASSED, tunnelReady = "no", errorCode = "communication_timeout", recoverable = true)),
        )
        assertEquals(ConnectionUiState.Reconnecting(ErrorTexts.messageKey("communication_timeout")), recovering)
    }

    @Test
    fun stoppingIsDisconnectingAndIdleIsDisconnectedWithReason() {
        assertEquals(ConnectionUiState.Disconnecting, map(available(IpcReplies.stopping())))
        assertEquals(ConnectionUiState.Disconnected(null), map(available(IpcReplies.disconnected())))
        assertEquals(
            ConnectionUiState.Disconnected(DisconnectReason.REVOKED),
            map(available(IpcReplies.disconnected()), reason = DisconnectReason.REVOKED),
        )
        assertEquals(
            ConnectionUiState.Disconnected(DisconnectReason.PROCESS_RESTARTED),
            map(available(IpcReplies.disconnected()), reason = DisconnectReason.PROCESS_RESTARTED),
        )
    }

    @Test
    fun connectingShowsFourChecksInOrderAndNeverAPendingAsPassed() {
        val state = map(available(IpcReplies.connecting())) as ConnectionUiState.Connecting
        assertEquals(listOf(CheckState.Done, CheckState.Done, CheckState.Pending, CheckState.Off), state.checks)
        assertFalse(state.statusUnavailable)
        val first = map(available(IpcReplies.connecting(List(4) { "pending" }))) as ConnectionUiState.Connecting
        assertEquals(listOf(CheckState.Pending, CheckState.Off, CheckState.Off, CheckState.Off), first.checks)
    }

    @Test
    fun failureSeparatesTransportFromInputAndShowsDiagnosticOnlyWhenCaptured() {
        val transport = map(available(IpcReplies.failed())) as ConnectionUiState.Failed
        assertFalse(transport.inputError)
        assertFalse(transport.diagnostic)
        assertEquals(ConnectionErrorCode.ENDPOINT_UNAVAILABLE_OR_INVALID.title, transport.error.panel)
        assertEquals(listOf(CheckState.Fail, CheckState.Off, CheckState.Off, CheckState.Off), transport.checks)

        val withDiagnostic = map(available(IpcReplies.failed(diagnostic = true))) as ConnectionUiState.Failed
        assertTrue(withDiagnostic.diagnostic)

        val input = map(available(IpcReplies.failed("access_key_rejected"))) as ConnectionUiState.Failed
        assertTrue(input.inputError)
        assertEquals(ConnectionErrorCode.ACCESS_KEY_REJECTED.message, input.error.description)

        // A timeout is not evidence of a wrong secret.
        val timeout = map(available(IpcReplies.failed("communication_timeout"))) as ConnectionUiState.Failed
        assertFalse(timeout.inputError)

        // Unknown codes never inject text: they fall back to communication_failed.
        val unknown = map(available(IpcReplies.failed("made_up_code"))) as ConnectionUiState.Failed
        assertEquals(ConnectionErrorCode.COMMUNICATION_FAILED.wireName, unknown.error.code)
    }

    @Test
    fun permissionMissingIsNotAServerError() {
        val state = map(available(IpcReplies.failed("tunnel_permission_missing")))
        assertEquals(ConnectionUiState.PermissionMissing, state)
    }

    @Test
    fun unavailableKeepsOnlyAHint() {
        assertEquals(ConnectionUiState.Unavailable(null), map(ConnectionStatus.Unavailable(null)))
        val connected = parse(IpcReplies.connected())
        val state = map(ConnectionStatus.Unavailable(connected)) as ConnectionUiState.Unavailable
        assertEquals(connected, state.lastKnown?.status)
        assertEquals(1_000L, state.lastKnown?.seenAtUtcMs)
    }

    @Test
    fun ownConnectProgressDrivesConnectingAndFailure() {
        val connecting = parse(IpcReplies.connecting())
        val open = progress(false, connecting)
        val state = map(ConnectionStatus.Available(connecting), open) as ConnectionUiState.Connecting
        assertEquals(CheckState.Pending, state.checks[2])

        // A failed read during the attempt is "not confirmed", never a result.
        val unavailable = progress(false, connecting, TrayStatus())
        val shown = map(ConnectionStatus.Unavailable(connecting), unavailable) as ConnectionUiState.Connecting
        assertTrue(shown.statusUnavailable)

        val failed = parse(IpcReplies.failed(diagnostic = true))
        val failure = map(ConnectionStatus.Available(failed), progress(false, connecting, failed)) as ConnectionUiState.Failed
        assertTrue(failure.diagnostic)
        assertEquals("endpoint_unavailable_or_invalid", failure.error.code)

        // Success is the confirmed status itself.
        val connected = parse(IpcReplies.connected())
        assertTrue(map(ConnectionStatus.Available(connected), progress(false, connecting, connected)) is ConnectionUiState.Connected)
    }

    @Test
    fun ownDisconnectProgressWaitsAndNeverFakesSuccess() {
        val connected = parse(IpcReplies.connected())
        val stopping = parse(IpcReplies.stopping())
        assertEquals(ConnectionUiState.Disconnecting, map(ConnectionStatus.Available(connected), progress(true, connected)))
        assertEquals(ConnectionUiState.Disconnecting, map(ConnectionStatus.Available(stopping), progress(true, stopping)))
        // Disconnect reply timed out: 38.
        assertEquals(
            ConnectionUiState.DisconnectUnconfirmed,
            map(ConnectionStatus.Available(connected), progress(true, connected, commandFailure = true)),
        )
        // A status read failure while waiting: 38 as on the desktop.
        assertEquals(
            ConnectionUiState.DisconnectUnconfirmed,
            map(ConnectionStatus.Unavailable(connected), progress(true, connected, TrayStatus())),
        )
        // Confirmed disconnected: 02.
        val offline = parse(IpcReplies.disconnected())
        assertEquals(ConnectionUiState.Disconnected(null), map(ConnectionStatus.Available(offline), progress(true, stopping, offline)))
    }

    @Test
    fun lastChecksOfScreen48() {
        assertNull(ConnectionStateMapper.lastChecks(ConnectionStatus.Loading))
        assertEquals(List(4) { CheckState.Done }, ConnectionStateMapper.lastChecks(available(IpcReplies.connected())))
        assertEquals(
            listOf(CheckState.Fail, CheckState.Off, CheckState.Off, CheckState.Off),
            ConnectionStateMapper.lastChecks(ConnectionStatus.Unavailable(parse(IpcReplies.failed()))),
        )
    }

    @Test
    fun checksHelperCoversEveryCombination() {
        val values = CheckpointResult.entries
        for (a in values) for (b in values) for (c in values) for (d in values) {
            val results = listOf(a, b, c, d)
            for (settled in listOf(true, false)) {
                val checks = ConnectionStateMapper.checks(results, settled)
                results.forEachIndexed { i, result ->
                    when (result) {
                        CheckpointResult.SUCCESS -> assertEquals(CheckState.Done, checks[i])
                        CheckpointResult.FAILED -> assertEquals(CheckState.Fail, checks[i])
                        CheckpointResult.PENDING -> assertTrue(checks[i] == CheckState.Pending || checks[i] == CheckState.Off)
                    }
                }
                val active = checks.count { it == CheckState.Pending }
                assertTrue(active <= 1)
                if (settled || CheckpointResult.FAILED in results) assertEquals(0, active)
            }
        }
    }

    @Test
    fun inputErrorsAreTheVerificationCategories() {
        assertEquals(
            setOf("server_verification_failed", "access_key_rejected", "communication_contracts_verification_failed", "server_version_incompatible"),
            ConnectionStateMapper.INPUT_ERRORS,
        )
        assertTrue(ErrorTextKey.entries.isNotEmpty())
    }
}
