package phpvpn.android.standin

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.controller.ContractsEvent
import phpvpn.android.bridge.controller.ContractsStage
import phpvpn.android.bridge.controller.SaveOperation
import phpvpn.android.bridge.controller.SaveOutcome
import phpvpn.android.bridge.controller.SaveResult
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.IPC_VERSION
import phpvpn.android.bridge.ipc.IpcCommand
import phpvpn.android.bridge.ipc.IpcJson
import phpvpn.android.bridge.ipc.IpcTimeouts
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.ipc.SetConfigRequest
import phpvpn.android.debug.ConfigReplies

/**
 * Port of client/tests/unit/test_contracts_save_machine.cpp for the Kotlin
 * stand-in of `ContractsSaveMachine` and `classifySaveResponse`. The check
 * reply crosses as raw bytes (the JNI shape), so the C++ `WizardReply`
 * fixtures become get-config-document envelopes.
 */
class StandInContractsSaveMachineTest {
    private val requestId = "12345678-1234-4234-8234-123456789abc"
    private val unknownText = "The save result is unknown. The file may already have changed. Check the current configuration before saving again."

    /**
     * Five peers keep every model small. Unlike the C++ fixture the object is
     * present: the Kotlin get-config-document check (WP06) requires it for a
     * `valid` document.
     */
    private val smallDocument = contractsDocument("10.99.99.2", "10.99.99.0/29", contractsPresent = true)

    private fun outcome(operation: SaveOperation, value: SaveOutcome, error: ErrorCode) = SaveResult(operation, value, error)

    private fun loadAndEdit(machine: StandInContractsSaveMachine): Boolean {
        if (machine.load(SecretText.of(smallDocument), "/etc/phpvpn/config.json", "rev-1") != null) return false
        val model = machine.model!!
        if (model.setKey("10.99.99.3", SecretText.of("a".repeat(64))) != null || model.setEnabled("10.99.99.3", true) != null) return false
        machine.changed()
        return true
    }

    private fun reply(availability: String, text: String? = null): ByteArray =
        ConfigReplies.configDocument(availability, text).replace("\$ID", requestId).toByteArray()

    private fun dispatched(machine: StandInContractsSaveMachine, nowMs: Long): SaveOperation {
        val prepared = machine.dispatch(nowMs)
        assertNotNull(prepared)
        prepared!!.document.wipe()
        return prepared.operation
    }

    private fun toUnknown(machine: StandInContractsSaveMachine): Boolean {
        if (!loadAndEdit(machine) || machine.prepare(false, false) != null) return false
        val operation = dispatched(machine, 10)
        return machine.saveFinished(outcome(operation, SaveOutcome.UNKNOWN, ErrorCode.TIMEOUT)) == ContractsEvent.UNKNOWN
    }

    private fun startCheck(machine: StandInContractsSaveMachine): Long = machine.check(false)!!

    @Test
    fun confirmedSaveCarriesIdentityAndDeadline() {
        val machine = StandInContractsSaveMachine()
        assertEquals(ContractsStage.CLOSED, machine.stage)
        assertTrue(loadAndEdit(machine))
        assertEquals(ContractsStage.EDITING, machine.stage)
        var snapshot = machine.snapshot(false)
        assertTrue(snapshot.open && snapshot.dirty && !snapshot.saving && !snapshot.unknown && !snapshot.checking)
        assertEquals(1L, snapshot.windowGeneration)
        assertEquals(2L, snapshot.draftGeneration)
        assertTrue(machine.snapshot(true).checking)

        assertNull(machine.prepare(false, false))
        assertTrue(machine.model!!.frozen)
        assertNotNull(machine.prepare(false, false))
        val prepared = machine.dispatch(1000)!!
        assertEquals(SaveOperation(1, 2, 1, 1000L + IpcTimeouts.CONTRACTS_SAVE_DEADLINE_MS), prepared.operation)
        assertEquals(machine.standInModel!!.candidate!!.text().reveal(), prepared.document.reveal())
        assertEquals(ContractsStage.SAVING, machine.stage)
        assertTrue(machine.saving)
        assertEquals("Saving configuration...", machine.notice)
        snapshot = machine.snapshot(false)
        assertTrue(snapshot.saving)
        assertEquals(1L, snapshot.operationGeneration)
        assertEquals(1000L + IpcTimeouts.CONTRACTS_SAVE_DEADLINE_MS, snapshot.deadlineMs)
        // A second dispatch needs a new prepare.
        assertNull(machine.dispatch(1000))
        // Changes during saving do not advance the draft.
        machine.changed()
        assertEquals(2L, machine.snapshot(false).draftGeneration)

        val foreign = prepared.operation.copy(operationGeneration = prepared.operation.operationGeneration + 1)
        assertEquals(ContractsEvent.NONE, machine.saveFinished(outcome(foreign, SaveOutcome.CONFIRMED, ErrorCode.NONE)))
        assertEquals(ContractsStage.SAVING, machine.stage)
        assertEquals(ContractsEvent.SAVED, machine.saveFinished(outcome(prepared.operation, SaveOutcome.CONFIRMED, ErrorCode.NONE)))
        assertEquals(ContractsStage.SAVED, machine.stage)
        assertEquals("Changes saved. Checking connection status...", machine.notice)
        snapshot = machine.snapshot(false)
        assertTrue(snapshot.open && !snapshot.dirty && !snapshot.saving)
        assertEquals(0L, snapshot.deadlineMs)
        assertEquals(ContractsEvent.NONE, machine.saveFinished(outcome(prepared.operation, SaveOutcome.CONFIRMED, ErrorCode.NONE)))
    }

