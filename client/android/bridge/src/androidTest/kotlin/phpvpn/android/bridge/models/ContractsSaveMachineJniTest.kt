package phpvpn.android.bridge.models

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import org.junit.AfterClass
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import phpvpn.android.bridge.NativeRuntime
import phpvpn.android.bridge.controller.ContractsController
import phpvpn.android.bridge.controller.ContractsEvent
import phpvpn.android.bridge.controller.ContractsStage
import phpvpn.android.bridge.controller.SaveOperation
import phpvpn.android.bridge.controller.SaveOutcome
import phpvpn.android.bridge.controller.SaveResult
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.IdSource
import phpvpn.android.bridge.ipc.StrictJson
import phpvpn.android.bridge.jni.NativeContractsSaveMachine
import phpvpn.android.bridge.jni.NativeSaveResponseClassifier
import phpvpn.android.bridge.models.ModelDocuments.document
import phpvpn.android.bridge.models.ModelDocuments.secret
import phpvpn.android.bridge.runtime.RuntimeHarness

/**
 * The tray ContractsSaveMachine and classifySaveResponse through JNI (WP04),
 * ported from client/tests/unit/test_contracts_save_machine.cpp. The check
 * replies are the raw get-config-document replies the machine parses itself
 * (WizardIpc::parseReply), and the classification runs on real replies of the
 * runtime where the desktop test built them. Sender refusal is not portable:
 * the JNI dispatch has no injected sender (see the WP04 report).
 */
@RunWith(AndroidJUnit4::class)
class ContractsSaveMachineJniTest {
    private val requestId = "12345678-1234-4234-8234-123456789abc"
    private val unknownText = "The save result is unknown. The file may already have changed. Check the current configuration before saving again."
    private val configPath = "/data/user/0/io.github.jaromirsivic.phpvpn/no_backup/phpvpn/config/config.json"
    private val small = document("10.99.99.2", "10.99.99.0/29")
    private val deadline = 30_000L

    private fun loadAndEdit(machine: NativeContractsSaveMachine) {
        assertNull(machine.load(secret(small), configPath, "rev-1"))
        val model = machine.model!!
        assertNull(model.setKey("10.99.99.3", secret("a".repeat(64))))
        assertNull(model.setEnabled("10.99.99.3", true))
        machine.changed()
    }

    private fun result(operation: SaveOperation, outcome: SaveOutcome, error: ErrorCode) = SaveResult(operation, outcome, error)

    private fun dispatched(machine: NativeContractsSaveMachine, nowMs: Long): SaveOperation {
        val prepared = machine.dispatch(nowMs)
        assertNotNull("the machine sent nothing", prepared)
        prepared!!.document.wipe()
        return prepared.operation
    }

    private fun toUnknown(machine: NativeContractsSaveMachine) {
        loadAndEdit(machine)
        assertNull(machine.prepare(false, false))
        assertEquals(ContractsEvent.UNKNOWN, machine.saveFinished(result(dispatched(machine, 10), SaveOutcome.UNKNOWN, ErrorCode.TIMEOUT)))
    }

    /** A get-config-document reply as the service builds it. */
    private fun configReply(id: String, availability: String, text: String?): ByteArray {
        val result = JsonObject(
            linkedMapOf(
                "configPath" to JsonPrimitive(configPath),
                "availability" to JsonPrimitive(availability),
                "reasonCode" to if (availability == "valid") kotlinx.serialization.json.JsonNull else JsonPrimitive("config_missing"),
                "document" to if (text == null) kotlinx.serialization.json.JsonNull else JsonPrimitive(text),
                "revisionToken" to if (text == null) kotlinx.serialization.json.JsonNull else JsonPrimitive("revision"),
            ),
        )
        return JsonObject(linkedMapOf("ipcVersion" to JsonPrimitive(1), "requestId" to JsonPrimitive(id), "result" to result)).toString().encodeToByteArray()
    }

