package phpvpn.android.bridge.model

import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.Fixtures
import phpvpn.android.bridge.edit
import phpvpn.android.bridge.errors.ConnectionErrorCode
import phpvpn.android.bridge.ipc.CheckpointResult
import phpvpn.android.bridge.ipc.ConnectionPhase
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.Tristate
import phpvpn.android.bridge.number
import phpvpn.android.bridge.serialized
import phpvpn.android.bridge.with

/**
 * Every state x phase x four-checkpoint combination of core/runtime/status_snapshot.h
 * through the wire parser, the indicator and the Connect progress, each with
 * and without `connectionStartedAt`, plus every tristate value.
 */
class StatusMatrixTest {
    private val base = Fixtures.json("status-disconnected.json", Fixtures.STATUS_ID)
    private val checkpointNames = listOf("serverUrlCheck", "serverSecretCheck", "accessKeyCheck", "communicationContractsCheck")
    private val checkpointWire = mapOf(CheckpointResult.PENDING to "pending", CheckpointResult.SUCCESS to "success", CheckpointResult.FAILED to "failed")

    private fun combinations(): List<List<CheckpointResult>> {
        val values = CheckpointResult.entries
        val result = mutableListOf<List<CheckpointResult>>()
        for (a in values) for (b in values) for (c in values) for (d in values) result += listOf(a, b, c, d)
        return result
    }

    @Test
    fun everyStatePhaseAndCheckpointCombination() {
        var checked = 0
        for (state in ConnectionState.entries) for (phase in ConnectionPhase.entries) for (checks in combinations()) for (started in listOf(false, true)) {
            val epoch = if (state == ConnectionState.DISCONNECTED) 0 else 5
            val document = base.edit("status") { status ->
                var s = status.with("state", state.wireName).with("phase", phase.wireName).with("tunnelReady", "yes")
                    .with("connectionEpoch", number(epoch.toString()))
                    .with("connectAttemptId", if (epoch == 0) JsonNull else JsonPrimitive(Fixtures.STATUS_ID))
                    .with("connectionStartedAt", if (started) JsonPrimitive("2026-09-04T18:52:30.000Z") else JsonNull)
                checkpointNames.forEachIndexed { i, name -> s = s.with(name, checkpointWire.getValue(checks[i])) }
                s
            }
            val parsed = StatusModel.parseStatusResponse(document.serialized())
            val consistent = when (state) {
                ConnectionState.CONNECTED -> started
                ConnectionState.DISCONNECTED, ConnectionState.CONNECTING -> !started
                else -> true
            }
            assertEquals("$state/$phase/$checks/$started", consistent, parsed.valid)
            checked++
            if (!parsed.valid) continue
            assertEquals(state, parsed.state)
            assertEquals(phase, parsed.phase)
            assertEquals(checks, listOf(parsed.serverUrl, parsed.serverSecret, parsed.accessKey, parsed.communicationContracts))

            val expectedIndicator = when {
                phase == ConnectionPhase.STOPPING || state == ConnectionState.CONNECTING || state == ConnectionState.RECOVERING ||
                    (state == ConnectionState.CONNECTED && phase == ConnectionPhase.RECONNECTING) -> ConnectionIndicator.CONNECTING
                state == ConnectionState.CONNECTED && phase == ConnectionPhase.RUNNING -> ConnectionIndicator.CONNECTED
                else -> ConnectionIndicator.DISCONNECTED
            }
            assertEquals(expectedIndicator, StatusModel.selectIndicator(parsed, false, false))
            assertEquals(ConnectionIndicator.DISCONNECTED, StatusModel.selectIndicator(parsed, true, false))

            val progress = ProgressModel()
            progress.onSnapshot(parsed, 0)
            when {
                state == ConnectionState.CONNECTED && phase == ConnectionPhase.RUNNING ->
                    assertEquals(ProgressStage.SUCCEEDED, progress.view.stage)
                state == ConnectionState.ERROR -> {
                    assertEquals(ProgressStage.FAILED, progress.view.stage)
                    // No lastError code: the allowlist falls back to communication_failed.
                    assertEquals(ConnectionErrorCode.COMMUNICATION_FAILED.wireName, progress.view.errorCode)
                }
                state == ConnectionState.DISCONNECTED && phase == ConnectionPhase.IDLE -> assertTrue(progress.view.closeRequested)
                else -> {
                    assertEquals(ProgressStage.CONNECTING, progress.view.stage)
                    assertEquals(checks, listOf(progress.view.serverUrl, progress.view.serverSecret, progress.view.accessKey, progress.view.communicationContracts))
                }
            }
            val disconnect = ProgressModel(disconnect = true)
            disconnect.onSnapshot(parsed.copy(tunnelReady = Tristate.NO), 0)
            val confirmed = state == ConnectionState.DISCONNECTED && phase == ConnectionPhase.IDLE
            assertEquals(if (confirmed) ProgressStage.SUCCEEDED else ProgressStage.CONNECTING, disconnect.view.stage)
            assertEquals(
                if (state == ConnectionState.ERROR && phase != ConnectionPhase.STOPPING) ProgressModel.DISCONNECT_UNCONFIRMED else "",
                disconnect.view.errorCode,
            )
        }
        assertEquals(5 * 9 * 81 * 2, checked)
    }

    @Test
    fun everyTristateOfReachabilityVerificationAndTunnel() {
        for (key in listOf("serverReachable", "serverVerified", "tunnelReady")) {
            for ((wire, value) in listOf("unknown" to Tristate.UNKNOWN, "yes" to Tristate.YES, "no" to Tristate.NO)) {
                val parsed = StatusModel.parseStatusResponse(base.edit("status") { it.with(key, wire) }.serialized())
                assertTrue(parsed.valid)
                if (key == "tunnelReady") assertEquals(value, parsed.tunnelReady)
            }
            assertFalse(StatusModel.parseStatusResponse(base.edit("status") { it.with(key, "maybe") }.serialized()).valid)
        }
    }

    @Test
    fun epochAndAttemptIdentityMustAgree() {
        val withEpoch = base.edit("status") { it.with("connectionEpoch", number("3")) }
        assertFalse(StatusModel.parseStatusResponse(withEpoch.serialized()).valid)
        val withAttempt = base.edit("status") { it.with("connectAttemptId", Fixtures.STATUS_ID) }
        assertFalse(StatusModel.parseStatusResponse(withAttempt.serialized()).valid)
        val upper = base.edit("status") { it.with("connectionEpoch", number("3")).with("connectAttemptId", Fixtures.STATUS_ID.uppercase()) }
        assertFalse(StatusModel.parseStatusResponse(upper.serialized()).valid)
        val both = base.edit("status") { it.with("connectionEpoch", number("3")).with("connectAttemptId", Fixtures.STATUS_ID) }
        assertTrue(StatusModel.parseStatusResponse(both.serialized()).valid)
        // A fractional or string epoch is not an unsigned integer.
        for (bad in listOf(number("3.0"), JsonPrimitive("3"), number("1e1"))) {
            assertFalse(StatusModel.parseStatusResponse(base.edit("status") { it.with("connectionEpoch", bad) }.serialized()).valid)
        }
        assertTrue((base["status"] as JsonObject).size == 26)
    }
}