    @Test
    fun refusedDispatchAndRejectionKeepTheDraft() {
        val machine = StandInContractsSaveMachine()
        assertNull(machine.dispatch(5))
        assertTrue(loadAndEdit(machine))
        assertNull(machine.dispatch(5))

        // Not sent (the Kotlin transport reports writeAttempted = false) resumes like a refused sender.
        assertNull(machine.prepare(false, false))
        var operation = dispatched(machine, 5)
        assertEquals(ContractsEvent.REJECTED, machine.saveFinished(outcome(operation, SaveOutcome.NOT_SENT, ErrorCode.IPC_UNAVAILABLE)))
        assertEquals(ContractsStage.EDITING, machine.stage)
        assertFalse(machine.model!!.frozen)
        assertEquals(1L, machine.snapshot(false).operationGeneration)
        assertEquals(0L, machine.snapshot(false).deadlineMs)

        machine.changed()
        assertTrue(machine.notice.isEmpty())
        assertNull(machine.prepare(false, false))
        operation = dispatched(machine, 7)
        assertEquals(2L, operation.operationGeneration)
        assertEquals(3L, operation.draftGeneration)
        assertEquals(ContractsEvent.REJECTED, machine.saveFinished(outcome(operation, SaveOutcome.REJECTED, ErrorCode.CONFIG_INVALID_JSON)))
        assertEquals(ContractsStage.EDITING, machine.stage)
        assertFalse(machine.model!!.frozen)
        assertEquals(ErrorCode.CONFIG_INVALID_JSON.wireName, machine.notice)
    }

    @Test
    fun unknownRequiresCheckBeforeRepeat() {
        val machine = StandInContractsSaveMachine()
        assertTrue(toUnknown(machine))
        assertEquals(ContractsStage.SAVE_UNKNOWN, machine.stage)
        assertEquals(unknownText, machine.notice)
        assertTrue(machine.snapshot(false).unknown)
        assertTrue(machine.model!!.frozen)
        assertFalse(machine.canRepeat(false))
        assertNotNull(machine.prepare(true, false))
        assertNotNull(machine.prepare(false, false))

        // A busy check channel starts nothing.
        assertNull(machine.check(true))
        val before = machine.snapshot(false).checkGeneration
        val generation = startCheck(machine)
        assertEquals(before + 1, generation)
        assertEquals("Checking current configuration...", machine.notice)
        assertEquals(ContractsEvent.NONE, machine.checkFinished(generation - 1, requestId, reply("missing")))
        assertEquals(ContractsEvent.DIFFERENT, machine.checkFinished(generation, requestId, reply("missing")))
        assertEquals("The current configuration differs. Your draft has been kept.", machine.notice)
        assertTrue(machine.canRepeat(false))
        assertFalse(machine.canRepeat(true))
        // The consumed generation makes a duplicate reply stale.
        assertEquals(ContractsEvent.NONE, machine.checkFinished(generation, requestId, reply("missing")))

        assertNotNull(machine.prepare(true, true))
        assertNull(machine.prepare(true, false))
        val operation = dispatched(machine, 20)
        assertEquals(2L, operation.operationGeneration)
        assertEquals(ContractsEvent.SAVED, machine.saveFinished(outcome(operation, SaveOutcome.CONFIRMED, ErrorCode.NONE)))
    }