    @Test
    fun ConfirmedSaveCarriesIdentityAndDeadline() {
        val machine = NativeContractsSaveMachine()
        try {
            assertEquals(ContractsStage.CLOSED, machine.stage)
            loadAndEdit(machine)
            assertEquals(ContractsStage.EDITING, machine.stage)
            var snapshot = machine.snapshot(false)
            assertTrue(snapshot.open && snapshot.dirty && !snapshot.saving && !snapshot.unknown && !snapshot.checking)
            assertEquals(1L, snapshot.windowGeneration)
            assertEquals(2L, snapshot.draftGeneration)
            assertTrue(machine.snapshot(true).checking)

            assertNull(machine.prepare(false, false))
            assertTrue(machine.model!!.frozen)
            assertEquals(ErrorCode.BUSY, machine.prepare(false, false))
            val prepared = machine.dispatch(1000)!!
            assertEquals(SaveOperation(1, 2, 1, 1000 + deadline), prepared.operation)
            assertTrue(prepared.document.reveal().contains("\"10.99.99.3\""))
            prepared.document.wipe()
            assertEquals(ContractsStage.SAVING, machine.stage)
            assertTrue(machine.saving)
            assertEquals("Saving configuration...", machine.notice)
            snapshot = machine.snapshot(false)
            assertTrue(snapshot.saving)
            assertEquals(1L, snapshot.operationGeneration)
            assertEquals(1000 + deadline, snapshot.deadlineMs)
            assertNull(machine.dispatch(1000))
            machine.changed()
            assertEquals(2L, machine.snapshot(false).draftGeneration)

            val foreign = prepared.operation.copy(operationGeneration = prepared.operation.operationGeneration + 1)
            assertEquals(ContractsEvent.NONE, machine.saveFinished(result(foreign, SaveOutcome.CONFIRMED, ErrorCode.NONE)))
            assertEquals(ContractsStage.SAVING, machine.stage)
            assertEquals(ContractsEvent.SAVED, machine.saveFinished(result(prepared.operation, SaveOutcome.CONFIRMED, ErrorCode.NONE)))
            assertEquals(ContractsStage.SAVED, machine.stage)
            assertEquals("Changes saved. Checking connection status...", machine.notice)
            snapshot = machine.snapshot(false)
            assertTrue(snapshot.open && !snapshot.dirty && !snapshot.saving)
            assertEquals(0L, snapshot.deadlineMs)
            assertEquals(ContractsEvent.NONE, machine.saveFinished(result(prepared.operation, SaveOutcome.CONFIRMED, ErrorCode.NONE)))
        } finally {
            machine.release()
        }
    }

    @Test
    fun RejectionAndNotSentKeepTheDraft() {
        val machine = NativeContractsSaveMachine()
        try {
            assertNull(machine.dispatch(5))
            loadAndEdit(machine)
            assertNull(machine.dispatch(5))
            assertNull(machine.prepare(false, false))
            var operation = dispatched(machine, 7)
            assertEquals(1L, operation.operationGeneration)
            assertEquals(2L, operation.draftGeneration)
            assertEquals(ContractsEvent.REJECTED, machine.saveFinished(result(operation, SaveOutcome.REJECTED, ErrorCode.CONFIG_INVALID_JSON)))
            assertEquals(ContractsStage.EDITING, machine.stage)
            assertFalse(machine.model!!.frozen)
            // The notice is errorCodeDescription(config_invalid_json): the runtime's own text for that code.
            RuntimeHarness.restart()
            val invalidJson = RuntimeHarness.json(RuntimeHarness.raw(RuntimeHarness.setConfig("{"), 30_000)!!)["error"] as JsonObject
            assertEquals("config_invalid_json", (invalidJson["code"] as JsonPrimitive).content)
            assertEquals((invalidJson["description"] as JsonPrimitive).content, machine.notice)
            machine.changed()
            assertTrue(machine.notice.isEmpty())

            assertNull(machine.prepare(false, false))
            operation = dispatched(machine, 9)
            assertEquals(2L, operation.operationGeneration)
            assertEquals(3L, operation.draftGeneration)
            assertEquals(ContractsEvent.REJECTED, machine.saveFinished(result(operation, SaveOutcome.NOT_SENT, ErrorCode.IPC_UNAVAILABLE)))
            assertEquals(ContractsStage.EDITING, machine.stage)
            assertTrue(machine.notice.isNotEmpty() && machine.notice != (invalidJson["description"] as JsonPrimitive).content)
        } finally {
            machine.release()
        }
    }

    @Test
    fun UnknownRequiresCheckBeforeRepeat() {
        val machine = NativeContractsSaveMachine()
        try {
            toUnknown(machine)
            assertEquals(ContractsStage.SAVE_UNKNOWN, machine.stage)
            assertEquals(unknownText, machine.notice)
            assertTrue(machine.snapshot(false).unknown)
            assertTrue(machine.model!!.frozen)
            assertFalse(machine.canRepeat(false))
            assertEquals(ErrorCode.BUSY, machine.prepare(true, false))
            assertEquals(ErrorCode.BUSY, machine.prepare(false, false))
            // A busy check channel starts nothing.
            assertNull(machine.check(true))
            val before = machine.snapshot(false).checkGeneration
            val generation = machine.check(false)!!
            assertEquals(before + 1, generation)
            assertEquals("Checking current configuration...", machine.notice)
            val missing = RuntimeHarness.fixture("config-document-missing.json", requestId).encodeToByteArray()
            assertEquals(ContractsEvent.NONE, machine.checkFinished(generation - 1, requestId, missing))
            assertEquals(ContractsEvent.DIFFERENT, machine.checkFinished(generation, requestId, missing))
            assertEquals("The current configuration differs. Your draft has been kept.", machine.notice)
            assertTrue(machine.canRepeat(false))
            assertFalse(machine.canRepeat(true))
            assertEquals(ContractsEvent.NONE, machine.checkFinished(generation, requestId, missing))

            assertEquals(ErrorCode.BUSY, machine.prepare(true, true))
            assertNull(machine.prepare(true, false))
            val operation = dispatched(machine, 20)
            assertEquals(2L, operation.operationGeneration)
            assertEquals(ContractsEvent.SAVED, machine.saveFinished(result(operation, SaveOutcome.CONFIRMED, ErrorCode.NONE)))
        } finally {
            machine.release()
        }
    }

