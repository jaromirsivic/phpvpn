package phpvpn.android.ui.screens.contracts

import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.controller.ContractsStage
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.ipc.StrictJson
import phpvpn.android.debug.ConfigReplies
import phpvpn.android.debug.ConfigReplies.answer
import phpvpn.android.debug.ConfigReplies.answerNext
import phpvpn.android.standin.StandInPasswordKeyModel
import phpvpn.android.ui.screens.password.PasswordKeyViewModel
import phpvpn.android.ui.screens.runFlowTest
import phpvpn.android.ui.navigation.SaveOrigin
import phpvpn.android.ui.screens.save.SaveOutcomePhase
import phpvpn.android.ui.screens.save.SaveOutcomeRegistry
import phpvpn.android.ui.screens.save.SaveOutcomeState
import phpvpn.android.ui.screens.writes

/**
 * Contracts flow of WP10 (19, 20, 21, 22, 41, 33, 46) on the ViewModel level
 * with the Kotlin stand-ins: draft per target IP across pages, invalid key,
 * key from a password, save, unknown result, conflict.
 */
class ContractsViewModelTest {
    private val keyA = "918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d"
    private val keyB = "f41f0ed508411aef592228e9d73ae137a4d31933f038160c04ae1e8d70597ebe"

    private fun contractsOf(config: String): JsonObject =
        (StrictJson.parse(config) as JsonObject)["communicationContracts"] as JsonObject

    @Test
    fun opensAValidDocumentWithEveryPeerExceptTheLocalIp() {
        val transport = FakeIpcTransport()
        transport.answer("get-config-document", ConfigReplies.valid(ConfigReplies.document(virtualIp = "10.99.99.40")))
        runFlowTest(transport) { deps ->
            val vm = ContractsViewModel(deps)
            advanceUntilIdle()
            val rows = vm.rows()
            assertEquals(ContractsStage.EDITING, vm.state.value.stage)
            assertEquals(252, rows.size)
            assertTrue(rows.none { it.remoteIp == "10.99.99.40" })
            assertEquals("10.99.99.2", rows.first().remoteIp)
            assertEquals("10.99.99.254", rows.last().remoteIp)
            assertEquals(13, contractsPageCount(rows.size))
            assertEquals("10.99.99.40", vm.localIp)
            assertEquals(1, transport.requests("get-config-document").size)
            assertEquals(0, transport.writes())
        }
    }

    @Test
    fun missingRepairableAndFailedReadsOpenNoModel() {
        for ((reply, availability) in listOf(
            ConfigReplies.missing() to ConfigAvailability.MISSING,
            ConfigReplies.configDocument("repairable", "{\"masterSetup\":{}}") to ConfigAvailability.REPAIRABLE,
            ConfigReplies.configDocument("invalid", "{") to ConfigAvailability.INVALID,
        )) {
            val transport = FakeIpcTransport()
            transport.answer("get-config-document", reply)
            runFlowTest(transport) { deps ->
                val vm = ContractsViewModel(deps)
                advanceUntilIdle()
                assertEquals(availability, vm.state.value.unavailable)
                assertEquals(ConfigReplies.CONFIG_PATH, vm.state.value.configPath)
                assertTrue(vm.rows().isEmpty())
            }
        }
        runFlowTest { deps ->
            val vm = ContractsViewModel(deps)
            advanceUntilIdle()
            assertEquals(ErrorCode.TIMEOUT.wireName, vm.state.value.failureCode)
            assertTrue(vm.rows().isEmpty())
        }
    }