    @Test
    fun checkOutcomesAndNotices() {
        StandInContractsSaveMachine().let { machine ->
            assertTrue(toUnknown(machine))
            val candidate = machine.standInModel!!.candidate!!.text().reveal()
            assertEquals(ContractsEvent.MATCHED, machine.checkFinished(startCheck(machine), requestId, reply("valid", candidate)))
            assertEquals("The current configuration matches your changes.", machine.notice)
            assertFalse(machine.canRepeat(false))
            assertEquals(ContractsStage.SAVE_UNKNOWN, machine.stage)
        }
        StandInContractsSaveMachine().let { machine ->
            assertTrue(toUnknown(machine))
            assertEquals(ContractsEvent.DIFFERENT, machine.checkFinished(startCheck(machine), requestId, reply("valid", smallDocument)))
            assertTrue(machine.canRepeat(false))
        }
        StandInContractsSaveMachine().let { machine ->
            // A valid availability with broken text is refused by the reply check itself.
            assertTrue(toUnknown(machine))
            assertEquals(ContractsEvent.CHECK_FAILED, machine.checkFinished(startCheck(machine), requestId, reply("valid", "{")))
            assertFalse(machine.notice.isEmpty())
            assertFalse(machine.canRepeat(false))
        }
        StandInContractsSaveMachine().let { machine ->
            assertTrue(toUnknown(machine))
            assertEquals(ContractsEvent.CHECK_FAILED, machine.checkFinished(startCheck(machine), requestId, reply("invalid", "{")))
            assertEquals("The current configuration is invalid. Resolve this condition before saving again.\n" + ConfigReplies.CONFIG_PATH, machine.notice)
        }
        StandInContractsSaveMachine().let { machine ->
            assertTrue(toUnknown(machine))
            val error = "{\"ipcVersion\":1,\"requestId\":\"$requestId\",\"error\":{\"code\":\"config_unreadable\",\"description\":\"Configuration unreadable.\"," +
                "\"recoverable\":false,\"field\":\"configuration\",\"httpStatus\":null,\"suggestedAction\":\"Inspect the file.\",\"configPath\":\"/etc/phpvpn/config.json\"}}"
            assertEquals(ContractsEvent.CHECK_FAILED, machine.checkFinished(startCheck(machine), requestId, error.toByteArray()))
            assertEquals("config_unreadable", machine.notice)
            assertFalse(machine.canRepeat(false))
        }
        StandInContractsSaveMachine().let { machine ->
            assertTrue(toUnknown(machine))
            assertEquals(ContractsEvent.CHECK_FAILED, machine.checkFinished(startCheck(machine), requestId, null))
            assertEquals(ErrorCode.IPC_UNAVAILABLE.wireName, machine.notice)
        }
    }

    @Test
    fun invalidDraftLoadFailureAndClose() {
        val machine = StandInContractsSaveMachine()
        assertNotNull(machine.load(SecretText.of("{}"), "", ""))
        assertEquals(ContractsStage.CLOSED, machine.stage)
        assertNull(machine.model)
        assertFalse(machine.snapshot(false).open)

        assertNull(machine.load(SecretText.of(smallDocument), "", ""))
        assertEquals(ErrorCode.BUSY, machine.load(SecretText.of(smallDocument), "", ""))
        assertNull(machine.model!!.setEnabled("10.99.99.4", true))
        machine.changed()
        assertEquals(ContractsStage.INVALID, machine.stage)
        assertNotNull(machine.prepare(false, false))
        assertEquals(ErrorCode.CONFIG_INVALID_KEY.wireName, machine.notice)
        assertNull(machine.model!!.setEnabled("10.99.99.4", false))
        machine.changed()
        assertEquals(ContractsStage.EDITING, machine.stage)

        assertNull(machine.model!!.setKey("10.99.99.4", SecretText.of("b".repeat(64))))
        assertNull(machine.model!!.setEnabled("10.99.99.4", true))
        machine.changed()
        assertNull(machine.prepare(false, false))
        val operation = dispatched(machine, 1)
        val windowGeneration = machine.snapshot(false).windowGeneration
        val checkGeneration = machine.snapshot(false).checkGeneration
        machine.close()
        assertEquals(ContractsStage.CLOSED, machine.stage)
        assertNull(machine.model)
        assertTrue(machine.notice.isEmpty())
        val closed = machine.snapshot(false)
        assertTrue(!closed.open && !closed.saving && !closed.dirty)
        assertEquals(windowGeneration + 1, closed.windowGeneration)
        assertEquals(checkGeneration + 1, closed.checkGeneration)
        assertEquals(ContractsEvent.NONE, machine.saveFinished(outcome(operation, SaveOutcome.CONFIRMED, ErrorCode.NONE)))
        assertEquals(ContractsEvent.NONE, machine.checkFinished(closed.checkGeneration, requestId, reply("missing")))
        assertNull(machine.check(false))

        // Reopening yields a new window identity, so an old operation can never match.
        assertTrue(loadAndEdit(machine))
        assertEquals(windowGeneration + 2, machine.snapshot(false).windowGeneration)
    }