    @Test
    fun CheckOutcomesAndNotices() {
        fun unknownMachine(): NativeContractsSaveMachine = NativeContractsSaveMachine().also { toUnknown(it) }
        // The candidate is the payload the machine sent; recover it through a second dispatch path.
        unknownMachine().let { machine ->
            try {
                val candidate = run {
                    // The frozen candidate is what dispatch sent: rebuild it by repeating the same edit.
                    val probe = NativeContractsSaveMachine()
                    try {
                        loadAndEdit(probe)
                        assertNull(probe.prepare(false, false))
                        probe.dispatch(1)!!.document.let { text -> text.reveal().also { text.wipe() } }
                    } finally {
                        probe.release()
                    }
                }
                assertEquals(ContractsEvent.MATCHED, machine.checkFinished(machine.check(false)!!, requestId, configReply(requestId, "valid", candidate)))
                assertEquals("The current configuration matches your changes.", machine.notice)
                assertFalse(machine.canRepeat(false))
                assertEquals(ContractsStage.SAVE_UNKNOWN, machine.stage)
            } finally {
                machine.release()
            }
        }
        unknownMachine().let { machine ->
            try {
                assertEquals(ContractsEvent.DIFFERENT, machine.checkFinished(machine.check(false)!!, requestId, configReply(requestId, "valid", small)))
                assertTrue(machine.canRepeat(false))
            } finally {
                machine.release()
            }
        }
        unknownMachine().let { machine ->
            try {
                // A reply whose declared availability does not match the text is malformed.
                assertEquals(ContractsEvent.CHECK_FAILED, machine.checkFinished(machine.check(false)!!, requestId, configReply(requestId, "valid", "{")))
                assertTrue(machine.notice.isNotEmpty())
                assertFalse(machine.canRepeat(false))
            } finally {
                machine.release()
            }
        }
        unknownMachine().let { machine ->
            try {
                val invalid = RuntimeHarness.fixture("config-document-invalid.json", requestId).encodeToByteArray()
                assertEquals(ContractsEvent.CHECK_FAILED, machine.checkFinished(machine.check(false)!!, requestId, invalid))
                val path = (((StrictJson.parse(invalid) as JsonObject)["result"] as JsonObject)["configPath"] as JsonPrimitive).content
                assertEquals("The current configuration is invalid. Resolve this condition before saving again.\n$path", machine.notice)
            } finally {
                machine.release()
            }
        }
        unknownMachine().let { machine ->
            try {
                val error = RuntimeHarness.fixture("wizard-error-directory-security.json", requestId).encodeToByteArray()
                assertEquals(ContractsEvent.CHECK_FAILED, machine.checkFinished(machine.check(false)!!, requestId, error))
                val fields = ((StrictJson.parse(error) as JsonObject)["error"] as JsonObject)
                val expected = listOfNotNull(
                    (fields["description"] as JsonPrimitive).content,
                    (fields["suggestedAction"] as JsonPrimitive).content,
                    (fields["configPath"] as? JsonPrimitive)?.takeIf { it.isString }?.content,
                ).joinToString("\n")
                assertEquals(expected, machine.notice)
                assertFalse(machine.canRepeat(false))
            } finally {
                machine.release()
            }
        }
        unknownMachine().let { machine ->
            try {
                // No reply at all (timeout): the safe ipc_unavailable text.
                assertEquals(ContractsEvent.CHECK_FAILED, machine.checkFinished(machine.check(false)!!, requestId, null))
                assertTrue(machine.notice.contains("\n"))
            } finally {
                machine.release()
            }
        }
    }

