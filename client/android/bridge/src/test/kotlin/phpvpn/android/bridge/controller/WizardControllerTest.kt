package phpvpn.android.bridge.controller

import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.Fixtures
import phpvpn.android.bridge.SequentialIds
import phpvpn.android.bridge.ipc.CheckpointResult
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.bridge.ipc.FakeIpcTransport.Reply
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.IpcJson
import phpvpn.android.bridge.ipc.JsonChecks
import phpvpn.android.bridge.ipc.MonotonicClock
import phpvpn.android.bridge.ipc.OperationKind
import phpvpn.android.bridge.ipc.WizardFields
import phpvpn.android.bridge.ipc.WizardMode
import phpvpn.android.bridge.respondFixture

/**
 * WizardController against scripted replies on virtual time: steps and
 * attempts, the 15 s keepalive and 60 s lease (47), the disconnect gate (11)
 * with exactly one disconnect and one resume, failed HTTP detail cleanup with
 * cancel-attempt, attempt budgets, interruption, Finish with receipt and the
 * unknown commit (33) verified only by wizard-get-commit.
 */
class WizardControllerTest {
    private class Harness(scope: TestScope) {
        val transport = FakeIpcTransport()
        val clock = MonotonicClock { scope.testScheduler.currentTime }
        val controller = WizardController(IpcClient(transport, SequentialIds(), clock = clock), scope.backgroundScope, clock, SequentialIds(500))
        val fields: WizardFields = IpcJson.decodeFromJsonElement(WizardFields.serializer(), Fixtures.json("wizard-fields.json"))

        init {
            transport.respondFixture("wizard-open", "wizard-view-edit.json")
            transport.respondFixture("wizard-keepalive", "wizard-keepalive.json")
            transport.respondFixture("wizard-close", "wizard-close.json")
            transport.respondFixture("wizard-cancel-attempt", "wizard-cancel-attempt.json")
            transport.respondFixture("wizard-check-url", "wizard-started.json")
            transport.respondFixture("wizard-check-secret", "wizard-started.json")
            transport.respondFixture("wizard-check-access", "wizard-started.json")
            transport.respondFixture("wizard-finish", "wizard-started.json")
        }

        val step get() = controller.state.value.step

        fun lastCheck() = transport.requests.last { it.command.startsWith("wizard-check") }

        /** The operation fixture correlated with the last check request. */
        fun operation(fixture: String, epoch: Long = 0): Reply {
            val check = lastCheck()
            val text = Fixtures.text(fixture)
                .replace("\"clientAttemptId\":\"${Fixtures.ID}\"", "\"clientAttemptId\":\"${JsonChecks.string(check.json["clientAttemptId"])}\"")
                .replace("\"attemptSequence\":1", "\"attemptSequence\":${JsonChecks.uint64(check.json["attemptSequence"])}")
                .replace("\"connectionEpoch\":0", "\"connectionEpoch\":$epoch")
            return Reply.Text(text)
        }

        fun commitReply(fixture: String): Reply {
            val commit = JsonChecks.string(transport.requests.last { it.command == "wizard-finish" }.json["commitId"])!!
            return Reply.Text(Fixtures.text(fixture).replace("\"commitId\":\"${Fixtures.ID}\"", "\"commitId\":\"$commit\""))
        }

        fun count(command: String) = transport.requests(command).size
    }

    private fun TestScope.opened(): Harness {
        val h = Harness(this)
        assertTrue(h.controller.open())
        runCurrent()
        return h
    }