    @Test
    fun envelopeSizeMatchesTheSentRequest() {
        val document = smallDocument
        val request = IpcJson.encodeToString(
            SetConfigRequest.serializer(),
            SetConfigRequest(IPC_VERSION, requestId, IpcCommand.SET_CONFIG.wireName, SecretText.of(document)),
        )
        assertEquals(request.toByteArray().size.toLong(), StandInEnvelopeSizer.size(document).ok())
    }

    @Test
    fun saveResponseDescriptionsMatchErrorCpp() {
        val source = File(clientRoot(), "src/core/base/error.cpp").readText()
        val canonical = Regex("""\{ErrorCode::\w+,\s*"([^"]+)",\s*"((?:[^"\\]|\\.)*)",\s*(true|false)\}""").findAll(source)
            .associate { it.groupValues[1] to (it.groupValues[2] to it.groupValues[3].toBoolean()) }
        for ((code, description) in StandInSaveResponseClassifier.rejectingDescriptions) {
            val expected = canonical[code.wireName] ?: error("${code.wireName} missing in error.cpp")
            assertEquals(code.wireName, expected.first, description)
            assertEquals(code.wireName, expected.second, code.recoverable)
        }
    }

    @Test
    fun portableClassificationOfTheSaveEnvelope() {
        val operation = SaveOperation(1, 2, 3, 1000)
        val classifier = StandInSaveResponseClassifier
        fun classify(text: String?, attempted: Boolean = true, at: Long = 10, id: String = requestId) =
            classifier.classify(operation, id, text?.replace("\$ID", requestId)?.toByteArray(), attempted, at)
        assertEquals(SaveOutcome.CONFIRMED, classify(ConfigReplies.ACK).outcome)
        assertEquals(SaveOutcome.NOT_SENT, classify(null, attempted = false).outcome)
        assertEquals(ErrorCode.IPC_UNAVAILABLE, classify(null, attempted = false).error)
        assertEquals(SaveOutcome.UNKNOWN, classify(null).outcome)
        assertEquals(SaveOutcome.UNKNOWN, classify(ConfigReplies.ACK, at = 1000).outcome) // at the deadline
        assertEquals(SaveOutcome.UNKNOWN, classify(ConfigReplies.ACK, id = "not-a-uuid").outcome)
        assertEquals(SaveOutcome.UNKNOWN, classify(ConfigReplies.ACK.replace("\"ok\"", "\"yes\"")).outcome)
        assertEquals(SaveOutcome.UNKNOWN, classify(ConfigReplies.ACK.replace("\$ID", "00000000-0000-4000-8000-000000000009")).outcome)
        val rejected = classify(ConfigReplies.INVALID_JSON)
        assertEquals(SaveOutcome.REJECTED, rejected.outcome)
        assertEquals(ErrorCode.CONFIG_INVALID_JSON, rejected.error)
        val header = classify(ConfigReplies.INVALID_HEADER)
        assertEquals(SaveOutcome.REJECTED, header.outcome)
        assertEquals("masterSetup.httpHeaders line 2: empty line or missing colon", header.validationDescription)
        // A foreign description or recoverable flag does not prove a rejection.
        assertEquals(SaveOutcome.UNKNOWN, classify(ConfigReplies.error("config_invalid_json", "Something else.")).outcome)
        assertEquals(SaveOutcome.UNKNOWN, classify(ConfigReplies.error("config_invalid_json", "The configuration is not a valid JSON document.", true)).outcome)
        assertEquals(SaveOutcome.UNKNOWN, classify(ConfigReplies.WRITE_FAILED).outcome)
        assertTrue(classifier.isHttpHeadersErrorDescription("masterSetup.httpHeaders line 3: X-Foo is a duplicate header name"))
        assertFalse(classifier.isHttpHeadersErrorDescription("masterSetup.httpHeaders line 0: invalid field name"))
        assertFalse(classifier.isHttpHeadersErrorDescription("masterSetup.httpHeaders line 3: X Foo is a duplicate header name"))
    }

    private fun clientRoot(): File {
        var dir: File? = File("").absoluteFile
        while (dir != null && !File(dir, "src/core/base/error.cpp").isFile) dir = dir.parentFile
        return dir ?: error("client/src/core/base/error.cpp not found")
    }
}