    @Test
    fun InvalidDraftLoadFailureAndClose() {
        val machine = NativeContractsSaveMachine()
        try {
            assertTrue(machine.load(secret("{}"), "", "") != null)
            assertEquals(ContractsStage.CLOSED, machine.stage)
            assertNull(machine.model)
            assertFalse(machine.snapshot(false).open)

            assertNull(machine.load(secret(small), "", ""))
            assertEquals(ErrorCode.BUSY, machine.load(secret(small), "", ""))
            assertNull(machine.model!!.setEnabled("10.99.99.4", true))
            machine.changed()
            assertEquals(ContractsStage.INVALID, machine.stage)
            assertEquals(ErrorCode.CONFIG_INVALID_KEY, machine.prepare(false, false))
            assertTrue(machine.notice.isNotEmpty())
            assertNull(machine.model!!.setEnabled("10.99.99.4", false))
            machine.changed()
            assertEquals(ContractsStage.EDITING, machine.stage)

            assertNull(machine.model!!.setKey("10.99.99.4", secret("b".repeat(64))))
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
            assertEquals(ContractsEvent.NONE, machine.saveFinished(result(operation, SaveOutcome.CONFIRMED, ErrorCode.NONE)))
            assertNull(machine.check(false))

            loadAndEdit(machine)
            assertEquals(windowGeneration + 2, machine.snapshot(false).windowGeneration)
        } finally {
            machine.release()
        }
    }

    @Test
    fun SaveResponseClassifiesRealRepliesOfTheRuntime() {
        RuntimeHarness.restart()
        val op = SaveOperation(1, 2, 3, 100)
        val ack = RuntimeHarness.fixture("ack.json", requestId).encodeToByteArray()
        val confirmed = NativeSaveResponseClassifier.classify(op, requestId, ack, true, 99)
        assertEquals(SaveOutcome.CONFIRMED, confirmed.outcome)
        assertEquals(ErrorCode.NONE, confirmed.error)
        assertEquals(op, confirmed.operation)
        assertEquals(SaveOutcome.UNKNOWN, NativeSaveResponseClassifier.classify(op, requestId, ack, true, 100).outcome)
        assertEquals(SaveOutcome.UNKNOWN, NativeSaveResponseClassifier.classify(SaveOperation(1, 2, 3, 0), requestId, ack, true, 0).outcome)
        assertEquals(SaveOutcome.UNKNOWN, NativeSaveResponseClassifier.classify(op, "not-a-uuid", ack, true, 99).outcome)
        val notSent = NativeSaveResponseClassifier.classify(op, requestId, null, false, 120)
        assertEquals(SaveOutcome.NOT_SENT, notSent.outcome)
        assertEquals(ErrorCode.IPC_UNAVAILABLE, notSent.error)
        assertEquals(SaveOutcome.UNKNOWN, NativeSaveResponseClassifier.classify(op, requestId, null, true, 99).outcome)

        // The runtime's own rejection of an invalid CIDR, byte for byte.
        val id = IdSource.RANDOM.next()
        val reply = RuntimeHarness.raw(RuntimeHarness.setConfig(RuntimeHarness.document().replace("10.99.99.0/24", "10.99.99.0/16"), id), 30_000)!!
        val rejected = NativeSaveResponseClassifier.classify(op, id, reply.encodeToByteArray(), true, 99)
        assertEquals(SaveOutcome.REJECTED, rejected.outcome)
        assertEquals(ErrorCode.CONFIG_INVALID_CIDR, rejected.error)
        assertTrue(rejected.validationDescription.isEmpty())
        val writeFailed = """{"ipcVersion":1,"requestId":"$requestId","error":{"code":"config_write_failed","description":"x","recoverable":true}}"""
        assertEquals(SaveOutcome.UNKNOWN, NativeSaveResponseClassifier.classify(op, requestId, writeFailed.encodeToByteArray(), true, 99).outcome)
        assertEquals(SaveOutcome.UNKNOWN, NativeSaveResponseClassifier.classify(op, requestId, "{}".encodeToByteArray(), true, 99).outcome)
    }

    @Test
    fun ContractsControllerDrivesTheNativeMachineThroughTheRuntime() = runBlocking {
        RuntimeHarness.restart()
        val client = RuntimeHarness.client()
        val machine = NativeContractsSaveMachine()
        try {
            val controller = ContractsController(client, machine, NativeSaveResponseClassifier)
            controller.open()
            assertEquals(ContractsStage.EDITING, controller.state.value.stage)
            val model = controller.model!!
            assertNull(model.setKey("10.99.99.3", secret("c".repeat(64))))
            assertNull(model.setEnabled("10.99.99.3", true))
            controller.changed()
            assertEquals(ContractsEvent.SAVED, controller.save())
            val stored = RuntimeHarness.configFile.readText()
            assertTrue(stored, stored.contains("\"10.99.99.3\"") && stored.contains("c".repeat(64)))
            controller.close()
            assertEquals(ContractsStage.CLOSED, machine.stage)
        } finally {
            machine.release()
        }
    }

    companion object {
        @JvmStatic
        @AfterClass
        fun stopRuntime() = NativeRuntime.stop()
    }
}