    @Test
    fun openOfflineRunsTheFourStepsAndFinishesWithAReceipt() = runTest {
        val h = opened()
        assertEquals(WizardStep.SERVER_URL, h.step)
        h.transport.respond("wizard-get-operation") { h.operation("wizard-operation-url-succeeded.json") }
        assertTrue(h.controller.verify(h.fields))
        runCurrent()
        assertTrue(h.controller.state.value.busy)
        advanceTimeBy(500); runCurrent()
        assertEquals(WizardStep.SERVER_SECRET, h.step)
        assertEquals(CheckpointResult.SUCCESS, h.controller.state.value.checks[0])

        h.transport.respond("wizard-get-operation") { h.operation("wizard-operation-secret-succeeded.json") }
        h.controller.verify(h.fields); runCurrent(); advanceTimeBy(500); runCurrent()
        assertEquals(WizardStep.IDENTITY, h.step)
        assertEquals("10.99.99.10", h.controller.state.value.suggestedIp)

        h.transport.respond("wizard-get-operation") { h.operation("wizard-operation-access-succeeded.json") }
        h.controller.verify(h.fields); runCurrent(); advanceTimeBy(500); runCurrent()
        assertEquals(WizardStep.CONTRACTS, h.step)
        assertEquals(2uL, h.controller.state.value.discardedRecordCount)
        // Each manual Next has its own increasing attemptSequence and a fresh attempt id.
        val sequences = h.transport.requests.filter { it.command.startsWith("wizard-check") }.map { JsonChecks.uint64(it.json["attemptSequence"]) }
        assertEquals(listOf(1uL, 2uL, 3uL), sequences)
        assertEquals(3, h.transport.requests.filter { it.command.startsWith("wizard-check") }.map { it.json["clientAttemptId"] }.toSet().size)

        h.transport.respond("wizard-get-operation") { h.commitReply("wizard-operation-finish-succeeded.json") }
        assertTrue(h.controller.finish(h.fields, createDefaultContracts = true))
        runCurrent()
        assertTrue(h.controller.state.value.committing)
        advanceTimeBy(500); runCurrent()
        assertEquals(WizardStep.FINISHED, h.step)
        assertNotNull(h.controller.state.value.receipt)
        assertEquals(1, h.count("wizard-finish"))
        assertEquals(1, h.count("wizard-close"))
    }

    @Test
    fun keepaliveEveryFifteenSecondsAndLocalLeaseExpiry() = runTest {
        val h = opened()
        advanceTimeBy(14_999); runCurrent()
        assertEquals(0, h.count("wizard-keepalive"))
        advanceTimeBy(250); runCurrent()
        assertEquals(1, h.count("wizard-keepalive"))
        advanceTimeBy(15_000); runCurrent()
        assertEquals(2, h.count("wizard-keepalive"))
        val lastContact = h.clock.nowMs()
        // From now on no reply arrives: a lost keepalive is not an error, the lease decides.
        h.transport.respond("wizard-keepalive") { Reply.Timeout }
        advanceTimeBy(59_000 - (h.clock.nowMs() - lastContact)); runCurrent()
        assertEquals(WizardStep.SERVER_URL, h.step)
        advanceTimeBy(1_250); runCurrent()
        assertEquals(WizardStep.SESSION_EXPIRED, h.step)
        assertEquals("wizard_session_expired", h.controller.state.value.error!!.code)
        // An expired session keeps no keepalive running and can be reopened.
        val keepalives = h.count("wizard-keepalive")
        advanceTimeBy(60_000); runCurrent()
        assertEquals(keepalives, h.count("wizard-keepalive"))
        assertTrue(h.controller.open())
        runCurrent()
        assertEquals(WizardStep.SERVER_URL, h.step)
        h.controller.endSession()
    }

    @Test
    fun keepaliveSessionExpiredLeadsToScreen47() = runTest {
        val h = opened()
        h.transport.respondFixture("wizard-keepalive", "wizard-error-session-expired.json")
        advanceTimeBy(15_250); runCurrent()
        assertEquals(WizardStep.SESSION_EXPIRED, h.step)
        assertFalse(h.controller.verify(h.fields))
    }

    @Test
    fun disconnectGateSendsOneDisconnectAndOneResume() = runTest {
        val h = Harness(this)
        h.transport.respondFixture("wizard-open", "wizard-view-edit-connected.json")
        h.transport.respondFixture("disconnect", "status-stopping.json")
        h.transport.enqueue("get-status", { Reply.Text(Fixtures.text("status-stopping.json")) })
        h.transport.respondFixture("get-status", "status-disconnected.json")
        h.transport.respondFixture("wizard-resume", "wizard-view-edit.json")
        h.controller.open(); runCurrent()
        assertEquals(WizardStep.DISCONNECT_GATE, h.step)
        assertFalse(h.controller.verify(h.fields))
        assertTrue(h.controller.disconnectAndContinue())
        runCurrent()
        assertTrue(h.controller.state.value.disconnecting)
        assertFalse(h.controller.disconnectAndContinue())
        advanceTimeBy(2_000); runCurrent()
        assertEquals(WizardStep.SERVER_URL, h.step)
        assertEquals(1, h.count("disconnect"))
        assertEquals(1, h.count("wizard-resume"))
        assertEquals("4", h.transport.requests("wizard-resume").single().json["connectionEpoch"].toString())
        assertTrue(h.transport.requests("get-status").all { it.timeoutMs == 400 })
        h.controller.endSession()
    }

