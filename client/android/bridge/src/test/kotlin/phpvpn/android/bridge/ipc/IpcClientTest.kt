package phpvpn.android.bridge.ipc

import kotlinx.coroutines.test.runTest
import kotlinx.serialization.KSerializer
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.Fixtures
import phpvpn.android.bridge.Fixtures.ID
import phpvpn.android.bridge.Fixtures.OTHER
import phpvpn.android.bridge.SequentialIds
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.respondFixture

/**
 * Every IPC v1 command through [IpcClient] against the fixtures: the exact
 * request members, the reply decoded into the kotlinx model, the model encoded
 * back to the same closed object, plus busy, timeout, malformed and
 * uncorrelated replies, the message limit and the wiping of request and reply
 * buffers. Also the client half of client/tests/unit/test_ipc.cpp.
 */
class IpcClientTest {
    private val session = WizardSession(ID, ID)
    private val fields = IpcJson.decodeFromJsonElement(WizardFields.serializer(), Fixtures.json("wizard-fields.json"))

    private fun setup(command: String, fixture: String): Pair<FakeIpcTransport, IpcClient> {
        val transport = FakeIpcTransport()
        transport.respondFixture(command, fixture)
        return transport to IpcClient(transport, SequentialIds())
    }

    /** Re-encodes the decoded model and compares with the fixture's object. */
    private fun <T> roundTrip(serializer: KSerializer<T>, value: T, fixture: String, member: String) {
        val expected = Fixtures.json(fixture)[member]!!
        assertEquals(fixture, expected, IpcJson.encodeToJsonElement(serializer, value))
    }

    @Test
    fun ipc_parses_every_command_of_the_contract_as_exact_requests() = runTest {
        val transport = FakeIpcTransport()
        val client = IpcClient(transport, SequentialIds())
        client.getStatus(); client.connect(); client.disconnect(); client.getInfo(); client.getConfig()
        client.setConfig(SecretText.of("{}")); client.getDiagnostics(); client.getServerInfo()
        client.dismissConnectDiagnostic(7uL, Fixtures.STATUS_ID)
        val expected = listOf(
            "get-status", "connect", "disconnect", "get-info", "get-config", "set-config", "get-diagnostics",
            "get-server-info", "dismiss-connect-diagnostic",
        )
        assertEquals(expected, transport.requests.map { it.command })
        for (request in transport.requests) {
            val members = when (request.command) {
                "set-config" -> setOf("ipcVersion", "requestId", "command", "config")
                "dismiss-connect-diagnostic" -> setOf("ipcVersion", "requestId", "command", "connectionEpoch", "connectAttemptId")
                else -> setOf("ipcVersion", "requestId", "command")
            }
            assertEquals(request.command, members, request.json.keys)
            assertTrue(WireFormats.isUuidV4(request.requestId))
            assertEquals(IpcCommand.fromWire(request.command)!!.timeoutMs, request.timeoutMs)
        }
        // Tray budgets: status 400 ms, commands and set-config 30 s.
        assertEquals(400, transport.requests[0].timeoutMs)
        assertEquals(30_000, transport.requests[1].timeoutMs)
        assertEquals(30_000, transport.requests[5].timeoutMs)
        // Every reply above was missing: each outcome is a timeout, never success.
    }

    @Test
    fun ipc_round_trips_a_built_set_config_request() = runTest {
        val (transport, client) = setup("set-config", "ack.json")
        assertEquals(IpcOutcome.Success(Unit), client.setConfig(SecretText.of("""{"masterSetup":{}}""")))
        val decoded = IpcJson.decodeFromJsonElement(SetConfigRequest.serializer(), transport.requests.single().json)
        assertEquals("""{"masterSetup":{}}""", decoded.config.reveal())
        assertEquals("set-config", decoded.command)
        val plain = IpcJson.decodeFromJsonElement(PlainRequest.serializer(), JsonObject(mapOf(
            "ipcVersion" to JsonPrimitive(1), "requestId" to JsonPrimitive(ID), "command" to JsonPrimitive("get-status"),
        )))
        assertEquals("get-status", plain.command)
    }

