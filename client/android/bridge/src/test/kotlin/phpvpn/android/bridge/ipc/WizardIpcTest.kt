package phpvpn.android.bridge.ipc

import kotlinx.coroutines.test.runTest
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.Fixtures
import phpvpn.android.bridge.Fixtures.ID
import phpvpn.android.bridge.Fixtures.OTHER
import phpvpn.android.bridge.SequentialIds
import phpvpn.android.bridge.array
import phpvpn.android.bridge.edit
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.errors.ErrorTextKey
import phpvpn.android.bridge.errors.ErrorTexts
import phpvpn.android.bridge.number
import phpvpn.android.bridge.obj
import phpvpn.android.bridge.serialized
import phpvpn.android.bridge.with
import phpvpn.android.bridge.without

/**
 * Port of client/tests/unit/test_wizard_ipc.cpp for the client side of the
 * contract: the requests the Kotlin client builds and the replies it accepts.
 * Test names keep the C++ names. Request-parsing cases of the service
 * (`WizardIpc::parseRequest`) map to "the client never sends it".
 */
class WizardIpcTest {
    private val check = StructuralConfigDocumentCheck

    private fun envelope(value: JsonElement) =
        JsonObject(mapOf("ipcVersion" to JsonPrimitive(1), "requestId" to JsonPrimitive(ID), "result" to value))

    private fun accepts(command: IpcCommand, value: JsonElement): Boolean =
        WireValidation.wizardReply(command, ID, envelope(value), check) is WizardReplyCheck.Result

    private fun reply(command: IpcCommand, text: String, id: String = ID) =
        WireValidation.wizardReply(command, id, StrictJson.parse(text), check)

    private fun result(name: String) = Fixtures.json(name)["result"] as JsonObject
    private fun fields() = Fixtures.json("wizard-fields.json")
    private fun defaultFields(headers: String = HttpHeaders.DEFAULT) =
        IpcJson.decodeFromJsonElement(WizardFields.serializer(), fields().with("httpHeaders", headers))

    private fun session() = WizardSession(ID, ID)

    private fun client(transport: FakeIpcTransport) = IpcClient(transport, SequentialIds())