    @Test
    fun draftIsKeptPerTargetIpAcrossPagesAndADetailDiscardRestoresOnlyItsRow() {
        val transport = FakeIpcTransport()
        transport.answer("get-config-document", ConfigReplies.valid())
        runFlowTest(transport) { deps ->
            val vm = ContractsViewModel(deps)
            advanceUntilIdle()
            // Page 1 and page 13 of 20 rows each.
            vm.openDetail("10.99.99.3")
            vm.setEnabled("10.99.99.3", true)
            assertTrue(vm.setKey("10.99.99.3", keyA))
            vm.closeDetail()
            vm.openDetail("10.99.99.254")
            vm.setEnabled("10.99.99.254", true)
            assertTrue(vm.setKey("10.99.99.254", keyB))
            vm.closeDetail()
            advanceUntilIdle()
            assertEquals(2, vm.dirtyCount())
            val rows = vm.rows()
            val page13 = rows.subList(12 * CONTRACTS_PAGE_SIZE, rows.size)
            assertTrue(page13.single { it.remoteIp == "10.99.99.254" }.dirty)
            assertTrue(rows.subList(0, CONTRACTS_PAGE_SIZE).single { it.remoteIp == "10.99.99.3" }.dirty)
            // A third detail changed and discarded (32) leaves the other two untouched.
            vm.openDetail("10.99.99.100")
            vm.setEnabled("10.99.99.100", true)
            assertTrue(vm.setKey("10.99.99.100", keyA))
            assertTrue(vm.detail("10.99.99.100")!!.changedSinceOpen)
            vm.discardDetail("10.99.99.100")
            advanceUntilIdle()
            assertFalse(vm.rows().single { it.remoteIp == "10.99.99.100" }.dirty)
            assertEquals(2, vm.dirtyCount())
            assertEquals(keyA, vm.keyText("10.99.99.3"))
            assertEquals(0, transport.writes())
        }
    }

    @Test
    fun disabledKeyIsKeptAndAnInvalidEnabledKeyBlocksSaveOn22() {
        val transport = FakeIpcTransport()
        transport.answer("get-config-document", ConfigReplies.valid())
        runFlowTest(transport) { deps ->
            val vm = ContractsViewModel(deps)
            advanceUntilIdle()
            vm.setEnabled("10.99.99.3", true)
            assertTrue(vm.setKey("10.99.99.3", keyA))
            vm.setEnabled("10.99.99.7", true)
            assertTrue(vm.setKey("10.99.99.7", "a29f00"))
            advanceUntilIdle()
            val detail = vm.detail("10.99.99.7")!!
            assertTrue(detail.keyErrorVisible && !detail.keyValid)
            assertEquals(6, detail.keyLength)
            assertEquals(ContractsStage.INVALID, vm.state.value.stage)
            assertEquals("10.99.99.7", vm.firstBlocking)
            vm.save()
            advanceUntilIdle()
            assertEquals(ContractsNavigation.Blocked("10.99.99.7"), vm.navigation.value)
            vm.consumeNavigation()
            assertEquals(0, transport.writes())
            // The error belongs to that contract; the other change stays.
            assertTrue(vm.rows().single { it.remoteIp == "10.99.99.3" }.dirty)
            assertTrue(vm.setKey("10.99.99.7", keyB))
            advanceUntilIdle()
            assertNull(vm.firstBlocking)
            assertEquals(ContractsStage.EDITING, vm.state.value.stage)
        }
    }

    @Test
    fun keyFromPasswordGoesOnlyToTheEnabledTargetAndATargetGoesStaleAfterASave() {
        val transport = FakeIpcTransport()
        transport.answer("get-config-document", ConfigReplies.valid())
        runFlowTest(transport) { deps ->
            val vm = ContractsViewModel(deps)
            advanceUntilIdle()
            val password = PasswordKeyViewModel(StandInPasswordKeyModel())
            // The cue is never hashed: an empty password has the standard empty hash.
            assertEquals("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", password.keyPreview)
            val target = vm.target("10.99.99.10")!!
            assertEquals("10.99.99.2_10.99.99.10", target.contractId)
            assertFalse(vm.targetEnabled("10.99.99.10"))
            password.useDefault(target.contractId)
            assertEquals(target.contractId, password.password)
            // The pair vector of client.md.
            assertEquals(keyB, password.keyPreview)
            assertTrue(password.update("abc"))
            assertEquals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", password.keyPreview)
            // A control character refuses the whole edit (the field keeps "abc") and, like the
            // native model, leaves no usable key until the next accepted edit.
            assertFalse(password.update("a\u0000b"))
            assertEquals("abc", password.password)
            assertFalse(password.valid)
            assertTrue(password.update("abc"))
            vm.setEnabled("10.99.99.10", true)
            assertTrue(vm.targetEnabled("10.99.99.10"))
            val key = password.key()
            assertNull(vm.applyPasswordKey(target, key))
            key.wipe()
            assertEquals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", vm.keyText("10.99.99.10"))
            assertEquals("", vm.keyText("10.99.99.11"))
            password.useRandom()
            assertEquals(64, password.password.length)
            assertTrue(password.valid)
            // A save freezes the model and retires the captured target.
            transport.answer("set-config", ConfigReplies.ACK)
            vm.save()
            advanceUntilIdle()
            assertEquals(ErrorCode.INVALID_ARGUMENT, vm.applyPasswordKey(target, SecretText.of(keyA)))
        }
    }