    @Test
    fun ipc_status_connect_and_disconnect_replies() = runTest {
        for (command in listOf("get-status", "connect", "disconnect")) {
            val (_, client) = setup(command, "status-connected.json")
            val outcome = when (command) {
                "get-status" -> client.getStatus()
                "connect" -> client.connect()
                else -> client.disconnect()
            }
            val status = (outcome as IpcOutcome.Success).value
            assertEquals(ConnectionState.CONNECTED, status.state)
            assertEquals(ConnectionPhase.RUNNING, status.phase)
            assertEquals(Tristate.YES, status.serverVerified)
            assertEquals(34uL, status.ipv4PacketsSent)
            assertNull(status.lastError) // present and null, never absent
            roundTrip(StatusSnapshot.serializer(), status, "status-connected.json", "status")
        }
    }

    @Test
    fun ipc_builds_an_error_response_without_any_internal_detail() = runTest {
        val (_, client) = setup("connect", "error-response-mac-invalid.json")
        val outcome = client.connect() as IpcOutcome.ServiceError
        assertEquals("response_mac_invalid", outcome.code)
        assertEquals(ErrorCode.RESPONSE_MAC_INVALID, outcome.errorCode)
        assertTrue(outcome.error.recoverable)
        assertNull(outcome.wizard)
        assertFalse(outcome.error.description.contains("0a1b2c"))
    }