    @Test
    fun offlineKnownGateOnlyResumes() = runTest {
        val h = Harness(this)
        h.transport.respondFixture("wizard-open", "wizard-view-edit.json")
        h.transport.respondFixture("wizard-resume", "wizard-view-edit.json")
        h.controller.open(); runCurrent()
        // An interrupted check moves to the gate; the service already reported disconnected.
        h.transport.respond("wizard-get-operation") { h.operation("wizard-operation-url-running.json", epoch = 9) }
        h.controller.verify(h.fields); runCurrent(); advanceTimeBy(500); runCurrent()
        assertEquals(WizardStep.DISCONNECT_GATE, h.step)
        assertEquals("wizard_connection_interrupted", h.controller.state.value.error!!.code)
        h.controller.endSession()
    }

    @Test
    fun failedCheckKeepsTheHttpDetailUntilInvalidatedThenCancelsTheAttemptOnce() = runTest {
        val h = opened()
        h.transport.respond("wizard-get-operation") {
            val reply = h.operation("wizard-operation-url-failed-diagnostic.json") as Reply.Text
            // The diagnostic fixture uses `other` as clientAttemptId; correlate it with the attempt.
            Reply.Text(reply.text.replace(Fixtures.OTHER + "\",\"attemptSequence\"", JsonChecks.string(h.lastCheck().json["clientAttemptId"]) + "\",\"attemptSequence\""))
        }
        h.controller.verify(h.fields); runCurrent(); advanceTimeBy(500); runCurrent()
        val state = h.controller.state.value
        assertEquals(WizardStep.SERVER_URL, state.step)
        assertEquals(CheckpointResult.FAILED, state.checks[0])
        assertEquals("response_invalid", state.error!!.code)
        assertNotNull(state.httpDiagnostic)
        assertEquals(0, h.count("wizard-cancel-attempt"))
        h.controller.invalidateFrom(OperationKind.URL)
        runCurrent()
        assertNull(h.controller.state.value.httpDiagnostic)
        val cancel = h.transport.requests("wizard-cancel-attempt").single()
        assertEquals(h.lastCheck().json["clientAttemptId"], cancel.json["clientAttemptId"])
        assertEquals(h.lastCheck().json["attemptSequence"], cancel.json["attemptSequence"])
        h.controller.invalidateFrom(OperationKind.URL); runCurrent()
        assertEquals(1, h.count("wizard-cancel-attempt"))
        h.controller.endSession()
    }

    @Test
    fun attemptBudgetExpiresIntoTimeoutAndCancelsTheAttempt() = runTest {
        val h = opened()
        h.transport.respond("wizard-get-operation") { h.operation("wizard-operation-url-running.json") }
        h.controller.verify(h.fields); runCurrent()
        // URL budget: 1 x responseTimeoutSeconds (30 s) + 15 s.
        advanceTimeBy(44_900); runCurrent()
        assertTrue(h.controller.state.value.busy)
        advanceTimeBy(500); runCurrent()
        val state = h.controller.state.value
        assertFalse(state.busy)
        assertEquals("timeout", state.error!!.code)
        assertEquals(CheckpointResult.FAILED, state.checks[0])
        assertEquals(1, h.count("wizard-cancel-attempt"))
        assertEquals(1, h.count("wizard-check-url")) // no automatic retry
        h.controller.endSession()
    }

