package phpvpn.android.bridge.controller

import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.Fixtures
import phpvpn.android.bridge.SequentialIds
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.JsonChecks
import phpvpn.android.bridge.ipc.MonotonicClock
import phpvpn.android.bridge.respondFixture

/** ContractsController over the [ContractsSaveMachineHandle] interface WP04 implements. */
class ContractsControllerTest {
    private fun TestScope.controller(transport: FakeIpcTransport, machine: FakeContractsSaveMachine): ContractsController {
        val clock = MonotonicClock { testScheduler.currentTime }
        return ContractsController(IpcClient(transport, SequentialIds(), clock = clock), machine, ReferenceSaveResponseClassifier, clock)
    }

    @Test
    fun opensOnlyAValidDocumentWithOneConfigRead() = runTest {
        val transport = FakeIpcTransport()
        transport.respondFixture("get-config-document", "config-document-valid.json")
        val machine = FakeContractsSaveMachine()
        val controller = controller(transport, machine)
        controller.open()
        assertEquals(Fixtures.text("default-config-document.json"), machine.loaded)
        assertEquals(ContractsStage.EDITING, controller.state.value.stage)
        assertEquals(Fixtures.CONFIG_PATH, controller.state.value.configPath)
        assertEquals(1, transport.requests.size)
        for (fixture in listOf("config-document-repairable.json", "config-document-missing.json")) {
            val other = FakeIpcTransport()
            other.respondFixture("get-config-document", fixture)
            val m = FakeContractsSaveMachine()
            val c = controller(other, m)
            c.open()
            assertEquals(null, m.loaded)
            assertTrue(c.state.value.unavailable != null && c.state.value.unavailable != ConfigAvailability.VALID)
        }
    }

    @Test
    fun saveSendsTheMachinePayloadAndFeedsTheClassifiedResult() = runTest {
        val transport = FakeIpcTransport()
        transport.respondFixture("get-config-document", "config-document-valid.json")
        transport.respondFixture("set-config", "ack.json")
        val machine = FakeContractsSaveMachine()
        val controller = controller(transport, machine)
        controller.open()
        assertEquals(ContractsEvent.SAVED, controller.save())
        val sent = transport.requests("set-config").single()
        assertEquals(machine.payload, JsonChecks.string(sent.json["config"]))
        assertEquals(30_000, sent.timeoutMs)
        assertEquals(SaveOutcome.CONFIRMED, machine.saveResults.single().outcome)
        assertEquals(ContractsStage.SAVED, controller.state.value.stage)
    }

    @Test
    fun unknownSaveIsCheckedThroughTheRawConfigDocumentReply() = runTest {
        val transport = FakeIpcTransport()
        transport.respondFixture("get-config-document", "config-document-valid.json")
        transport.respond("set-config") { FakeIpcTransport.Reply.Timeout }
        val machine = FakeContractsSaveMachine()
        val controller = controller(transport, machine)
        controller.open()
        assertEquals(ContractsEvent.UNKNOWN, controller.save())
        assertTrue(controller.state.value.canRepeat)
        assertEquals(ContractsEvent.MATCHED, controller.checkSaveResult())
        assertEquals(2, transport.requests("get-config-document").size)
        assertTrue(machine.checkedResponses.single()!!.contains("\"availability\":\"valid\""))
        assertEquals(1, transport.requests("set-config").size) // never an automatic second write
    }

    @Test
    fun prepareFailureSendsNothing() = runTest {
        val transport = FakeIpcTransport()
        transport.respondFixture("get-config-document", "config-document-valid.json")
        val machine = FakeContractsSaveMachine()
        machine.prepareError = phpvpn.android.bridge.errors.ErrorCode.CONFIG_INVALID_KEY
        val controller = controller(transport, machine)
        controller.open()
        assertEquals(ContractsEvent.NONE, controller.save())
        assertEquals("config_invalid_key", controller.state.value.failureCode)
        assertEquals(0, transport.requests("set-config").size)
        controller.close()
        assertTrue(machine.closed)
    }
}