    @Test
    fun confirmedSavePublishesOnlyTheContractsChange() {
        val transport = FakeIpcTransport()
        transport.answer("get-config-document", ConfigReplies.valid())
        transport.answer("set-config", ConfigReplies.ACK)
        runFlowTest(transport) { deps ->
            val vm = ContractsViewModel(deps)
            advanceUntilIdle()
            vm.setEnabled("10.99.99.3", true)
            vm.setKey("10.99.99.3", keyA)
            vm.setKey("10.99.99.9", "draft-only") // disabled: never published
            vm.save()
            advanceUntilIdle()
            assertEquals(ContractsNavigation.Saved, vm.navigation.value)
            val submitted = ConfigReplies.submitted(transport.requests("set-config").single())
            assertEquals(JsonObject(mapOf("10.99.99.3" to JsonObject(mapOf("payloadKey" to JsonPrimitive(keyA))))), contractsOf(submitted))
            val original = StrictJson.parse(ConfigReplies.DEFAULT_DOCUMENT) as JsonObject
            assertEquals(original["masterSetup"], (StrictJson.parse(submitted) as JsonObject)["masterSetup"])
            assertEquals(30_000, transport.requests("set-config").single().timeoutMs)
        }
    }

    @Test
    fun rejectedAndNotProvenSavesKeepTheDraft() {
        val transport = FakeIpcTransport()
        transport.answer("get-config-document", ConfigReplies.valid())
        transport.answerNext("set-config", ConfigReplies.INVALID_JSON)
        runFlowTest(transport) { deps ->
            val vm = ContractsViewModel(deps)
            advanceUntilIdle()
            vm.setEnabled("10.99.99.3", true)
            vm.setKey("10.99.99.3", keyA)
            vm.save()
            advanceUntilIdle()
            assertEquals(SaveProblem(ErrorCode.CONFIG_INVALID_JSON, notSent = false), vm.rejection)
            assertNull(vm.navigation.value)
            assertEquals(ContractsStage.EDITING, vm.state.value.stage)
            assertEquals(1, vm.dirtyCount())
            // config_write_failed does not prove an unchanged disk: unknown (33), no automatic retry.
            transport.answerNext("set-config", ConfigReplies.WRITE_FAILED)
            vm.save()
            advanceUntilIdle()
            assertEquals(ContractsNavigation.Unknown, vm.navigation.value)
            assertEquals(ContractsStage.SAVE_UNKNOWN, vm.state.value.stage)
            assertEquals(2, transport.requests("set-config").size)
        }
    }

    @Test
    fun unknownSaveIsVerifiedByReadingAndAMatchIsASuccessNotARetry() {
        val transport = FakeIpcTransport()
        transport.answerNext("get-config-document", ConfigReplies.valid())
        runFlowTest(transport) { deps ->
            val vm = ContractsViewModel(deps)
            advanceUntilIdle()
            val source = vm.saveOutcome
            assertEquals(SaveOrigin.Contracts, source.origin)
            assertSame(source, SaveOutcomeRegistry.source(SaveOrigin.Contracts))
            assertEquals(SaveOutcomePhase.IDLE, source.state.value.phase)
            assertFalse(source.verify()) // nothing unknown yet
            vm.setEnabled("10.99.99.3", true)
            vm.setKey("10.99.99.3", keyA)
            vm.save() // no reply: the write may have happened
            advanceUntilIdle()
            assertEquals(ContractsNavigation.Unknown, vm.navigation.value)
            assertEquals(SaveOutcomeState(SaveOutcomePhase.UNKNOWN, draftChanges = 1, configPath = ConfigReplies.CONFIG_PATH), source.state.value)
            val submitted = ConfigReplies.submitted(transport.requests("set-config").single())
            transport.answerNext("get-config-document", ConfigReplies.valid(submitted))
            assertTrue(source.verify())
            assertEquals(SaveOutcomePhase.CHECKING, source.state.value.phase)
            assertFalse(source.verify()) // one check at a time
            advanceUntilIdle()
            assertEquals(SaveOutcomePhase.CONFIRMED, source.state.value.phase)
            assertFalse(source.state.value.canRepeat)
            assertFalse(source.repeatSave())
            assertEquals(1, transport.requests("set-config").size)
            assertEquals(2, transport.requests("get-config-document").size)
        }
    }

