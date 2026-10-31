package phpvpn.android.bridge.model

import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.Fixtures
import phpvpn.android.bridge.SequentialIds
import phpvpn.android.bridge.edit
import phpvpn.android.bridge.ipc.CheckpointResult
import phpvpn.android.bridge.ipc.ConnectionPhase
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.bridge.ipc.HttpAction
import phpvpn.android.bridge.ipc.HttpDiagnostic
import phpvpn.android.bridge.ipc.HttpDiagnosticMessage
import phpvpn.android.bridge.ipc.IPC_MAX_MESSAGE_BYTES
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.IpcOutcome
import phpvpn.android.bridge.ipc.RequestHeadersSource
import phpvpn.android.bridge.ipc.ResponseHeadersSource
import phpvpn.android.bridge.ipc.StrictJson
import phpvpn.android.bridge.number
import phpvpn.android.bridge.serialized
import phpvpn.android.bridge.with
import kotlinx.coroutines.test.runTest

/** 1:1 port of client/tests/unit/test_status_contract.cpp. */
class StatusContractTest {
    private fun contract(value: String) = Fixtures.text("status-contract-template.json").replace("\${VALUE}", value)

    private fun reverseObjects(value: JsonElement): JsonElement =
        if (value is JsonObject) JsonObject(LinkedHashMap<String, JsonElement>().apply {
            value.entries.reversed().forEach { (k, v) -> put(k, reverseObjects(v)) }
        }) else value

    @Test
    fun StatusContract_ServiceToTrayFullUnsignedRangeAndObjectOrder() {
        val signedMax = Long.MAX_VALUE.toULong()
        for (value in listOf(signedMax - 1uL, signedMax, signedMax + 1uL, ULong.MAX_VALUE)) {
            var document = StrictJson.parse(contract(value.toString())) as JsonObject
            // dataPollsWithPayload is 0, so dataPollsTotal == dataPollsEmpty keeps the invariant.
            val status = document["status"] as JsonObject
            assertEquals(26, status.size)
            assertNull(status["packetsSent"])
            assertNull(status["packetsReceived"])
            repeat(2) {
                val parsed = StatusModel.parseStatusResponse(document.serialized())
                assertTrue(parsed.valid)
                assertEquals(ConnectionState.RECOVERING, parsed.state)
                assertNotNull(parsed.connectionStartedAtUtcMs)
                assertEquals(CheckpointResult.SUCCESS, parsed.serverUrl)
                assertEquals(CheckpointResult.SUCCESS, parsed.serverSecret)
                assertEquals(CheckpointResult.FAILED, parsed.accessKey)
                assertEquals("communication_timeout", parsed.errorCode)
                assertEquals("The server did not respond in time. Verification could not be completed.", parsed.errorDescription)
                assertTrue(parsed.errorRecoverable)
                TrafficCounters.KEYS.forEachIndexed { index, key ->
                    assertEquals(key, if (key == "dataPollsWithPayload") 0uL else value, parsed.counters[index])
                }
                document = reverseObjects(document) as JsonObject
            }
        }
    }

    @Test
    fun StatusContract_InvalidReplyDoesNotResetServiceCountersInTray() {
        var current = StatusModel.parseStatusResponse(contract("123"))
        assertTrue(current.valid)
        val document = (StrictJson.parse(contract("123")) as JsonObject).edit("status") { it.with("ipv4BytesReceived", number("-1")) }
        val invalid = StatusModel.parseStatusResponse(document.serialized())
        assertFalse(invalid.valid)
        assertNull(StatusModel.applyStatusSnapshot(current, invalid))
        current = StatusModel.applyStatusSnapshot(current, invalid) ?: current
        assertEquals(123uL, current.counters.ipv4BytesReceived)
        assertEquals(CheckpointResult.FAILED, current.accessKey)
    }

    /** BoundedHttpCapture of [size] bytes 0x01, encoded as http-octets-v1. */
    private fun capture(size: Int): JsonObject {
        val truncated = size > HttpDiagnosticMessage.HTTP_DIAGNOSTIC_LIMIT
        val prefix = "\u0001".repeat(if (truncated) HttpDiagnosticMessage.HTTP_DIAGNOSTIC_PREFIX else size)
        val suffix = if (truncated) "\u0001".repeat(HttpDiagnosticMessage.HTTP_DIAGNOSTIC_TAIL) else ""
        return JsonObject(
            mapOf(
                "originalSizeBytes" to number(size.toString()),
                "prefix" to JsonPrimitive(prefix),
                "separator" to JsonPrimitive(if (truncated) HttpDiagnosticMessage.SEPARATOR else ""),
                "suffix" to JsonPrimitive(suffix),
            ),
        )
    }

    private fun diagnostic(size: Int) = JsonObject(
        mapOf(
            "format" to JsonPrimitive("http-octets-v1"), "action" to JsonPrimitive("health"),
            "responseComplete" to JsonPrimitive(true), "requestHeadersSource" to JsonPrimitive("application"),
            "responseHeadersSource" to JsonPrimitive("unavailable"), "request" to capture(size), "response" to capture(size),
        ),
    )

