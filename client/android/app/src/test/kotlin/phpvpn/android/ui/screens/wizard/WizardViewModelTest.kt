package phpvpn.android.ui.screens.wizard

import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.ViewModelStore
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import phpvpn.android.bridge.controller.WizardStep
import phpvpn.android.bridge.ipc.CheckpointResult
import phpvpn.android.bridge.ipc.FakeIpcTransport.Reply
import phpvpn.android.bridge.ipc.HttpHeaders
import phpvpn.android.bridge.ipc.IdSource
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.JsonChecks
import phpvpn.android.bridge.ipc.MonotonicClock
import phpvpn.android.bridge.ipc.OperationKind
import phpvpn.android.debug.FakeWizardService
import phpvpn.android.debug.FakeWizardService.Check
import phpvpn.android.debug.FakeWizardService.Finish
import phpvpn.android.ui.navigation.SaveOrigin
import phpvpn.android.ui.screens.save.SaveOutcomePhase
import phpvpn.android.ui.screens.save.SaveOutcomeRegistry

/**
 * WizardViewModel over the scripted [FakeWizardService] on virtual time:
 * every step and its screen navigation, local validation, each error branch
 * of the screen map (18), the HTTP detail and its invalidation, lease expiry
 * (47), commit unknown (33) with a confirmed, failed, unknown and restarted
 * receipt, the gate (11) with status transitions, the foreign Connect
 * (epoch change), Reset (40), the dirty flag of 32 and the best-effort
 * wizard-close.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class WizardViewModelTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    private class Harness(scope: TestScope, configure: FakeWizardService.() -> Unit) {
        val service = FakeWizardService().apply(configure)
        val clock = MonotonicClock { scope.testScheduler.currentTime }
        private var nextId = 0x500
        private val ids = IdSource { "00000000-0000-4000-8000-%012x".format(nextId++) }
        val dependencies = WizardDependencies(
            client = IpcClient(service.transport, ids, clock = clock),
            backgroundScope = scope.backgroundScope,
            connection = null,
            clock = clock,
            ids = ids,
        )
        val store = ViewModelStore()
        val vm: WizardViewModel = ViewModelProvider.create(
            store,
            viewModelFactory { initializer { WizardViewModel(dependencies) } },
        )[WizardViewModel::class]

        val state get() = vm.state.value
        val target get() = vm.navigation.value?.target

        fun count(command: String) = service.requests(command).size

        fun lastFields(command: String) = service.requests(command).last().json["fields"] as kotlinx.serialization.json.JsonObject
    }

    private val harnesses = mutableListOf<Harness>()

    /**
     * runTest that clears every ViewModel at the end: the controller ticker
     * runs on the test scheduler and would otherwise keep virtual time busy.
     */
    private fun wizardTest(body: suspend TestScope.() -> Unit) = runTest(dispatcher) {
        try {
            body()
        } finally {
            harnesses.forEach { it.store.clear() }
            harnesses.clear()
            runCurrent()
        }
    }

    private fun TestScope.harness(configure: FakeWizardService.() -> Unit = {}): Harness {
        val h = Harness(this, configure)
        harnesses += h
        runCurrent()
        return h
    }

    /** One controller tick period plus the 500 ms operation poll. */
    private fun TestScope.poll() {
        advanceTimeBy(600)
        runCurrent()
    }

    private fun TestScope.passUrl(h: Harness) {
        h.vm.setUrl("https://vpn.example.com/phpvpn.php")
        assertTrue(h.vm.verify())
        runCurrent()
        poll()
    }

    private fun TestScope.passSecret(h: Harness) {
        assertTrue(h.vm.verify())
        runCurrent()
        poll()
    }

    @Test
    fun mainPathFromTheUrlToTheSavedConfiguration() = wizardTest {
        val h = harness()
        assertEquals(WizardStep.SERVER_URL, h.state.step)
        assertEquals(WizardNavTarget.URL, h.target)
        assertFalse("a loaded draft is clean", h.state.dirty)
        assertSame(h.vm.saveOutcome, SaveOutcomeRegistry.source(SaveOrigin.Wizard))

        passUrl(h)
        assertTrue(h.state.dirty)
        assertEquals(WizardStep.SERVER_SECRET, h.state.step)
        assertEquals(WizardNavTarget.SECRET, h.target)
        assertEquals(CheckpointResult.SUCCESS, h.state.checks[0])

        passSecret(h)
        assertEquals(WizardStep.IDENTITY, h.state.step)
        assertEquals(WizardNavTarget.IDENTITY, h.target)
        // Automatic detection takes the verified suggestion; the default key follows the IP.
        assertEquals("10.99.99.10", h.state.virtualIp)
        assertSecret("default key of the suggestion", WizardRules.defaultAccessKey("10.99.99.10").reveal(), h.state.accessKey)

        assertTrue(h.vm.verify())
        runCurrent()
        val access = h.lastFields("wizard-check-access")
        assertEquals("10.99.99.10", JsonChecks.string(access["virtualIp"]))
        assertEquals(true, JsonChecks.bool(access["useDefaultAccessKeyInWizard"]))
        poll()
        assertEquals(WizardStep.CONTRACTS, h.state.step)
        assertEquals(WizardNavTarget.FINISH, h.target)
        assertEquals(13, h.state.missingContracts)
        assertEquals(13, h.state.contractsToAdd)

        assertTrue(h.vm.finish())
        runCurrent()
        assertTrue(h.state.committing)
        assertFalse("no edit while committing", h.vm.setCreateContracts(false))
        assertFalse("no Cancel while committing", h.vm.leave())
        poll()
        assertEquals(WizardStep.FINISHED, h.state.step)
        assertEquals(WizardNavTarget.SAVED, h.target)
        assertEquals(SaveOutcomePhase.CONFIRMED, h.vm.saveOutcome.state.value.phase)
        assertEquals(WizardSavedSummary("https://vpn.example.com/phpvpn.php", "10.99.99.10", "10.99.99.0/24"), WizardSavedStore.latest)
        assertEquals(1, h.count("wizard-finish"))
        assertEquals(true, JsonChecks.bool(h.service.requests("wizard-finish").last().json["createDefaultContracts"]))
        assertEquals("the confirmed Finish sends exactly one close", 1, h.count("wizard-close"))
        h.store.clear()
        runCurrent()
        assertEquals("no second close after a confirmed Finish", 1, h.count("wizard-close"))
        assertNull(SaveOutcomeRegistry.source(SaveOrigin.Wizard))
    }

    @Test
    fun localValidationNeverReachesTheService() = wizardTest {
        val h = harness()
        // A new draft has the placeholder URL: rejected before DNS with its own advice.
        h.vm.setUrl(FakeWizardService.PLACEHOLDER_URL)
        assertFalse(h.vm.verify())
        assertEquals(LocalProblem.URL_PLACEHOLDER, h.state.localProblem)
        assertEquals(WizardViewModel.FIELD_URL, h.state.invalidField)
        h.vm.setUrl("https://user@vpn.example.com/phpvpn.php")
        assertNull("an edit clears the local problem", h.state.localProblem)
        assertFalse(h.vm.verify())
        assertEquals(LocalProblem.URL_INVALID, h.state.localProblem)
        assertEquals(0, h.count("wizard-check-url"))
        assertNull("no navigation to 18 for a local problem", h.target.takeIf { it == WizardNavTarget.ERROR })

        passUrl(h)
        passSecret(h)
        h.vm.setAutoDetectIp(false)
        h.vm.setIp("10.99.99.255")
        assertFalse(h.vm.verify())
        assertEquals(LocalProblem.IP_INVALID, h.state.localProblem)
        assertEquals(WizardViewModel.FIELD_IP, h.state.invalidField)
        h.vm.setIp("10.99.99.9")
        h.vm.setDefaultKey(false)
        h.vm.setAccessKey("A".repeat(64))
        assertFalse(h.vm.verify())
        assertEquals(LocalProblem.KEY_INVALID, h.state.localProblem)
        assertEquals(WizardViewModel.FIELD_KEY, h.state.invalidField)
        assertEquals(0, h.count("wizard-check-access"))
    }

    @Test
    fun automaticIpWithoutSuggestionAsksForManualEntry() = wizardTest {
        val h = harness { suggestedIp = null }
        passUrl(h)
        passSecret(h)
        assertEquals(WizardStep.IDENTITY, h.state.step)
        assertNull(h.state.suggestedIp)
        assertEquals("", h.state.virtualIp)
        assertFalse(h.vm.verify())
        assertEquals(LocalProblem.NO_SUGGESTION, h.state.localProblem)
        assertFalse("the field is disabled while automatic", h.vm.setIp("10.99.99.5") && h.state.virtualIp == "10.99.99.5")
        h.vm.setAutoDetectIp(false)
        h.vm.setIp("10.99.99.5")
        assertSecret("default key of the manual IP", WizardRules.defaultAccessKey("10.99.99.5").reveal(), h.state.accessKey)
        assertTrue(h.vm.verify())
    }

    /** Each error branch of the screen map goes to 18 once, with the right field marked. */
    @Test
    fun everyCheckFailureShowsTheVerificationError() = wizardTest {
        data class Case(val kind: String, val check: Check, val field: String?)
        val cases = listOf(
            Case("url", Check.Fail("response_invalid", "serverUrl", httpStatus = 200, diagnostic = true), null),
            Case("url", Check.Fail("server_version_incompatible", "serverUrl", description = "x Client version 1.0.3, server version 2.0.0."), WizardViewModel.FIELD_URL),
            Case("secret", Check.Fail("crypto_tag_mismatch", "serverSecret"), WizardViewModel.FIELD_SECRET),
            Case("secret", Check.Fail("server_unauthorized", "serverSecret", httpStatus = 401), WizardViewModel.FIELD_SECRET),
            Case("access", Check.Fail("server_unauthorized", "accessKey", httpStatus = 401), WizardViewModel.FIELD_KEY),
            Case("access", Check.Fail("wizard_contract_self_conflict", "virtualIp"), WizardViewModel.FIELD_IP),
            Case("access", Check.Fail("transport_connect_failed", null), null),
        )
        for (case in cases) {
            val h = harness { checks[case.kind] = case.check }
            if (case.kind != "url") passUrl(h)
            if (case.kind == "access") passSecret(h)
            if (case.kind == "url") h.vm.setUrl("https://vpn.example.com/phpvpn.php")
            val before = h.state.step
            assertTrue(h.vm.verify())
            runCurrent()
            poll()
            val failure = case.check as Check.Fail
            assertEquals(case.toString(), WizardNavTarget.ERROR, h.target)
            assertEquals("the step stays", before, h.state.step)
            assertEquals(failure.code, h.state.error?.code)
            assertEquals(CheckpointResult.FAILED, h.state.checks[OperationKind.valueOf(case.kind.uppercase()).ordinal])
            assertEquals(case.toString(), case.field, h.state.invalidField)
            assertEquals("HTTP detail only when captured", failure.diagnostic, h.state.httpDiagnostic != null)
            // Leaving 18 and coming back does not show it again for the same attempt.
            h.vm.consumeNavigation(h.vm.navigation.value!!)
            poll()
            assertNull(h.vm.navigation.value)
            h.store.clear()
        }
    }

    @Test
    fun timeoutKeepsTheInputsValidAndCancelsTheAttempt() = wizardTest {
        val h = harness { checks["url"] = Check.Pending }
        h.vm.setUrl("https://vpn.example.com/phpvpn.php")
        assertTrue(h.vm.verify())
        runCurrent()
        assertTrue(h.state.busy)
        assertFalse("inputs are locked during the check", h.vm.setUrl("https://other.example.com/phpvpn.php"))
        // URL budget: 1 x 30 s + 15 s.
        advanceTimeBy(44_000)
        runCurrent()
        assertTrue(h.state.busy)
        advanceTimeBy(1_500)
        runCurrent()
        assertFalse(h.state.busy)
        assertEquals("timeout", h.state.error?.code)
        assertEquals(WizardNavTarget.ERROR, h.target)
        assertNull("a timeout never marks an input", h.state.invalidField)
        assertEquals("https://vpn.example.com/phpvpn.php", h.state.serverUrl)
        assertEquals(1, h.count("wizard-check-url"))
        assertEquals(1, h.count("wizard-cancel-attempt"))
        // No automatic retry: the next attempt is the user's.
        advanceTimeBy(60_000)
        runCurrent()
        assertEquals(1, h.count("wizard-check-url"))
        h.store.clear()
    }

    /** Port of DiagnosticCorrelatesEveryIdentityAndClearsOnAllInvalidations (ViewModel side). */
    @Test
    fun httpDetailDisappearsWithEveryInvalidation() = wizardTest {
        val invalidations: List<Pair<String, (Harness) -> Unit>> = listOf(
            "new check" to { h -> h.vm.verify() },
            "URL edit" to { h -> h.vm.setUrl("https://other.example.com/phpvpn.php") },
            "headers" to { h -> h.vm.acceptHeaders("Cookie: synthetic=1\nUser-Agent: test") },
            "reset" to { h -> h.vm.reset() },
            "explicit dismiss" to { h -> h.vm.dismissHttpDiagnostic() },
            "leave" to { h -> h.vm.leave() },
        )
        for ((name, invalidate) in invalidations) {
            val h = harness { checks["url"] = Check.Fail("response_invalid", "serverUrl", httpStatus = 200, diagnostic = true) }
            h.vm.setUrl("https://vpn.example.com/phpvpn.php")
            h.vm.verify()
            runCurrent()
            poll()
            assertNotNull(name, h.state.httpDiagnostic)
            val cancels = h.count("wizard-cancel-attempt")
            invalidate(h)
            runCurrent()
            assertNull(name, h.state.httpDiagnostic)
            if (name != "leave") assertEquals("$name: one cancel-attempt for the failed detail", cancels + 1, h.count("wizard-cancel-attempt"))
            h.store.clear()
        }
    }

    @Test
    fun secretEditAndBackClearTheDetailOfTheSecretStep() = wizardTest {
        for (back in listOf(false, true)) {
            val h = harness { checks["secret"] = Check.Fail("response_invalid", "serverSecret", diagnostic = true) }
            passUrl(h)
            h.vm.verify()
            runCurrent()
            poll()
            assertNotNull(h.state.httpDiagnostic)
            if (back) {
                assertTrue(h.vm.back())
                assertEquals(WizardStep.SERVER_URL, h.state.step)
                assertNull("Back resets the error of the step left", h.state.error)
            } else {
                h.vm.setSecret("changed")
            }
            runCurrent()
            assertNull(h.state.httpDiagnostic)
            h.store.clear()
        }
    }

    @Test
    fun leaseExpiryLeadsToSessionExpiredAndBlocksTheSave() = wizardTest {
        val h = harness()
        h.service.transport.respond("wizard-keepalive") { Reply.Timeout }
        passUrl(h)
        advanceTimeBy(61_000)
        runCurrent()
        assertEquals(WizardStep.SESSION_EXPIRED, h.state.step)
        assertEquals(WizardNavTarget.EXPIRED, h.target)
        assertEquals(List(3) { CheckpointResult.PENDING }, h.state.checks)
        assertFalse("a stale verification result is never used", h.vm.verify())
        assertFalse(h.vm.finish())
        h.store.clear()
        runCurrent()
        assertEquals("an expired session is not closed again", 0, h.count("wizard-close"))
    }

    @Test
    fun serviceReportedExpiryAlsoLeadsToSessionExpired() = wizardTest {
        val h = harness()
        h.service.keepaliveExpired = true
        advanceTimeBy(15_500)
        runCurrent()
        assertEquals(WizardStep.SESSION_EXPIRED, h.state.step)
        assertEquals(WizardNavTarget.EXPIRED, h.target)
        h.store.clear()
    }

    private fun TestScope.atContracts(configure: FakeWizardService.() -> Unit): Harness {
        val h = harness(configure)
        passUrl(h)
        passSecret(h)
        assertTrue(h.vm.verify())
        runCurrent()
        poll()
        assertEquals(WizardStep.CONTRACTS, h.state.step)
        return h
    }

    @Test
    fun lostFinishReplyIsUnknownAndAConfirmedReceiptFinishes() = wizardTest {
        val h = atContracts { finish = Finish.ReplyLost; commitState = "confirmed" }
        assertTrue(h.vm.finish())
        runCurrent()
        advanceTimeBy(5_100)
        runCurrent()
        assertEquals(WizardStep.COMMIT_UNKNOWN, h.state.step)
        assertEquals(WizardNavTarget.SAVE_UNKNOWN, h.target)
        val source = SaveOutcomeRegistry.source(SaveOrigin.Wizard)!!
        assertEquals(SaveOutcomePhase.UNKNOWN, source.state.value.phase)
        assertFalse("the Wizard never repeats a write from 33", source.repeatSave())
        assertFalse(h.vm.finish())
        assertTrue(source.verify())
        assertEquals(SaveOutcomePhase.CHECKING, source.state.value.phase)
        assertFalse("one check at a time", source.verify())
        runCurrent()
        assertEquals(SaveOutcomePhase.CONFIRMED, source.state.value.phase)
        assertEquals(WizardStep.FINISHED, h.state.step)
        assertEquals(WizardNavTarget.SAVED, h.target)
        assertEquals("never a second Finish", 1, h.count("wizard-finish"))
        assertEquals(1, h.count("wizard-get-commit"))
        assertEquals(JsonChecks.string(h.service.requests("wizard-finish").single().json["commitId"]), JsonChecks.string(h.service.requests("wizard-get-commit").single().json["commitId"]))
        h.store.clear()
    }

    @Test
    fun failedReceiptReturnsToTheDraftAndUnknownStaysUnknown() = wizardTest {
        val h = atContracts { finish = Finish.ReplyLost; commitState = "unknown" }
        h.vm.finish()
        runCurrent()
        advanceTimeBy(5_100)
        runCurrent()
        val source = h.vm.saveOutcome
        source.verify()
        runCurrent()
        assertEquals(SaveOutcomePhase.UNKNOWN, source.state.value.phase)
        assertEquals(WizardStep.COMMIT_UNKNOWN, h.state.step)

        h.service.commitState = "failed"
        source.verify()
        runCurrent()
        assertEquals(SaveOutcomePhase.NOT_SAVED, source.state.value.phase)
        assertEquals(WizardStep.CONTRACTS, h.state.step)
        assertEquals(WizardNavTarget.FINISH, h.target)
        assertEquals("config_write_failed", h.state.error?.code)
        assertEquals(1, h.count("wizard-finish"))
        // Finish again is a new explicit commit with a new commitId.
        h.service.finish = Finish.Confirmed
        assertTrue(h.vm.finish())
        runCurrent()
        val commits = h.service.requests("wizard-finish").map { JsonChecks.string(it.json["commitId"]) }
        assertEquals(2, commits.size)
        assertNotEquals(commits[0], commits[1])
        h.store.clear()
    }

    @Test
    fun receiptCheckAfterAServiceRestartReadsTheConfigurationStateOnce() = wizardTest {
        val h = atContracts { finish = Finish.ReplyLost; commitState = "expired" }
        h.vm.finish()
        runCurrent()
        advanceTimeBy(5_100)
        runCurrent()
        h.vm.verifyCommit()
        runCurrent()
        val state = h.vm.saveOutcome.state.value
        assertEquals(SaveOutcomePhase.UNKNOWN, state.phase)
        assertTrue(state.serviceRestarted)
        assertEquals(FakeWizardService.CONFIG_PATH, state.configPath)
        assertEquals(1, h.count("get-config-state"))
        assertEquals(1, h.count("wizard-finish"))
        h.store.clear()
    }

    @Test
    fun gateDisconnectsOnceWaitsForIdleAndResumesOnce() = wizardTest {
        val h = harness {
            vpnState = "connected"
            vpnPhase = "running"
            epoch = 4uL
            disconnectReads = 2
        }
        assertEquals(WizardStep.DISCONNECT_GATE, h.state.step)
        assertEquals(WizardNavTarget.GATE, h.target)
        assertFalse(h.state.offlineKnown)
        assertTrue(h.vm.disconnectAndContinue())
        runCurrent()
        assertTrue("the disconnect reply said stopping: wait", h.state.disconnecting)
        assertEquals(0, h.count("wizard-resume"))
        repeat(4) { poll() }
        assertEquals(WizardStep.SERVER_URL, h.state.step)
        assertEquals(WizardNavTarget.URL, h.target)
        assertEquals(1, h.count("disconnect"))
        assertEquals(1, h.count("wizard-resume"))
        assertTrue(h.count("get-status") >= 2)
        h.store.clear()
    }

    @Test
    fun foreignConnectInterruptsKeepsTheDraftAndContinuesWithResumeOnly() = wizardTest {
        val h = harness { checks["secret"] = Check.Interrupt }
        passUrl(h)
        h.vm.setSecret("synthetic-secret")
        assertTrue(h.vm.verify())
        runCurrent()
        poll()
        assertEquals(WizardStep.DISCONNECT_GATE, h.state.step)
        assertEquals(WizardNavTarget.GATE, h.target)
        assertEquals("wizard_connection_interrupted", h.state.error?.code)
        assertEquals(List(3) { CheckpointResult.PENDING }, h.state.checks)
        // The foreign Connect is running: one disconnect, then resume; the draft stays.
        h.vm.disconnectAndContinue()
        runCurrent()
        poll()
        assertEquals(WizardStep.SERVER_URL, h.state.step)
        assertEquals(WizardNavTarget.URL, h.target)
        assertEquals(1, h.count("disconnect"))
        assertEquals(1, h.count("wizard-resume"))
        assertEquals("https://vpn.example.com/phpvpn.php", h.state.serverUrl)
        assertSecret("entries are kept", "synthetic-secret", h.state.secretText)
        assertTrue(h.state.dirty)
        h.store.clear()
    }

    @Test
    fun alreadyDisconnectedGateOnlyResumes() = wizardTest {
        val h = harness { checks["secret"] = Check.Interrupt }
        passUrl(h)
        h.vm.verify()
        runCurrent()
        poll()
        assertEquals(WizardStep.DISCONNECT_GATE, h.state.step)
        // The foreign connection is gone again before the user acts.
        h.service.vpnState = "disconnected"
        h.service.vpnPhase = "idle"
        poll()
        assertTrue(h.state.offlineKnown)
        h.vm.disconnectAndContinue()
        runCurrent()
        assertEquals(WizardStep.SERVER_URL, h.state.step)
        assertEquals(0, h.count("disconnect"))
        assertEquals(1, h.count("wizard-resume"))
        h.store.clear()
    }

    @Test
    fun resetLoadsTheDefaultPlaceholderAndMarksTheDraftDirty() = wizardTest {
        val h = harness {
            mode = "edit"
            availability = "valid"
            fields = FakeWizardService.Fields(serverUrl = "https://old.example.com/phpvpn.php", virtualIp = "10.99.99.4", accessKey = "04".repeat(32), autoDetectIp = false)
        }
        assertEquals("https://old.example.com/phpvpn.php", h.state.serverUrl)
        passUrl(h)
        assertTrue(h.vm.back())
        assertEquals(WizardStep.SERVER_URL, h.state.step)
        assertTrue(h.vm.reset())
        runCurrent()
        assertEquals(FakeWizardService.PLACEHOLDER_URL, h.state.serverUrl)
        assertEquals("", h.state.virtualIp)
        assertTrue(h.state.autoDetectIp)
        assertTrue("Reset is a draft change", h.state.dirty)
        assertEquals(List(3) { CheckpointResult.PENDING }, h.state.checks)
        assertFalse(h.vm.verify())
        assertEquals(LocalProblem.URL_PLACEHOLDER, h.state.localProblem)
        assertEquals(1, h.count("wizard-reset"))
        h.store.clear()
    }

    @Test
    fun acceptedHeadersInvalidateEveryCheckAndRejectedOnesChangeNothing() = wizardTest {
        val h = harness()
        passUrl(h)
        assertEquals(WizardStep.SERVER_SECRET, h.state.step)
        val rejected = h.vm.acceptHeaders("Host: x")
        assertEquals(HeaderCheck.Invalid(HeaderProblem.MANAGED, 1, "Host"), rejected)
        assertEquals(HttpHeaders.DEFAULT, h.state.httpHeaders)
        assertEquals(CheckpointResult.SUCCESS, h.state.checks[0])
        assertTrue(h.vm.acceptHeaders(HeaderRules.displayText(HttpHeaders.DEFAULT)) is HeaderCheck.Valid)
        assertEquals("an unchanged block keeps the evidence", CheckpointResult.SUCCESS, h.state.checks[0])
        assertTrue(h.vm.acceptHeaders("Cookie: synthetic=1\nUser-Agent: test") is HeaderCheck.Valid)
        assertEquals("Cookie: synthetic=1\r\nUser-Agent: test", h.state.httpHeaders)
        assertEquals(CheckpointResult.PENDING, h.state.checks[0])
        assertEquals(WizardStep.SERVER_URL, h.state.step)
        assertEquals(WizardNavTarget.URL, h.target)
        h.vm.verify()
        runCurrent()
        assertEquals("Cookie: synthetic=1\r\nUser-Agent: test", JsonChecks.string(h.service.requests("wizard-check-url").last().json["httpHeaders"]))
        h.store.clear()
    }

    @Test
    fun leavingSendsOneBestEffortCloseAndWipesTheDraft() = wizardTest {
        val h = harness()
        h.vm.setUrl("https://vpn.example.com/phpvpn.php")
        assertTrue(h.state.dirty)
        assertTrue(h.vm.leave())
        runCurrent()
        assertEquals(1, h.count("wizard-close"))
        assertEquals(1000, h.service.requests("wizard-close").single().timeoutMs)
        assertFalse(h.state.dirty)
        assertEquals("", h.state.serverUrl)
        h.store.clear()
        runCurrent()
        assertEquals("no second close when the graph goes away", 1, h.count("wizard-close"))
    }

    @Test
    fun clearingTheGraphClosesAnOpenSession() = wizardTest {
        val h = harness()
        h.store.clear()
        runCurrent()
        assertEquals(1, h.count("wizard-close"))
        assertNull(SaveOutcomeRegistry.source(SaveOrigin.Wizard))
    }

    /** WizardModel.CorrelationDeadlineEpochAndNoAutomaticRetry (ViewModel part): contracts and Back. */
    @Test
    fun contractsCountAndBackThroughEveryStep() = wizardTest {
        val h = atContracts {}
        assertEquals(13, h.state.contractsToAdd)
        assertTrue(h.vm.setCreateContracts(false))
        assertEquals(0, h.state.contractsToAdd)
        assertTrue(h.vm.back())
        assertEquals(WizardNavTarget.IDENTITY, h.target)
        assertTrue(h.vm.back())
        assertEquals(WizardNavTarget.SECRET, h.target)
        assertTrue(h.vm.back())
        assertEquals(WizardNavTarget.URL, h.target)
        assertFalse(h.vm.back())
        assertEquals(WizardStep.SERVER_URL, h.state.step)
        assertEquals("Back keeps every verified value", "10.99.99.10", h.state.virtualIp)
        h.store.clear()
    }

    @Test
    fun unsupportedNetworkDisablesTheDefaultContracts() = wizardTest {
        val h = atContracts { vpnCidr = "192.168.7.0/24"; suggestedIp = "192.168.7.10" }
        assertFalse(h.state.contractsAllowed)
        assertEquals(0, h.state.contractsToAdd)
        assertFalse(h.vm.setCreateContracts(false))
        h.vm.finish()
        runCurrent()
        assertEquals(false, JsonChecks.bool(h.service.requests("wizard-finish").last().json["createDefaultContracts"]))
        h.store.clear()
    }

    @Test
    fun failedOpenOffersTryAgain() = wizardTest {
        val h = harness { openFails = true }
        assertEquals(WizardStep.LOAD_ERROR, h.state.step)
        assertEquals(WizardNavTarget.URL, h.target)
        h.service.openFails = false
        assertTrue(h.vm.reopen())
        runCurrent()
        assertEquals(WizardStep.SERVER_URL, h.state.step)
        assertEquals(2, h.count("wizard-open"))
        h.store.clear()
    }

    @Test
    fun unreadableConfigurationOffersOnlyReset() = wizardTest {
        val h = harness { availability = "unreadable"; mode = "reset"; fields = null }
        assertEquals(WizardStep.LOAD_ERROR, h.state.step)
        assertFalse(h.vm.verify())
        assertTrue(h.vm.reset())
        runCurrent()
        assertEquals(WizardStep.SERVER_URL, h.state.step)
        assertEquals(FakeWizardService.PLACEHOLDER_URL, h.state.serverUrl)
        h.store.clear()
    }
}