    @Test
    fun wizard_ipc_http_headers_all_requests_strict_roundtrip_and_legacy() = runTest {
        val transport = FakeIpcTransport()
        val client = client(transport)
        val headers = "Cookie: synthetic=1\r\nX: \"a\\b\"\r\nX-Utf8: \u017e"
        client.wizardCheckUrl(session(), ID, 1uL, "http://127.0.0.1/phpvpn.php", headers)
        client.wizardCheckSecret(session(), ID, 1uL, "http://127.0.0.1/phpvpn.php", SecretText.of("s"), headers)
        client.wizardCheckAccess(session(), ID, 1uL, defaultFields(headers))
        client.wizardFinish(session(), ID, defaultFields(headers), true)
        val sent = transport.requests
        assertEquals(4, sent.size)
        assertEquals(headers, JsonChecks.string(sent[0].json["httpHeaders"]))
        assertEquals(headers, JsonChecks.string(sent[1].json["httpHeaders"]))
        assertEquals(headers, JsonChecks.string(sent[2].json.obj("fields")["httpHeaders"]))
        assertEquals(headers, JsonChecks.string(sent[3].json.obj("fields")["httpHeaders"]))
        // Invalid header blocks never leave the client (the service would answer ipc_malformed).
        // The C++ case "X: \xff" is invalid UTF-8, which a Kotlin String cannot carry.
        for (bad in listOf("HOST: synthetic-secret", "X: a\nx: b", "", "X: a\u0001")) {
            assertEquals(IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED), client.wizardCheckUrl(session(), ID, 1uL, "u", bad))
            assertEquals(IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED), client.wizardCheckAccess(session(), ID, 1uL, defaultFields(bad)))
        }
        val huge = "X: " + "\t".repeat(IPC_MAX_MESSAGE_BYTES)
        assertEquals(IpcOutcome.Rejected(ErrorCode.IPC_MESSAGE_TOO_LARGE), client.wizardCheckUrl(session(), ID, 1uL, "u", huge))
        assertEquals(4, transport.requests.size)

        val view = result("wizard-view-edit.json").edit("fields") { it.with("httpHeaders", "Cookie: changed") }
        assertTrue(accepts(IpcCommand.WIZARD_OPEN, view))
        val decoded = IpcJson.decodeFromJsonElement(WizardView.serializer(), view)
        assertEquals("Cookie: changed", decoded.fields!!.httpHeaders)
        assertEquals(HttpHeaders.DEFAULT, decoded.context!!.httpHeaders)
        val legacy = view.edit("fields") { it.without("httpHeaders") }
        assertEquals(ErrorCode.WIZARD_UNSUPPORTED, WireValidation.view(legacy))
        assertEquals(ErrorCode.IPC_MALFORMED, WireValidation.view(legacy.with("contractCount", "wrong")))
        // A complete historical reply diagnoses wizard_unsupported through the client.
        assertEquals(WizardReplyCheck.Rejected(ErrorCode.WIZARD_UNSUPPORTED), WireValidation.wizardReply(IpcCommand.WIZARD_OPEN, ID, envelope(legacy), check))
    }

    /** C++ `requestFields`, per command. */
    private val expectedMembers: Map<IpcCommand, Set<String>> = run {
        val triple = setOf("ipcVersion", "requestId", "command")
        val session = triple + setOf("serviceInstanceId", "sessionId")
        val attempt = setOf("clientAttemptId", "attemptSequence")
        mapOf(
            IpcCommand.GET_CONFIG_STATE to triple, IpcCommand.GET_CONFIG_DOCUMENT to triple,
            IpcCommand.GET_DEFAULT_CONFIG to triple, IpcCommand.CREATE_DEFAULT_CONFIG to triple, IpcCommand.WIZARD_OPEN to triple,
            IpcCommand.WIZARD_RESET to session, IpcCommand.WIZARD_KEEPALIVE to session, IpcCommand.WIZARD_CLOSE to session,
            IpcCommand.WIZARD_CHECK_URL to session + attempt + setOf("serverUrl", "httpHeaders"),
            IpcCommand.WIZARD_CHECK_SECRET to session + attempt + setOf("serverUrl", "httpHeaders", "serverSecret"),
            IpcCommand.WIZARD_CHECK_ACCESS to session + attempt + setOf("fields"),
            IpcCommand.WIZARD_GET_OPERATION to session + "operationId", IpcCommand.WIZARD_CANCEL_OPERATION to session + "operationId",
            IpcCommand.WIZARD_CANCEL_ATTEMPT to session + attempt,
            IpcCommand.WIZARD_RESUME to session + "connectionEpoch",
            IpcCommand.WIZARD_FINISH to session + setOf("commitId", "fields", "createDefaultContracts"),
            IpcCommand.WIZARD_GET_COMMIT to session + "commitId",
        )
    }

    @Test
    fun wizard_ipc_all_seventeen_request_schemas_are_closed_and_strict() = runTest {
        val transport = FakeIpcTransport()
        val client = client(transport)
        client.getConfigState(); client.getConfigDocument(); client.getDefaultConfig(); client.createDefaultConfig(); client.wizardOpen()
        client.wizardReset(session()); client.wizardKeepalive(session())
        client.wizardCheckUrl(session(), ID, 1uL, "http://127.0.0.1/phpvpn.php", HttpHeaders.DEFAULT)
        client.wizardCheckSecret(session(), ID, 1uL, "http://127.0.0.1/phpvpn.php", SecretText.of("x"), HttpHeaders.DEFAULT)
        client.wizardCheckAccess(session(), ID, 1uL, defaultFields())
        client.wizardGetOperation(session(), ID); client.wizardCancelOperation(session(), ID)
        client.wizardCancelAttempt(session(), ID, 1uL); client.wizardResume(session(), 0uL)
        client.wizardFinish(session(), ID, defaultFields(), true); client.wizardGetCommit(session(), ID); client.wizardClose(session())
        val sent = transport.requests
        assertEquals(17, sent.size)
        val decoders = mapOf(
            IpcCommand.WIZARD_RESET to WizardSessionRequest.serializer(), IpcCommand.WIZARD_CHECK_URL to WizardCheckUrlRequest.serializer(),
            IpcCommand.WIZARD_CHECK_SECRET to WizardCheckSecretRequest.serializer(), IpcCommand.WIZARD_CHECK_ACCESS to WizardCheckAccessRequest.serializer(),
            IpcCommand.WIZARD_GET_OPERATION to WizardOperationRequest.serializer(), IpcCommand.WIZARD_CANCEL_ATTEMPT to WizardCancelAttemptRequest.serializer(),
            IpcCommand.WIZARD_RESUME to WizardResumeRequest.serializer(), IpcCommand.WIZARD_FINISH to WizardFinishRequest.serializer(),
            IpcCommand.WIZARD_GET_COMMIT to WizardGetCommitRequest.serializer(), IpcCommand.WIZARD_OPEN to PlainRequest.serializer(),
        )
        for (request in sent) {
            val command = IpcCommand.fromWire(request.command)!!
            assertTrue(command.wizardFamily)
            assertEquals(command.wireName, expectedMembers.getValue(command), request.json.keys)
            assertEquals(1L, JsonChecks.int64(request.json["ipcVersion"]))
            assertTrue(WireFormats.isUuidV4(request.requestId))
            assertEquals(command.timeoutMs, request.timeoutMs)
            if (request.json.containsKey("fields")) assertEquals(8, request.json.obj("fields").size)
            decoders[command]?.let { IpcJson.decodeFromJsonElement(it, request.json) }
        }
        assertEquals(17, sent.map { it.command }.toSet().size)
    }

    private fun diagnosticOperation(): JsonObject = result("wizard-operation-url-failed-diagnostic.json")

    @Test
    fun wizard_ipc_diagnostic_schema_closed_roundtrip_and_illegal_placement() {
        val valid = diagnosticOperation()
        assertTrue(accepts(IpcCommand.WIZARD_GET_OPERATION, valid))
        val decoded = IpcJson.decodeFromJsonElement(OperationResult.serializer(), valid)
        assertEquals("POST http://localhost/?action=health HTTP/?\r\n\r\n{}", decoded.httpDiagnostic!!.request.prefix)
        for (level in listOf("httpDiagnostic", "request", "response")) {
            val path = if (level == "httpDiagnostic") arrayOf("httpDiagnostic") else arrayOf("httpDiagnostic", level)
            val target = path.fold(valid) { o, name -> o.obj(name) }
            for (key in target.keys) assertFalse("$level-$key", accepts(IpcCommand.WIZARD_GET_OPERATION, valid.edit(*path) { it.without(key) }))
            assertFalse(accepts(IpcCommand.WIZARD_GET_OPERATION, valid.edit(*path) { it.with("extra", JsonNull) }))
        }
        for ((key, replacement) in listOf(
            "httpDiagnostic" to JsonNull, "kind" to JsonPrimitive("access"), "kind" to JsonPrimitive("finish"),
            "operationState" to JsonPrimitive("succeeded"), "operationState" to JsonPrimitive("running"),
            "operationState" to JsonPrimitive("cancelled"), "operationState" to JsonPrimitive("interrupted"),
            "error" to JsonNull, "payload" to JsonObject(emptyMap()),
        )) assertFalse("$key=$replacement", accepts(IpcCommand.WIZARD_GET_OPERATION, valid.with(key, replacement)))
        for ((key, replacement) in listOf(
            "format" to JsonPrimitive("unknown"), "action" to JsonPrimitive("data"), "action" to JsonPrimitive("info"),
            "responseComplete" to number("1"), "requestHeadersSource" to JsonPrimitive("available"),
            "responseHeadersSource" to JsonPrimitive("application"),
        )) assertFalse("$key=$replacement", accepts(IpcCommand.WIZARD_GET_OPERATION, valid.edit("httpDiagnostic") { it.with(key, replacement) }))
        for ((key, replacement) in listOf(
            "serverUrl" to JsonPrimitive("http://localhost/"), "configPath" to JsonPrimitive("/fixture.json"),
            "code" to JsonPrimitive("a".repeat(129)), "description" to JsonPrimitive("a".repeat(4097)),
            "suggestedAction" to JsonPrimitive("a".repeat(4097)), "field" to JsonPrimitive("serverSecret"),
        )) assertFalse("$key", accepts(IpcCommand.WIZARD_GET_OPERATION, valid.edit("error") { it.with(key, replacement) }))
        val secret = valid.with("kind", "secret").edit("error") { it.with("field", "serverSecret") }.edit("httpDiagnostic") { it.with("action", "info") }
        assertTrue(accepts(IpcCommand.WIZARD_GET_OPERATION, secret))
        val old = valid.without("httpDiagnostic")
        assertEquals(8, old.size)
        assertTrue(accepts(IpcCommand.WIZARD_GET_OPERATION, old))
    }

    @Test
    fun wizard_ipc_diagnostic_octet_lengths_numbers_and_separator_are_exact() {
        val response = JsonObject(
            mapOf(
                "originalSizeBytes" to number("65537"), "prefix" to JsonPrimitive("a".repeat(64512)),
                "separator" to JsonPrimitive(HttpDiagnosticMessage.SEPARATOR), "suffix" to JsonPrimitive("b".repeat(1024)),
            ),
        )
        val valid = diagnosticOperation().edit("httpDiagnostic") { it.with("response", response) }
        assertTrue(accepts(IpcCommand.WIZARD_GET_OPERATION, valid))
        for ((key, replacement) in listOf(
            "originalSizeBytes" to number("-1"), "originalSizeBytes" to number("0"), "originalSizeBytes" to number("1.5"),
            "originalSizeBytes" to JsonPrimitive("65537"), "originalSizeBytes" to number("65536"),
            "prefix" to JsonPrimitive("a".repeat(64511)), "prefix" to JsonPrimitive("a".repeat(64513)),
            "prefix" to JsonPrimitive("a".repeat(64511) + "\u0100"), "suffix" to JsonPrimitive("b".repeat(1023)),
            "separator" to JsonPrimitive("\n...\n...\n...\n...\n"), "separator" to JsonPrimitive(""),
        )) assertFalse("$key", accepts(IpcCommand.WIZARD_GET_OPERATION, valid.edit("httpDiagnostic", "response") { it.with(key, replacement) }))
        var json = envelope(valid).serialized()
        json = json.replaceFirst("65537", "18446744073709551616")
        assertTrue(reply(IpcCommand.WIZARD_GET_OPERATION, json) is WizardReplyCheck.Rejected)
        json = envelope(valid).serialized()
        json = json.replaceFirst("\"format\":", "\"format\":\"http-octets-v1\",\"format\":")
        assertTrue(reply(IpcCommand.WIZARD_GET_OPERATION, json) is WizardReplyCheck.Rejected)
    }

    @Test
    fun wizard_ipc_diagnostic_worst_case_envelope_has_proven_headroom() = runTest {
        val max = ULong.MAX_VALUE.toString()
        val message = JsonObject(
            mapOf(
                "originalSizeBytes" to number(max), "prefix" to JsonPrimitive("\u0001".repeat(64512)),
                "separator" to JsonPrimitive(HttpDiagnosticMessage.SEPARATOR), "suffix" to JsonPrimitive("\u0001".repeat(1024)),
            ),
        )
        val op = diagnosticOperation().with("connectionEpoch", number(max)).with("attemptSequence", number(max))
            .edit("httpDiagnostic") { it.with("request", message).with("response", message) }
            .edit("error") { it.with("code", "\u0001".repeat(128)).with("description", "\u0001".repeat(4096)).with("suggestedAction", "\u0001".repeat(4096)) }
        val encoded = envelope(op).serialized()
        assertTrue(encoded.toByteArray().size <= 852232)
        val transport = FakeIpcTransport()
        transport.respond("wizard-get-operation") { FakeIpcTransport.Reply.Text(encoded.replace(ID, it.requestId)) }
        val outcome = IpcClient(transport, SequentialIds()).wizardGetOperation(session(), OTHER)
        val parsed = (outcome as IpcOutcome.Success).value
        assertEquals(ULong.MAX_VALUE, parsed.httpDiagnostic!!.request.originalSizeBytes)
        assertEquals(64512, parsed.httpDiagnostic!!.request.prefixOctets().size)
        assertEquals(ULong.MAX_VALUE, parsed.attemptSequence)
    }

    @Test
    fun wizard_ipc_diagnostic_safe_error_catalog_satisfies_envelope_bounds() {
        for (code in ErrorCode.entries) {
            val op = diagnosticOperation().edit("error") { it.with("code", code.wireName) }
            assertTrue(code.wireName.length <= 128)
            assertTrue(code.wireName, accepts(IpcCommand.WIZARD_GET_OPERATION, op))
        }
    }

    @Test
    fun wizard_ipc_sequence_full_unsigned_range_and_exact_secret_round_trip() = runTest {
        val transport = FakeIpcTransport()
        val client = client(transport)
        val secret = "before\u0000after\\n\n"
        val fields = IpcJson.decodeFromJsonElement(WizardFields.serializer(), fields().with("serverSecret", secret))
        client.wizardCheckAccess(session(), ID, ULong.MAX_VALUE, fields)
        val sent = transport.requests.single().json
        assertEquals(ULong.MAX_VALUE.toString(), (sent["attemptSequence"] as JsonPrimitive).content)
        val decoded = IpcJson.decodeFromJsonElement(WizardCheckAccessRequest.serializer(), sent)
        assertEquals(ULong.MAX_VALUE, decoded.attemptSequence)
        assertEquals(secret, decoded.fields.serverSecret.reveal())
        assertEquals(IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED), client.wizardCheckAccess(session(), ID, 0uL, fields))
        assertEquals(1, transport.requests.size)
        val cancel = result("wizard-cancel-attempt.json")
        for (bad in listOf(number("-1"), number("1.5"), JsonPrimitive("1"), number("0"))) {
            assertFalse(accepts(IpcCommand.WIZARD_CANCEL_ATTEMPT, cancel.with("attemptSequence", bad)))
        }
        assertTrue(accepts(IpcCommand.WIZARD_CANCEL_ATTEMPT, cancel.with("attemptSequence", number(ULong.MAX_VALUE.toString()))))
    }

    @Test
    fun wizard_ipc_views_context_and_operation_payloads_are_validated() {
        val view = result("wizard-view-create.json")
        assertTrue(accepts(IpcCommand.WIZARD_OPEN, view))
        assertFalse(accepts(IpcCommand.WIZARD_OPEN, view.with("secretHint", "unexpected")))
        for (name in listOf("url", "secret", "access", "finish")) {
            val value = result("wizard-operation-$name-succeeded.json")
            assertTrue(name, accepts(IpcCommand.WIZARD_GET_OPERATION, value))
            val decoded = IpcJson.decodeFromJsonElement(OperationResult.serializer(), value)
            assertEquals(name, IpcJson.encodeToJsonElement(OperationKind.serializer(), decoded.kind).let { (it as JsonPrimitive).content })
            // Round trip: the model writes the same closed object it read.
            assertEquals(value, IpcJson.encodeToJsonElement(OperationResult.serializer(), decoded))
            assertTrue(reply(IpcCommand.WIZARD_GET_OPERATION, envelope(value).serialized()) is WizardReplyCheck.Result)
            assertTrue(reply(IpcCommand.WIZARD_GET_OPERATION, envelope(value).serialized(), OTHER) is WizardReplyCheck.Rejected)
            assertFalse(accepts(IpcCommand.WIZARD_GET_OPERATION, value.with("payload", JsonNull)))
        }
    }

    @Test
    fun wizard_ipc_errors_omit_raw_details_and_enforce_allowlisted_fields() {
        val text = Fixtures.reply("wizard-error-unauthorized.json", ID)
        val parsed = reply(IpcCommand.WIZARD_CHECK_ACCESS, text)
        assertTrue(parsed is WizardReplyCheck.Error)
        val error = IpcJson.decodeFromJsonElement(WizardError.serializer(), (parsed as WizardReplyCheck.Error).error)
        assertEquals(401, error.httpStatus)
        assertEquals("accessKey", error.field)
        // The UI text is selected by code, never by the received description.
        assertEquals(ErrorTextKey.ERROR_SERVER_UNAUTHORIZED, ErrorTexts.messageKey(error.code))
        assertEquals(ErrorTextKey.ACTION_SERVER_UNAUTHORIZED, ErrorTexts.wizardActionKey(error.code, error.field))
        assertFalse(WireValidation.error(parsed.error.with("ciphertext", "hidden")))
        for ((key, value) in listOf("httpStatus" to number("99"), "httpStatus" to number("600"), "field" to JsonPrimitive("password"),
            "recoverable" to JsonPrimitive("false"), "configPath" to JsonPrimitive("relative/config.json"))) {
            assertFalse(key, WireValidation.error(parsed.error.with(key, value)))
        }
    }

    @Test
    fun wizard_ipc_configuration_errors_have_safe_action_and_authoritative_path() {
        val base = Fixtures.json("wizard-error-directory-security.json")
        for (code in listOf(
            ErrorCode.CONFIG_DIRECTORY_SECURITY_INVALID, ErrorCode.CONFIG_WRITE_FAILED, ErrorCode.CONFIG_UNREADABLE,
            ErrorCode.CONFIG_READ_FAILED, ErrorCode.CONFIG_TOO_LARGE, ErrorCode.CONFIG_ENCODING_ERROR,
            ErrorCode.JOURNAL_CORRUPT, ErrorCode.JOURNAL_AMBIGUOUS,
        )) {
            val text = base.edit("error") { it.with("code", code.wireName) }.serialized()
            val parsed = reply(IpcCommand.CREATE_DEFAULT_CONFIG, text) as WizardReplyCheck.Error
            val error = IpcJson.decodeFromJsonElement(WizardError.serializer(), parsed.error)
            assertEquals(code.wireName, error.code)
            assertEquals(Fixtures.CONFIG_PATH, error.configPath)
            val action = ErrorTexts.wizardActionKey(error.code, error.field)
            if (code == ErrorCode.CONFIG_DIRECTORY_SECURITY_INVALID) {
                // Android: restart the app, not an administrator restarting a service.
                assertEquals(ErrorTextKey.ACTION_CONFIG_DIRECTORY_SECURITY, action)
            } else {
                assertEquals(ErrorTextKey.ACTION_CONFIG_CHECK, action)
            }
        }
        assertEquals(ErrorTextKey.ACTION_VERIFY_AGAIN, ErrorTexts.wizardActionKey("transport_connect_failed", null))
        assertEquals(ErrorTextKey.ACTION_VERIFY_AGAIN, ErrorTexts.wizardActionKey("config_invalid_url", "serverUrl"))
        assertEquals(ErrorTextKey.ACTION_CONNECTION_INTERRUPTED, ErrorTexts.wizardActionKey("wizard_connection_interrupted", null))
    }

    @Test
    fun wizard_ipc_maximum_document_escaping_and_message_limit() = runTest {
        val control = "\u0001".repeat(MAX_CONFIG_FILE_BYTES.toInt())
        assertTrue(envelope(JsonObject(mapOf("document" to JsonPrimitive(control)))).serialized().toByteArray().size < IPC_MAX_MESSAGE_BYTES)
        assertFalse(accepts(IpcCommand.GET_DEFAULT_CONFIG, JsonObject(mapOf("document" to JsonPrimitive(control)))))
        val invalid = result("config-document-invalid.json").with("document", control)
        assertTrue(accepts(IpcCommand.GET_CONFIG_DOCUMENT, invalid))
        assertFalse(accepts(IpcCommand.GET_CONFIG_DOCUMENT, invalid.with("document", "x".repeat(MAX_CONFIG_FILE_BYTES.toInt() + 1))))
        val transport = FakeIpcTransport()
        val outcome = client(transport).wizardCheckSecret(
            session(), ID, 1uL, "u", SecretText.of("\u0001".repeat(IPC_MAX_MESSAGE_BYTES)), HttpHeaders.DEFAULT,
        )
        assertEquals(IpcOutcome.Rejected(ErrorCode.IPC_MESSAGE_TOO_LARGE), outcome)
        assertTrue(transport.requests.isEmpty())
    }

    @Test
    fun wizard_ipc_only_exact_correlated_legacy_unknown_command_maps_to_unsupported() {
        val legacy = Fixtures.reply("legacy-unknown-command.json", ID)
        val mapped = reply(IpcCommand.WIZARD_OPEN, legacy) as WizardReplyCheck.Error
        assertTrue(mapped.legacyUnsupported)
        val error = IpcJson.decodeFromJsonElement(WizardError.serializer(), mapped.error)
        assertEquals("wizard_unsupported", error.code)
        assertTrue(error.suggestedAction.contains("Update"))
        assertTrue(reply(IpcCommand.WIZARD_OPEN, legacy, OTHER) is WizardReplyCheck.Rejected)
        val root = StrictJson.parse(legacy) as JsonObject
        val untrusted = root.edit("error") { it.with("description", "UNTRUSTED RAW DESCRIPTION") }
        val sanitized = reply(IpcCommand.WIZARD_OPEN, untrusted.serialized()) as WizardReplyCheck.Error
        assertFalse(sanitized.error.serialized().contains("UNTRUSTED"))
        assertFalse(WireValidation.error(untrusted.obj("error"))) // General parser remains strict.
        for (bad in listOf(JsonPrimitive("true"), JsonPrimitive(true), JsonNull)) {
            assertTrue(reply(IpcCommand.WIZARD_OPEN, untrusted.edit("error") { it.with("recoverable", bad) }.serialized()) is WizardReplyCheck.Rejected)
        }
        assertTrue(reply(IpcCommand.WIZARD_OPEN, untrusted.edit("error") { it.with("serverUrl", "http://127.0.0.1/phpvpn.php") }.serialized()) is WizardReplyCheck.Rejected)
        for (code in listOf("internal_error", "ipc_malformed", "ipc_unsupported_version")) {
            assertTrue(reply(IpcCommand.WIZARD_OPEN, root.edit("error") { it.with("code", code) }.serialized()) is WizardReplyCheck.Rejected)
        }
        assertTrue(reply(IpcCommand.WIZARD_OPEN, root.with("ipcVersion", number("2")).serialized()) is WizardReplyCheck.Rejected)
        assertTrue(reply(IpcCommand.WIZARD_OPEN, root.with("result", JsonNull).serialized()) is WizardReplyCheck.Rejected)
    }

    @Test
    fun wizard_ipc_configuration_documents_enforce_availability_nulls_and_actual_content() {
        val valid = result("config-document-valid.json")
        assertTrue(accepts(IpcCommand.GET_CONFIG_DOCUMENT, valid))
        assertTrue(accepts(IpcCommand.GET_CONFIG_DOCUMENT, result("config-document-repairable.json")))
        val invalid = result("config-document-invalid.json")
        assertTrue(accepts(IpcCommand.GET_CONFIG_DOCUMENT, invalid))
        val missing = result("config-document-missing.json")
        assertTrue(accepts(IpcCommand.GET_CONFIG_DOCUMENT, missing))
        for (availability in listOf("missing", "unreadable", "encoding_error", "too_large", "repairable", "invalid")) {
            val changed = valid.with("availability", availability).with("reasonCode", "config_invalid_value")
            // Deviation: declaring a valid document "repairable" or "invalid" needs the native validator
            // (ConfigDocumentCheck from WP04); the structural default keeps any readable text editable.
            val expected = availability == "repairable" || availability == "invalid"
            assertEquals(availability, expected, accepts(IpcCommand.GET_CONFIG_DOCUMENT, changed))
        }
        for (bad in listOf(JsonNull, JsonPrimitive(""))) assertFalse(accepts(IpcCommand.GET_CONFIG_DOCUMENT, valid.with("revisionToken", bad)))
        assertFalse(accepts(IpcCommand.GET_CONFIG_DOCUMENT, missing.with("document", "")))
        assertFalse(accepts(IpcCommand.GET_CONFIG_DOCUMENT, valid.with("reasonCode", "config_missing")))
        var changed = invalid.with("reasonCode", JsonNull)
        assertFalse(accepts(IpcCommand.GET_CONFIG_DOCUMENT, changed))
        changed = changed.with("reasonCode", "config_invalid_json").with("document", "text\u0000tail")
        assertTrue(accepts(IpcCommand.GET_CONFIG_DOCUMENT, changed))
        val bom = "\uFEFF" + Fixtures.text("default-config-document.json")
        changed = changed.with("document", bom)
        assertTrue(accepts(IpcCommand.GET_CONFIG_DOCUMENT, changed))
        assertFalse(accepts(IpcCommand.GET_CONFIG_DOCUMENT, changed.with("availability", "valid").with("reasonCode", JsonNull)))
        for (member in valid.keys) assertFalse(member, accepts(IpcCommand.GET_CONFIG_DOCUMENT, valid.without(member)))
    }

    @Test
    fun wizard_ipc_configuration_paths_state_and_create_results_are_coherent() {
        val state = result("config-state.json")
        assertTrue(accepts(IpcCommand.GET_CONFIG_STATE, state))
        // Android/Linux accept only an absolute normalised POSIX file path.
        for (path in listOf(
            "", "config.json", "etc/phpvpn/config.json", "/", "//etc/phpvpn/config.json", "/etc//phpvpn/config.json",
            "/etc/phpvpn/", "/etc/./phpvpn/config.json", "/etc/phpvpn/..", "/etc/../etc/phpvpn/config.json",
            "/etc/phpvpn/config.json/.", "/etc/phpvpn/con\nfig.json", "/etc/phpvpn/con\u0001fig.json",
            "/etc/phpvpn/con\u0000fig.json", "C:/ProgramData/PHPVPN/config.json", "C:\\ProgramData\\PHPVPN\\config.json",
            "\\\\server\\share\\config.json", "~/config.json",
        )) assertFalse(path, accepts(IpcCommand.GET_CONFIG_STATE, state.with("configPath", path)))
        for (path in listOf(
            "/etc/phpvpn/config.json", "/c", "/var/lib/phpvpn/.config.json", "/etc/phpvpn/...", "/etc/phpvpn/config..json",
            "/opt/php vpn/config \u017e.json", Fixtures.CONFIG_PATH,
        )) assertTrue(path, accepts(IpcCommand.GET_CONFIG_STATE, state.with("configPath", path)))
        for (availability in listOf("missing", "unreadable", "encoding_error", "too_large")) {
            val changed = state.with("availability", availability).with("reasonCode", "config_unreadable")
            assertFalse(accepts(IpcCommand.GET_CONFIG_STATE, changed))
            assertTrue(accepts(IpcCommand.GET_CONFIG_STATE, changed.with("revisionToken", JsonNull)))
        }
        val created = result("create-default-config-created.json")
        assertTrue(accepts(IpcCommand.CREATE_DEFAULT_CONFIG, created))
        val brokenCreated = created.with("availability", "invalid").with("document", "broken-json")
        assertFalse(accepts(IpcCommand.CREATE_DEFAULT_CONFIG, brokenCreated))
        val race = brokenCreated.with("created", JsonPrimitive(false)) // External create race opens actual invalid text.
        assertTrue(accepts(IpcCommand.CREATE_DEFAULT_CONFIG, race))
        assertFalse(accepts(IpcCommand.CREATE_DEFAULT_CONFIG, race.with("revisionToken", "")))
        assertTrue(accepts(IpcCommand.CREATE_DEFAULT_CONFIG, result("create-default-config-race.json")))
    }

    @Test
    fun wizard_ipc_context_rejects_impossible_budgets_and_overflow() {
        val encoded = Fixtures.json("wizard-context.json")
        assertTrue(WireValidation.context(encoded))
        val context = IpcJson.decodeFromJsonElement(WizardContext.serializer(), encoded)
        assertEquals(134217728L, context.maxOutboundQueueBytes)
        assertEquals(262144L, context.minInboundPerPeerQueueBytes)
        assertEquals(5000L, context.maxQueuedPacketAgeMs)
        val reservation = (WireFormats.clientAddressCount(24) - 1) * context.minOutboundPerPeerQueueBytes
        for (key in listOf("maxInboundQueueBytes", "maxOutboundQueueBytes")) {
            assertFalse(WireValidation.context(encoded.with(key, number((reservation - 1).toString()))))
            assertTrue(WireValidation.context(encoded.with(key, number(reservation.toString()))))
        }
        assertFalse(WireValidation.context(encoded.with("minOutboundPerPeerQueueBytes", number(Long.MAX_VALUE.toString()))))
        assertFalse(WireValidation.context(encoded.with("minInboundPerPeerQueueBytes", number(Long.MAX_VALUE.toString()))))
        val asymmetric = encoded.with("minOutboundPerPeerQueueBytes", number("16")).with("minInboundPerPeerQueueBytes", number("17"))
            .with("maxQueuedPacketAgeMs", number("1234"))
        assertTrue(WireValidation.context(asymmetric))
        assertFalse(WireValidation.context(encoded.with("maxNdjsonLineBytes", number((context.maxResponseBodyBytes + 1).toString()))))
        for (member in encoded.keys) {
            if (member == "vpnCidr" || member == "trustAnyTlsCertificate") continue
            for (bad in listOf(number("0"), number("-1"), number("1.5"), number(ULong.MAX_VALUE.toString()))) {
                assertFalse("$member=$bad", WireValidation.context(encoded.with(member, bad)))
            }
        }
        assertTrue(WireValidation.context(encoded.with("responseTimeoutSeconds", number(MAX_HTTP_TIMEOUT_SECONDS.toString()))))
        assertFalse(WireValidation.context(encoded.with("responseTimeoutSeconds", number((MAX_HTTP_TIMEOUT_SECONDS + 1).toString()))))
        for (cidr in listOf("10.99.99.1/24", "10.99.99.0/23", "10.99.99.0/30", "10.99.99.0/024", "10.099.99.0/24")) {
            assertFalse(cidr, WireValidation.context(encoded.with("vpnCidr", cidr)))
        }
    }

    @Test
    fun wizard_ipc_full_default_template_and_view_reset_semantics() {
        assertTrue(accepts(IpcCommand.GET_DEFAULT_CONFIG, result("default-config.json")))
        for (content in listOf("", "{}", "{\"masterSetup\":{}}", "syntactically broken")) {
            assertFalse(content, accepts(IpcCommand.GET_DEFAULT_CONFIG, JsonObject(mapOf("document" to JsonPrimitive(content)))))
        }
        val unreadableReset = result("wizard-view-reset-unreadable.json")
        assertTrue(accepts(IpcCommand.WIZARD_OPEN, unreadableReset))
        assertFalse(accepts(IpcCommand.WIZARD_RESET, unreadableReset))
        val reset = unreadableReset.with("fields", fields()).with("context", Fixtures.json("wizard-context.json"))
        assertTrue(accepts(IpcCommand.WIZARD_RESET, reset))
        assertFalse(accepts(IpcCommand.WIZARD_OPEN, reset))
        assertTrue(WireValidation.view(reset.with("revisionToken", "stale-but-readable")) != null)
        val valid = reset.with("availability", "valid").with("revisionToken", "stale-but-readable")
        assertTrue(accepts(IpcCommand.WIZARD_RESET, valid))
        assertTrue(WireValidation.view(valid.with("mode", "repair")) != null)
        assertTrue(WireValidation.view(valid.with("mode", "edit").with("revisionToken", JsonNull)) != null)
        for (state in listOf("pending", "running")) {
            assertFalse(accepts(IpcCommand.WIZARD_CANCEL_OPERATION, JsonObject(mapOf("operationId" to JsonPrimitive(ID), "operationState" to JsonPrimitive(state)))))
        }
        assertTrue(accepts(IpcCommand.WIZARD_CANCEL_OPERATION, JsonObject(mapOf("operationId" to JsonPrimitive(ID), "operationState" to JsonPrimitive("succeeded")))))
        // existingDefaultContractPeers: sorted, unique, candidates .2-.15 of a 10.x.y.0/24 only.
        val peers = result("wizard-view-repair-peers.json")
        assertTrue(accepts(IpcCommand.WIZARD_OPEN, peers))
        for (bad in listOf(array("10.99.99.3", "10.99.99.2"), array("10.99.99.16"), array("10.99.98.2"), array("10.99.99.2", "10.99.99.2"),
            array("10.99.99.2", "10.99.99.3", "10.99.99.4", "10.99.99.5"))) {
            assertFalse(bad.toString(), accepts(IpcCommand.WIZARD_OPEN, peers.with("existingDefaultContractPeers", bad)))
        }
        assertFalse(accepts(IpcCommand.WIZARD_OPEN, peers.edit("context") { it.with("vpnCidr", "192.168.0.0/24") }))
        assertTrue(peers["existingDefaultContractPeers"] is JsonArray)
    }
}