    @Test
    fun conflictIsNeverOverwrittenSilentlyOnlyByAnExplicitSaveAgain() {
        val transport = FakeIpcTransport()
        transport.answerNext("get-config-document", ConfigReplies.valid())
        runFlowTest(transport) { deps ->
            val vm = ContractsViewModel(deps)
            advanceUntilIdle()
            val source = vm.saveOutcome
            vm.setEnabled("10.99.99.3", true)
            vm.setKey("10.99.99.3", keyA)
            assertFalse(source.repeatSave()) // not allowed before a check
            vm.save()
            advanceUntilIdle()
            assertFalse(source.repeatSave()) // not allowed while the result is unknown
            assertEquals(1, transport.requests("set-config").size)
            // Someone else saved another document meanwhile.
            val foreign = ConfigReplies.document(contracts = mapOf("10.99.99.5" to keyB))
            transport.answerNext("get-config-document", ConfigReplies.valid(foreign))
            source.verify()
            advanceUntilIdle()
            assertEquals(SaveOutcomePhase.CONFLICT, source.state.value.phase)
            assertTrue(source.state.value.canRepeat)
            assertEquals(1, transport.requests("set-config").size)
            transport.answer("set-config", ConfigReplies.ACK)
            assertTrue(source.repeatSave())
            advanceUntilIdle()
            assertEquals(SaveOutcomePhase.CONFIRMED, source.state.value.phase)
            assertEquals(2, transport.requests("set-config").size)
            assertEquals(contractsOf(ConfigReplies.submitted(transport.requests("set-config")[0])), contractsOf(ConfigReplies.submitted(transport.requests("set-config")[1])))
        }
    }

    @Test
    fun aRepeatThatIsRejectedIsNotSavedAndKeepsTheDraft() {
        val transport = FakeIpcTransport()
        transport.answerNext("get-config-document", ConfigReplies.valid())
        runFlowTest(transport) { deps ->
            val vm = ContractsViewModel(deps)
            advanceUntilIdle()
            vm.setEnabled("10.99.99.3", true)
            vm.setKey("10.99.99.3", keyA)
            vm.save()
            advanceUntilIdle()
            transport.answerNext("get-config-document", ConfigReplies.missing())
            vm.saveOutcome.verify()
            advanceUntilIdle()
            assertEquals(SaveOutcomePhase.CONFLICT, vm.saveOutcome.state.value.phase)
            transport.answerNext("set-config", ConfigReplies.INVALID_JSON)
            assertTrue(vm.saveOutcome.repeatSave())
            advanceUntilIdle()
            assertEquals(SaveOutcomePhase.NOT_SAVED, vm.saveOutcome.state.value.phase)
            assertEquals(ErrorCode.CONFIG_INVALID_JSON.wireName, vm.saveOutcome.state.value.errorCode)
            assertFalse(vm.saveOutcome.state.value.canRepeat)
            assertEquals(ContractsStage.EDITING, vm.state.value.stage)
            assertEquals(1, vm.dirtyCount())
        }
    }

    @Test
    fun aFailedCheckDecidesNothing() {
        val transport = FakeIpcTransport()
        transport.answerNext("get-config-document", ConfigReplies.valid())
        runFlowTest(transport) { deps ->
            val vm = ContractsViewModel(deps)
            advanceUntilIdle()
            vm.setEnabled("10.99.99.3", true)
            vm.setKey("10.99.99.3", keyA)
            vm.save()
            advanceUntilIdle()
            // The check read times out: still unknown, with the reason, and checkable again.
            assertTrue(vm.saveOutcome.verify())
            advanceUntilIdle()
            val state = vm.saveOutcome.state.value
            assertEquals(SaveOutcomePhase.UNKNOWN, state.phase)
            assertEquals(ErrorCode.IPC_UNAVAILABLE.wireName, state.errorCode)
            assertFalse(state.canRepeat)
            assertEquals(ContractsStage.SAVE_UNKNOWN, vm.state.value.stage)
            assertNotNull(vm.navigation.value)
            assertEquals(1, transport.requests("set-config").size)
        }
    }
}