    @Test
    fun StatusContract_ConnectHttpDiagnosticStrictSchemaBoundsAndCorrelation() {
        val base = Fixtures.json("status-diagnostic-base.json")
        for (size in listOf(65535, 65536, 65537, 200000)) {
            val wire = base.edit("status") { it.with("httpDiagnostic", diagnostic(size)) }.serialized()
            assertTrue(wire.toByteArray().size < IPC_MAX_MESSAGE_BYTES)
            val parsed = StatusModel.parseStatusResponse(wire)
            assertTrue(parsed.valid)
            val d = parsed.httpDiagnostic!!
            assertEquals(7uL, parsed.connectionEpoch)
            assertEquals(size.toULong(), d.response.originalSizeBytes)
            assertEquals(minOf(size, if (size > 65536) 64512 else size), d.response.prefixOctets().size)
            assertTrue(d.response.prefixOctets().all { it == 1.toByte() })
            assertEquals(if (size > 65536) 1024 else 0, d.response.suffixOctets().size)
        }
        val valid = base.edit("status") { it.with("httpDiagnostic", diagnostic(4)) }
        for ((key, value) in listOf(
            "connectionEpoch" to number("0"), "connectionEpoch" to number("-1"),
            "state" to JsonPrimitive("recovering"), "phase" to JsonPrimitive("info"),
            "accessKeyCheck" to JsonPrimitive("failed"), "lastError" to JsonNull, "httpDiagnostic" to JsonNull,
        )) {
            assertFalse("$key=$value", StatusModel.parseStatusResponse(valid.edit("status") { it.with(key, value) }.serialized()).valid)
        }
        val extra = valid.edit("status", "httpDiagnostic") { it.with("unexpected", JsonPrimitive(true)) }
        assertFalse(StatusModel.parseStatusResponse(extra.serialized()).valid)
        assertFalse(StatusModel.parseStatusResponse(" ".repeat(IPC_MAX_MESSAGE_BYTES + 1)).valid)
        // Without a detail the member is absent (26 members).
        assertTrue(StatusModel.parseStatusResponse(base.serialized()).let { it.valid && it.httpDiagnostic == null })
        assertEquals(26, (base["status"] as JsonObject).size)
    }

    @Test
    fun StatusContract_DismissRequestIsClosedAndRejectsInvalidIdentity() = runTest {
        // Client side of the dismiss-connect-diagnostic request of the same C++ test.
        val transport = FakeIpcTransport()
        transport.respond("dismiss-connect-diagnostic") { FakeIpcTransport.Reply.Text(Fixtures.text("ack.json")) }
        val client = IpcClient(transport, SequentialIds())
        assertEquals(IpcOutcome.Success(Unit), client.dismissConnectDiagnostic(7uL, Fixtures.STATUS_ID))
        val sent = transport.requests.single().json
        assertEquals(setOf("ipcVersion", "requestId", "command", "connectionEpoch", "connectAttemptId"), sent.keys)
        assertEquals("7", (sent["connectionEpoch"] as JsonPrimitive).content)
        assertEquals(Fixtures.STATUS_ID, (sent["connectAttemptId"] as JsonPrimitive).content)
        // Epoch zero and a non-UUIDv4 identity never leave the client.
        assertEquals(IpcOutcome.Rejected(phpvpn.android.bridge.errors.ErrorCode.IPC_MALFORMED), client.dismissConnectDiagnostic(0uL, Fixtures.STATUS_ID))
        assertEquals(IpcOutcome.Rejected(phpvpn.android.bridge.errors.ErrorCode.IPC_MALFORMED), client.dismissConnectDiagnostic(7uL, "7"))
        assertEquals(1, transport.requests.size)
    }

    @Test
    fun StatusContract_ConnectHttpDiagnosticModelRetiresOldAndClearedAttempts() {
        val diagnostic = HttpDiagnostic(
            HttpDiagnostic.FORMAT, HttpAction.HEALTH, true, RequestHeadersSource.APPLICATION, ResponseHeadersSource.UNAVAILABLE,
            HttpDiagnosticMessage(1uL, "x", "", ""), HttpDiagnosticMessage(1uL, "x", "", ""),
        )
        val s = TrayStatus(
            valid = true, connectionEpoch = 7uL, state = ConnectionState.ERROR, phase = ConnectionPhase.IDLE,
            connectAttemptId = Fixtures.STATUS_ID, hasError = true, errorCode = "endpoint_unavailable_or_invalid",
            httpDiagnostic = diagnostic,
        )
        var model = ProgressModel()
        model.onSnapshot(s, 0); assertNotNull(model.httpDiagnostic)
        val old = s.copy(connectionEpoch = 6uL, httpDiagnostic = null)
        model.onSnapshot(old, 0); assertNotNull(model.httpDiagnostic)
        val cleared = s.copy(httpDiagnostic = null)
        model.onSnapshot(cleared, 0); assertNull(model.httpDiagnostic)
        model.onSnapshot(s, 0); assertNull(model.httpDiagnostic)
        model = ProgressModel(); model.onSnapshot(s, 0)
        model.onSnapshot(TrayStatus(), 0); assertNull(model.httpDiagnostic)
        model.onSnapshot(s, 0); assertNull(model.httpDiagnostic)
        model = ProgressModel(); model.onSnapshot(s, 0)
        val newer = s.copy(connectionEpoch = 8uL, state = ConnectionState.CONNECTING, httpDiagnostic = null)
        model.onSnapshot(newer, 0); assertNull(model.httpDiagnostic); assertTrue(model.view.closeRequested)
        model.onSnapshot(s, 0); assertNull(model.httpDiagnostic)
        model = ProgressModel(); model.onSnapshot(s, 0); model.onClose()
        model.onSnapshot(s, 0); assertNull(model.httpDiagnostic)
        model = ProgressModel(); model.onSnapshot(s, 0)
        var restarted = s.copy(connectAttemptId = "7f2a7c1e-9b44-4b0e-8d2f-5c6a1e7b9d03")
        model.onSnapshot(restarted, 0); assertNull(model.httpDiagnostic); assertTrue(model.view.closeRequested)
        model = ProgressModel(); model.onSnapshot(s, 0)
        restarted = restarted.copy(connectionEpoch = 1uL)
        model.onSnapshot(restarted, 0); assertNull(model.httpDiagnostic)
        model.onSnapshot(s, 0); assertNull(model.httpDiagnostic)
    }
}