    @Test
    fun lostFinishReplyIsCommitUnknownVerifiedByGetCommitOnly() = runTest {
        val h = opened()
        advanceToContracts(h)
        h.transport.respond("wizard-finish") { Reply.Timeout }
        h.controller.finish(h.fields, createDefaultContracts = false); runCurrent()
        assertEquals(WizardStep.COMMIT_UNKNOWN, h.step)
        h.transport.respond("wizard-get-commit") { h.commitReply("wizard-commit-pending.json") }
        assertTrue(h.controller.verifyCommit()); runCurrent()
        assertEquals(WizardStep.COMMIT_UNKNOWN, h.step)
        h.transport.respond("wizard-get-commit") { h.commitReply("wizard-commit-confirmed.json") }
        h.controller.verifyCommit(); runCurrent()
        assertEquals(WizardStep.FINISHED, h.step)
        assertEquals(1, h.count("wizard-finish"))
    }

    @Test
    fun commitDeadlineAndRestartedServiceNeverRepeatTheWrite() = runTest {
        val h = opened()
        advanceToContracts(h)
        h.transport.respond("wizard-get-operation") { Reply.Text(Fixtures.text("wizard-operation-url-running.json")) }
        h.transport.respond("wizard-finish") { Reply.Text(Fixtures.text("wizard-started.json")) }
        h.controller.finish(h.fields, createDefaultContracts = true); runCurrent()
        assertTrue(h.controller.state.value.committing)
        assertFalse(h.controller.close()) // refused while committing
        h.transport.respond("wizard-get-operation") { Reply.Timeout }
        advanceTimeBy(500); runCurrent()
        assertEquals(WizardStep.COMMIT_UNKNOWN, h.step)
        h.transport.respondFixture("wizard-get-commit", "wizard-error-session-expired.json")
        h.transport.respondFixture("get-config-state", "config-state.json")
        h.controller.verifyCommit(); runCurrent()
        assertEquals(WizardStep.COMMIT_UNKNOWN, h.step)
        assertEquals(Fixtures.CONFIG_PATH, h.controller.state.value.restartState!!.configPath)
        assertEquals(1, h.count("wizard-finish"))
        assertEquals(1, h.count("get-config-state"))
        h.controller.endSession()
    }

    @Test
    fun failedCommitReturnsToContractsWithTheError() = runTest {
        val h = opened()
        advanceToContracts(h)
        h.transport.respondFixture("wizard-finish", "wizard-error-commit-unknown.json")
        h.controller.finish(h.fields, createDefaultContracts = true); runCurrent()
        assertEquals(WizardStep.COMMIT_UNKNOWN, h.step)
        h.transport.respond("wizard-get-commit") { h.commitReply("wizard-commit-failed.json") }
        h.controller.verifyCommit(); runCurrent()
        assertEquals(WizardStep.CONTRACTS, h.step)
        assertEquals("config_write_failed", h.controller.state.value.error!!.code)
        h.controller.endSession()
    }

    @Test
    fun resetAndCloseUseTheControlChannel() = runTest {
        val h = opened()
        h.transport.respondFixture("wizard-reset", "wizard-view-reset.json")
        assertTrue(h.controller.reset()); runCurrent()
        assertEquals(WizardMode.RESET, h.controller.state.value.view!!.mode)
        assertFalse(h.controller.state.value.resetting)
        assertTrue(h.controller.close()); runCurrent()
        assertEquals(WizardStep.CLOSED, h.step)
        assertEquals(1000, h.transport.requests("wizard-close").single().timeoutMs)
        val keepalives = h.count("wizard-keepalive")
        advanceTimeBy(60_000); runCurrent()
        assertEquals(keepalives, h.count("wizard-keepalive"))
    }

    @Test
    fun loadErrorForUnsupportedService() = runTest {
        val h = Harness(this)
        h.transport.respondFixture("wizard-open", "legacy-unknown-command.json")
        h.controller.open(); runCurrent()
        assertEquals(WizardStep.LOAD_ERROR, h.step)
        assertEquals("wizard_unsupported", h.controller.state.value.error!!.code)
        h.controller.endSession()
    }

    private fun TestScope.advanceToContracts(h: Harness) {
        for (fixture in listOf("url", "secret", "access")) {
            h.transport.respond("wizard-get-operation") { h.operation("wizard-operation-$fixture-succeeded.json") }
            h.controller.verify(h.fields); runCurrent(); advanceTimeBy(500); runCurrent()
        }
        assertEquals(WizardStep.CONTRACTS, h.step)
    }
}