    @Test
    fun ipc_builds_a_diagnostics_response_with_the_queue_and_counter_groups() = runTest {
        val (_, client) = setup("get-diagnostics", "diagnostics.json")
        val diagnostics = (client.getDiagnostics() as IpcOutcome.Success).value
        assertTrue(diagnostics.configurationValid)
        assertEquals(1, diagnostics.configurationWarnings.size)
        assertEquals(4096L, diagnostics.outboundQueue.occupiedBytes)
        assertEquals(2uL, diagnostics.outboundQueue.activeSubBucketCount)
        assertEquals(1024L, diagnostics.outboundQueue.inFlightBytes)
        assertEquals(2uL, diagnostics.outboundQueue.queuedItemCount)
        assertEquals(5uL, diagnostics.counters.queueExpiredPackets)
        assertEquals(2uL, diagnostics.counters.outboundQueueExpiredPackets)
        assertEquals(3uL, diagnostics.counters.inboundQueueExpiredPackets)
        assertEquals(7uL, diagnostics.counters.packetsDropped)
        roundTrip(DiagnosticsSnapshot.serializer(), diagnostics, "diagnostics.json", "diagnostics")
        // Closed object: an unknown member is rejected.
        val transport = FakeIpcTransport()
        transport.respond("get-diagnostics") {
            FakeIpcTransport.Reply.Text(Fixtures.text("diagnostics.json").replace("\"lastError\":null", "\"lastError\":null,\"extra\":1"))
        }
        assertEquals(IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED), IpcClient(transport, SequentialIds()).getDiagnostics())
    }

    @Test
    fun ipc_get_info_and_get_config_have_no_envelope() = runTest {
        val (_, info) = setup("get-info", "get-info.json")
        val text = (info.getInfo() as IpcOutcome.Success).value
        assertEquals(Fixtures.text("get-info.json"), text) // shown as received
        val (_, config) = setup("get-config", "default-config-document.json")
        val document = (config.getConfig() as IpcOutcome.Success).value
        assertEquals(Fixtures.text("default-config-document.json"), document.reveal())
        document.wipe()
        assertTrue(document.isWiped && document.isEmpty())
        val (_, failing) = setup("get-config", "busy-classic.json")
        assertTrue((failing.getConfig() as IpcOutcome.ServiceError).busy)
    }

    @Test
    fun ipc_server_info_reply_keeps_raw_text_for_the_native_model() = runTest {
        val (_, client) = setup("get-server-info", "server-info.json")
        val reply = (client.getServerInfo() as IpcOutcome.Success).value
        assertEquals("10.99.99.2", reply.result.localAddress)
        assertEquals(2, reply.result.localMaskedCommunicationContracts.size)
        assertTrue(reply.rawReply.contains("\"infoPlaintext\""))
        roundTrip(ServerInfoResult.serializer(), reply.result, "server-info.json", "result")
        val (_, incompatible) = setup("get-server-info", "server-info-incompatible.json")
        assertEquals(ErrorCode.SERVER_VERSION_INCOMPATIBLE, (incompatible.getServerInfo() as IpcOutcome.ServiceError).errorCode)
        // Own address, duplicate peer, foreign /24 and an uppercase hash are rejected.
        val base = Fixtures.text("server-info.json")
        for (bad in listOf(
            base.replace("\"10.99.99.3\"", "\"10.99.99.2\""), base.replace("\"10.99.99.4\"", "\"10.99.99.3\""),
            base.replace("\"10.99.99.4\"", "\"10.99.98.4\""), base.replace("a".repeat(64), "A".repeat(64)),
            base.replace("\"localAddress\":\"10.99.99.2\"", "\"localAddress\":\"10.99.99.255\""),
        )) {
            val transport = FakeIpcTransport()
            transport.respond("get-server-info") { FakeIpcTransport.Reply.Text(bad) }
            assertEquals(IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED), IpcClient(transport, SequentialIds()).getServerInfo())
        }
    }

    @Test
    fun ipc_configuration_command_replies_decode_and_round_trip() = runTest {
        val state = (setup("get-config-state", "config-state.json").second.getConfigState() as IpcOutcome.Success).value
        assertEquals(ULong.MAX_VALUE, state.connectionEpoch)
        roundTrip(ConfigStateResult.serializer(), state, "config-state.json", "result")
        for (name in listOf("valid", "repairable", "invalid", "missing")) {
            val fixture = "config-document-$name.json"
            val document = (setup("get-config-document", fixture).second.getConfigDocument() as IpcOutcome.Success).value
            assertEquals(name, document.availability.wireName)
            assertEquals(document.availability.readable, document.document != null)
            roundTrip(ConfigDocumentResult.serializer(), document, fixture, "result")
        }
        // Unreadable, encoding and size failures arrive as structured errors, never as a
        // document result without text (wizard_ipc.cpp documentMatches requires the text).
        assertEquals(
            IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED),
            setup("get-config-document", "config-document-unreadable.json").second.getConfigDocument(),
        )
        val template = (setup("get-default-config", "default-config.json").second.getDefaultConfig() as IpcOutcome.Success).value
        assertEquals(Fixtures.text("default-config-document.json"), template.document.reveal())
        for (name in listOf("created", "race")) {
            val fixture = "create-default-config-$name.json"
            val created = (setup("create-default-config", fixture).second.createDefaultConfig() as IpcOutcome.Success).value
            assertEquals(name == "created", created.created)
            roundTrip(CreateDefaultConfigResult.serializer(), created, fixture, "result")
        }
    }

    @Test
    fun ipc_wizard_command_replies_decode_and_round_trip() = runTest {
        val open = (setup("wizard-open", "wizard-view-edit.json").second.wizardOpen() as IpcOutcome.Success).value
        assertEquals(WizardMode.EDIT, open.mode)
        assertEquals("CHANGE_THIS_SERVER_SECRET", open.fields!!.serverSecret.reveal())
        assertFalse(open.fields.toString().contains("CHANGE_THIS")) // secrets are redacted
        roundTrip(WizardView.serializer(), open, "wizard-view-edit.json", "result")
        val reset = (setup("wizard-reset", "wizard-view-reset.json").second.wizardReset(session) as IpcOutcome.Success).value
        assertEquals(WizardMode.RESET, reset.mode)
        val resumed = (setup("wizard-resume", "wizard-view-edit-connected.json").second.wizardResume(session, 4uL) as IpcOutcome.Success).value
        assertFalse(resumed.offline)
        val keepalive = (setup("wizard-keepalive", "wizard-keepalive.json").second.wizardKeepalive(session) as IpcOutcome.Success).value
        assertEquals(60_000L, keepalive.leaseDurationMs)
        assertTrue((setup("wizard-close", "wizard-close.json").second.wizardClose(session) as IpcOutcome.Success).value.closed)
        for (command in listOf("wizard-check-url", "wizard-check-secret", "wizard-check-access", "wizard-finish")) {
            val client = setup(command, "wizard-started.json").second
            val started = when (command) {
                "wizard-check-url" -> client.wizardCheckUrl(session, ID, 1uL, "http://127.0.0.1/phpvpn.php", HttpHeaders.DEFAULT)
                "wizard-check-secret" -> client.wizardCheckSecret(session, ID, 1uL, "http://127.0.0.1/phpvpn.php", SecretText.of("s"), HttpHeaders.DEFAULT)
                "wizard-check-access" -> client.wizardCheckAccess(session, ID, 1uL, fields)
                else -> client.wizardFinish(session, ID, fields, true)
            }
            assertEquals(OperationStarted(OTHER, OperationState.PENDING), (started as IpcOutcome.Success).value)
        }
        val operation = (setup("wizard-get-operation", "wizard-operation-secret-succeeded.json").second
            .wizardGetOperation(session, OTHER) as IpcOutcome.Success).value
        assertEquals(SecretPayload(true, "10.99.99.10"), operation.payload)
        val cancelled = (setup("wizard-cancel-operation", "wizard-cancel-operation.json").second
            .wizardCancelOperation(session, OTHER) as IpcOutcome.Success).value
        assertEquals(OperationState.CANCELLED, cancelled.operationState)
        val attempt = (setup("wizard-cancel-attempt", "wizard-cancel-attempt.json").second
            .wizardCancelAttempt(session, ID, 1uL) as IpcOutcome.Success).value
        assertEquals(AttemptState.CANCELLED, attempt.attemptState)
        for (name in listOf("confirmed", "failed", "pending", "unknown")) {
            val fixture = "wizard-commit-$name.json"
            val commit = (setup("wizard-get-commit", fixture).second.wizardGetCommit(session, ID) as IpcOutcome.Success).value
            assertEquals(name, IpcJson.encodeToJsonElement(CommitState.serializer(), commit.commitState).let { (it as JsonPrimitive).content })
            roundTrip(CommitResult.serializer(), commit, fixture, "result")
        }
    }

    @Test
    fun ipc_busy_replies_of_both_envelopes() = runTest {
        val classic = setup("get-status", "busy-classic.json").second.getStatus() as IpcOutcome.ServiceError
        assertTrue(classic.busy)
        assertTrue(classic.error.recoverable)
        val wizard = setup("wizard-check-url", "busy-wizard.json").second
            .wizardCheckUrl(session, ID, 1uL, "u", HttpHeaders.DEFAULT) as IpcOutcome.ServiceError
        assertTrue(wizard.busy)
        assertNotNull(wizard.wizard)
        assertNull(wizard.wizard!!.field)
    }

    @Test
    fun ipc_timeout_is_never_success() = runTest {
        val transport = FakeIpcTransport()
        transport.respond("get-status") { FakeIpcTransport.Reply.Delayed(400, FakeIpcTransport.Reply.Text(Fixtures.text("status-connected.json"))) }
        transport.respond("connect") { FakeIpcTransport.Reply.Timeout }
        val client = IpcClient(transport, SequentialIds())
        assertEquals(IpcOutcome.TimedOut, client.getStatus())
        assertEquals(IpcOutcome.TimedOut, client.connect())
        val raw = client.setConfigRaw(SecretText.of("{}"))
        assertNull(raw.response)
        assertTrue(raw.writeAttempted)
        assertEquals(IpcOutcome.TimedOut, client.setConfig(SecretText.of("{}")))
        // A reply just inside the budget is accepted.
        transport.respond("get-status") { FakeIpcTransport.Reply.Delayed(399, FakeIpcTransport.Reply.Text(Fixtures.text("status-connected.json"))) }
        assertTrue(client.getStatus() is IpcOutcome.Success)
    }

    @Test
    fun ipc_rejects_malformed_uncorrelated_and_oversized_replies() = runTest {
        val bad = listOf(
            FakeIpcTransport.Reply.Raw(byteArrayOf(0xC3.toByte(), 0x28)), // invalid UTF-8
            FakeIpcTransport.Reply.Text("{"),
            FakeIpcTransport.Reply.Text("not json"),
            FakeIpcTransport.Reply.Text(Fixtures.text("status-connected.json") + "x"),
            FakeIpcTransport.Reply.Text(Fixtures.text("error-uncorrelated.json")),
            FakeIpcTransport.Reply.Text(Fixtures.text("status-connected.json").replace("\$ID", OTHER)),
            FakeIpcTransport.Reply.Text(Fixtures.text("ack.json")),
        )
        for (reply in bad) {
            val transport = FakeIpcTransport()
            transport.respond("get-status") { reply }
            assertEquals(reply.toString(), IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED), IpcClient(transport, SequentialIds()).getStatus())
        }
        val transport = FakeIpcTransport()
        transport.respond("get-status") { FakeIpcTransport.Reply.Raw(ByteArray(IPC_MAX_MESSAGE_BYTES + 1) { ' '.code.toByte() }) }
        assertEquals(IpcOutcome.Rejected(ErrorCode.IPC_MESSAGE_TOO_LARGE), IpcClient(transport, SequentialIds()).getStatus())
        // A Wizard reply for another request id is rejected as well.
        val wizard = FakeIpcTransport()
        wizard.respond("wizard-open") { FakeIpcTransport.Reply.Text(Fixtures.text("wizard-view-edit.json").replace("\$ID", OTHER)) }
        assertEquals(IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED), IpcClient(wizard, SequentialIds()).wizardOpen())
    }

    @Test
    fun ipc_rejects_a_request_above_the_message_limit_without_sending() = runTest {
        val transport = FakeIpcTransport()
        val client = IpcClient(transport, SequentialIds())
        val outcome = client.setConfig(SecretText.of("\u0001".repeat(IPC_MAX_MESSAGE_BYTES)))
        assertEquals(IpcOutcome.Rejected(ErrorCode.IPC_MESSAGE_TOO_LARGE), outcome)
        assertTrue(transport.requests.isEmpty())
    }

    @Test
    fun ipc_wipes_request_and_reply_buffers() = runTest {
        var sentRequest: ByteArray? = null
        var reply: ByteArray? = null
        val transport = IpcTransport { request, _ ->
            sentRequest = request
            assertTrue(request.toString(Charsets.UTF_8).contains("SECRET-VALUE"))
            Fixtures.reply("ack.json", (StrictJson.parse(request) as JsonObject)["requestId"].let { (it as JsonPrimitive).content })
                .toByteArray().also { reply = it }
        }
        assertEquals(IpcOutcome.Success(Unit), IpcClient(transport, SequentialIds()).setConfig(SecretText.of("{\"k\":\"SECRET-VALUE\"}")))
        assertArrayEquals(ByteArray(sentRequest!!.size), sentRequest)
        assertArrayEquals(ByteArray(reply!!.size), reply)
    }

    @Test
    fun ipc_wizard_errors_carry_structured_members() = runTest {
        val outcome = setup("wizard-check-access", "wizard-error-unauthorized.json").second
            .wizardCheckAccess(session, ID, 1uL, fields) as IpcOutcome.ServiceError
        val error = outcome.wizard!!
        assertEquals(401, error.httpStatus)
        assertEquals(Fixtures.CONFIG_PATH, error.configPath)
        assertEquals("http://127.0.0.1/phpvpn.php", error.serverUrl)
        val element: JsonElement = IpcJson.encodeToJsonElement(WizardError.serializer(), error)
        assertEquals(Fixtures.json("wizard-error-unauthorized.json")["error"], element)
        val legacy = setup("wizard-open", "legacy-unknown-command.json").second.wizardOpen() as IpcOutcome.ServiceError
        assertEquals(ErrorCode.WIZARD_UNSUPPORTED, legacy.errorCode)
    }
}
