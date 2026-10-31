package phpvpn.android.bridge.model

import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.Fixtures
import phpvpn.android.bridge.edit
import phpvpn.android.bridge.ipc.ConnectionPhase
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.Tristate
import phpvpn.android.bridge.number
import phpvpn.android.bridge.serialized
import phpvpn.android.bridge.with
import phpvpn.android.bridge.without

/**
 * 1:1 port of client/tests/unit/test_tray_status.cpp. Test names keep the
 * C++ names so the mapping is traceable; the fixture is tray_fixture.h
 * kStatusFixture (ipc/status-fixture.json).
 */
class TrayStatusTest {
    private val fixtureText = Fixtures.text("status-fixture.json")
    private fun fixture(): JsonObject = Fixtures.json("status-fixture.json")
    private fun JsonObject.status(edit: (JsonObject) -> JsonObject) = edit("status", edit = edit)
    private fun parse(document: JsonObject) = StatusModel.parseStatusResponse(document.serialized())

    @Test
    fun TrayStatus_ParsesWholeFixtureAndReplacesOnlyValidSnapshots() {
        var current = StatusModel.parseStatusResponse(fixtureText)
        assertTrue(current.valid)
        assertEquals(3uL, current.counters.dataPollsTotal)
        assertEquals(456uL, current.counters.serializedDataRequestBodyBytes)
        for (text in listOf("", "{}", "null", "[]", "{", """{"error":{"description":"unavailable"}}""")) {
            val invalid = StatusModel.parseStatusResponse(text)
            assertFalse(invalid.valid)
            assertNull(StatusModel.applyStatusSnapshot(current, invalid))
            current = StatusModel.applyStatusSnapshot(current, invalid) ?: current
            assertEquals(3uL, current.counters.dataPollsTotal)
        }
    }

    @Test
    fun TrayStatus_EveryRequiredMemberIsRequiredAndUnknownMembersAreRejected() {
        val names = fixture()["status"]!!.let { (it as JsonObject).keys }
        for (name in names) {
            assertFalse(name, parse(fixture().status { it.without(name) }).valid)
        }
        for (old in listOf("packetsSent", "packetsReceived", "serverUrl", "configuration")) {
            assertFalse(old, parse(fixture().status { it.with(old, JsonPrimitive(0)) }).valid)
        }
    }

    @Test
    fun TrayStatus_RejectsNegativeFractionalStringNullAndOverflowCounters() {
        val invalid = listOf(number("-1"), number("0.5"), JsonPrimitive("9"), JsonNull, JsonPrimitive(true))
        for (key in TrafficCounters.KEYS) {
            for (value in invalid) {
                assertFalse("$key=$value", parse(fixture().status { it.with(key, value) }).valid)
            }
        }
        val oversized = fixtureText.replaceFirst("456", "18446744073709551616")
        assertFalse(StatusModel.parseStatusResponse(oversized).valid)
        assertFalse(parse(fixture().status { it.with("dataPollsTotal", number("4")) }).valid)
    }

    @Test
    fun TrayStatus_RejectsInvalidEnumsVersionsAndMalformedError() {
        for (key in listOf("state", "phase", "serverUrlCheck", "serverSecretCheck", "accessKeyCheck", "serverReachable", "serverVerified", "tunnelReady")) {
            assertFalse(key, parse(fixture().status { it.with(key, "not-a-valid-value") }).valid)
        }
        assertFalse(parse(fixture().with("requestId", "not-an-uuid")).valid)
        assertFalse(parse(fixture().status { it.with("stateChangedAt", "not-a-time") }).valid)
        assertFalse(parse(fixture().status { it.with("connectionStartedAt", "not-a-time") }).valid)
        assertFalse(parse(fixture().with("ipcVersion", number("2"))).valid)
        assertFalse(parse(fixture().status { it.with("lastError", JsonObject(mapOf("code" to JsonPrimitive("communication_failed")))) }).valid)
        assertFalse(StatusModel.parseStatusResponse("""{"ipcVersion":1,"ipcVersion":1}""").valid)
    }

    @Test
    fun TrayStatus_PreservesPublicServiceErrorWithoutCheckpointInference() {
        val error = JsonObject(
            mapOf(
                "code" to JsonPrimitive("communication_timeout"),
                "description" to JsonPrimitive("The server did not respond in time. Verification could not be completed."),
                "recoverable" to JsonPrimitive(true),
            ),
        )
        val parsed = parse(fixture().status { it.with("lastError", error) })
        assertTrue(parsed.valid)
        assertTrue(parsed.hasError)
        assertEquals("communication_timeout", parsed.errorCode)
        assertEquals("The server did not respond in time. Verification could not be completed.", parsed.errorDescription)
        assertTrue(parsed.errorRecoverable)
    }

    @Test
    fun TrayStatus_IconMappingRequiresHealthyConnectedAndHonorsLocalOperation() {
        var status = TrayStatus(valid = true, state = ConnectionState.CONNECTED, phase = ConnectionPhase.RUNNING, tunnelReady = Tristate.YES)
        assertEquals(ConnectionIndicator.CONNECTED, StatusModel.selectIndicator(status, false, false))
        assertEquals(ConnectionIndicator.CONNECTING, StatusModel.selectIndicator(status, false, true))
        assertEquals(ConnectionIndicator.DISCONNECTED, StatusModel.selectIndicator(status, true, true))
        status = status.copy(valid = false)
        assertEquals(ConnectionIndicator.DISCONNECTED, StatusModel.selectIndicator(status, false, true))
        status = status.copy(valid = true, hasError = true)
        assertEquals(ConnectionIndicator.DISCONNECTED, StatusModel.selectIndicator(status, false, false))
        status = status.copy(hasError = false, tunnelReady = Tristate.NO)
        assertEquals(ConnectionIndicator.DISCONNECTED, StatusModel.selectIndicator(status, false, false))
        status = status.copy(phase = ConnectionPhase.STOPPING)
        assertEquals(ConnectionIndicator.CONNECTING, StatusModel.selectIndicator(status, false, false))
        // Soft recovery: the tunnel is up but the exchange is being retried.
        status = status.copy(phase = ConnectionPhase.RECONNECTING, tunnelReady = Tristate.YES)
        assertEquals(ConnectionIndicator.CONNECTING, StatusModel.selectIndicator(status, false, false))
        status = status.copy(tunnelReady = Tristate.NO, phase = ConnectionPhase.IDLE)
        for (state in listOf(ConnectionState.CONNECTING, ConnectionState.RECOVERING)) {
            assertEquals(ConnectionIndicator.CONNECTING, StatusModel.selectIndicator(status.copy(state = state), false, false))
        }
        // The C++ case "unknown" is an unparsed state string; here it is the null state of an invalid parse.
        for (state in listOf(ConnectionState.DISCONNECTED, ConnectionState.ERROR, null, ConnectionState.CONNECTED)) {
            assertEquals(ConnectionIndicator.DISCONNECTED, StatusModel.selectIndicator(status.copy(state = state), false, false))
        }
    }
}
