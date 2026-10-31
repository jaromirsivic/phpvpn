package phpvpn.android.ui.screens.editor

import kotlinx.coroutines.test.advanceUntilIdle
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.controller.EditorNotice
import phpvpn.android.bridge.controller.EditorState
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.debug.ConfigReplies
import phpvpn.android.debug.ConfigReplies.answer
import phpvpn.android.debug.ConfigReplies.answerNext
import phpvpn.android.ui.screens.runFlowTest
import phpvpn.android.ui.screens.save.SaveVerification
import phpvpn.android.ui.screens.writes

/**
 * Edit Config on Android (23, 24, 39, 42, 36a, 33, 46) on the ViewModel level:
 * T-U04 branches of 36a, Reset only in the draft, save with the save-response
 * semantics, unknown result, conflict.
 */
class EditorViewModelTest {
    private val edited = ConfigReplies.document(virtualIp = "10.99.99.9")

    @Test
    fun missingConfigurationAsks36aWithTheServicePathAndNoCreatesNothing() {
        val transport = FakeIpcTransport()
        transport.answer("get-config-document", ConfigReplies.missing())
        runFlowTest(transport) { deps ->
            val vm = EditorViewModel(deps)
            vm.open()
            advanceUntilIdle()
            assertEquals(EditorNavigation.Missing(ConfigReplies.CONFIG_PATH), vm.navigation.value)
            assertEquals(EditorState.ConfirmCreate(ConfigReplies.CONFIG_PATH), vm.state.value)
            vm.answerCreate(false)
            advanceUntilIdle()
            assertEquals(EditorState.Closed, vm.state.value)
            assertEquals(0, transport.writes())
            assertNull(vm.createdNow)
        }
    }

    @Test
    fun yesCreatesAndOpensTheActualDocumentAndCancelDeletesNothing() {
        val transport = FakeIpcTransport()
        transport.answer("get-config-document", ConfigReplies.missing())
        transport.answer("create-default-config", ConfigReplies.createDefault(created = true))
        runFlowTest(transport) { deps ->
            val vm = EditorViewModel(deps)
            vm.open()
            advanceUntilIdle()
            vm.answerCreate(true)
            advanceUntilIdle()
            val editing = vm.state.value as EditorState.Editing
            assertTrue(editing.session.created)
            assertEquals(true, vm.createdNow)
            assertEquals(ConfigReplies.DEFAULT_DOCUMENT, vm.text)
            assertFalse(vm.dirty)
            // No path and no content in the request: the service owns the template.
            assertEquals(setOf("ipcVersion", "requestId", "command"), transport.requests("create-default-config").single().json.keys)
            vm.close()
            advanceUntilIdle()
            assertEquals(EditorState.Closed, vm.state.value)
            // Exactly the one create; Cancel sends nothing (there is no delete command at all).
            assertEquals(1, transport.writes())
            assertEquals(listOf("get-config-document", "create-default-config"), transport.requests.map { it.command })
        }
    }

    @Test
    fun createdFalseOpensTheFileThatAppearedWithoutOverwritingIt() {
        val transport = FakeIpcTransport()
        transport.answer("get-config-document", ConfigReplies.missing())
        transport.answer("create-default-config", ConfigReplies.createDefault(created = false, availability = "invalid", document = "broken-json"))
        runFlowTest(transport) { deps ->
            val vm = EditorViewModel(deps)
            vm.open()
            advanceUntilIdle()
            vm.answerCreate(true)
            advanceUntilIdle()
            val editing = vm.state.value as EditorState.Editing
            assertFalse(editing.session.created)
            assertEquals(false, vm.createdNow)
            assertEquals("broken-json", vm.text)
            assertEquals(ConfigAvailability.INVALID, editing.session.availability)
            assertEquals(0, transport.requests("set-config").size)
        }
    }

    @Test
    fun yesFrom36aReachedDirectlyChecksFirstAndNeverCreatesOverAnExistingFile() {
        val transport = FakeIpcTransport()
        transport.answer("get-config-document", ConfigReplies.valid())
        runFlowTest(transport) { deps ->
            val vm = EditorViewModel(deps)
            vm.answerCreate(true)
            advanceUntilIdle()
            assertTrue(vm.state.value is EditorState.Editing)
            assertEquals(0, transport.writes())
        }
    }

    @Test
    fun resetChangesOnlyTheDraftAndAFailedResetKeepsIt() {
        val transport = FakeIpcTransport()
        transport.answer("get-config-document", ConfigReplies.valid(edited))
        transport.answerNext("get-default-config", ConfigReplies.defaultConfig())
        runFlowTest(transport) { deps ->
            val vm = EditorViewModel(deps)
            vm.open()
            advanceUntilIdle()
            assertEquals(edited, vm.text)
            vm.reset()
            advanceUntilIdle()
            assertEquals(true, vm.resetLoaded)
            assertEquals(ConfigReplies.DEFAULT_DOCUMENT, vm.text)
            assertTrue(vm.dirty)
            assertEquals(0, transport.writes())
            vm.consumeReset()
            vm.edit("my draft")
            advanceUntilIdle()
            vm.reset() // get-default-config times out
            advanceUntilIdle()
            assertEquals(false, vm.resetLoaded)
            assertEquals("my draft", vm.text)
            assertTrue((vm.state.value as EditorState.Editing).notice is EditorNotice.ResetFailed)
            assertEquals(0, transport.writes())
        }
    }

    @Test
    fun confirmedSaveSendsTheEditedDocumentAndLeadsTo25() {
        val transport = FakeIpcTransport()
        transport.answer("get-config-document", ConfigReplies.valid())
        transport.answer("set-config", ConfigReplies.ACK)
        runFlowTest(transport) { deps ->
            val vm = EditorViewModel(deps)
            vm.open()
            advanceUntilIdle()
            vm.edit(edited)
            advanceUntilIdle()
            assertTrue(vm.dirty)
            vm.save()
            advanceUntilIdle()
            assertEquals(EditorNavigation.Saved, vm.navigation.value)
            assertEquals(edited, ConfigReplies.submitted(transport.requests("set-config").single()))
            assertTrue(vm.state.value is EditorState.Saved)
        }
    }

    @Test
    fun rejectedSaveShows39WithTheValidatorTextAndKeepsTheDraft() {
        val transport = FakeIpcTransport()
        transport.answer("get-config-document", ConfigReplies.valid())
        transport.answerNext("set-config", ConfigReplies.INVALID_HEADER, ConfigReplies.INVALID_JSON)
        runFlowTest(transport) { deps ->
            val vm = EditorViewModel(deps)
            vm.open()
            advanceUntilIdle()
            vm.edit(edited)
            vm.save()
            advanceUntilIdle()
            val header = vm.rejection!!
            assertEquals(ErrorCode.CONFIG_INVALID_VALUE, header.code)
            // The line number comes from the validator, never from the client.
            assertEquals("masterSetup.httpHeaders line 2: empty line or missing colon", header.validationDescription)
            assertEquals(edited, vm.text)
            assertNull(vm.navigation.value)
            vm.dismissRejection()
            vm.edit("{")
            vm.save()
            advanceUntilIdle()
            assertEquals(ErrorCode.CONFIG_INVALID_JSON, vm.rejection!!.code)
            assertEquals("", vm.rejection!!.validationDescription)
            assertEquals("{", vm.text)
            assertTrue(vm.state.value is EditorState.Editing)
        }
    }

    @Test
    fun unknownSaveIsVerifiedAndAMatchIsConfirmedWithoutAnotherWrite() {
        val transport = FakeIpcTransport()
        transport.answerNext("get-config-document", ConfigReplies.valid())
        runFlowTest(transport) { deps ->
            val vm = EditorViewModel(deps)
            vm.open()
            advanceUntilIdle()
            vm.edit(edited)
            vm.save() // no reply
            advanceUntilIdle()
            assertEquals(EditorNavigation.Unknown, vm.navigation.value)
            assertTrue(vm.state.value is EditorState.SaveUnknown)
            transport.answerNext("get-config-document", ConfigReplies.valid(edited))
            assertEquals(SaveVerification.Confirmed(ConfigReplies.CONFIG_PATH), vm.verify())
            assertEquals(1, transport.requests("set-config").size)
        }
    }

    @Test
    fun conflictKeepsTheDraftAndOnlyAnExplicitSaveAgainWrites() {
        val transport = FakeIpcTransport()
        transport.answerNext("get-config-document", ConfigReplies.valid())
        runFlowTest(transport) { deps ->
            val vm = EditorViewModel(deps)
            vm.open()
            advanceUntilIdle()
            vm.edit(edited)
            vm.save()
            advanceUntilIdle()
            transport.answerNext("get-config-document", ConfigReplies.missing())
            assertEquals(SaveVerification.Failed(EditorViewModel.DIFFERENT), vm.verify())
            assertEquals(1, transport.requests("set-config").size)
            assertEquals(edited, vm.text)
            transport.answer("set-config", ConfigReplies.ACK)
            vm.saveAgain()
            advanceUntilIdle()
            assertEquals(EditorNavigation.Saved, vm.navigation.value)
            assertEquals(2, transport.requests("set-config").size)
            assertEquals(edited, ConfigReplies.submitted(transport.requests("set-config")[1]))
        }
    }

    @Test
    fun aFailedCheckStaysUnknown() {
        val transport = FakeIpcTransport()
        transport.answerNext("get-config-document", ConfigReplies.valid())
        runFlowTest(transport) { deps ->
            val vm = EditorViewModel(deps)
            vm.open()
            advanceUntilIdle()
            vm.edit(edited)
            vm.save()
            advanceUntilIdle()
            assertEquals(SaveVerification.Unknown, vm.verify())
            assertTrue(vm.state.value is EditorState.SaveUnknown)
            assertEquals(1, transport.requests("set-config").size)
        }
    }

    @Test
    fun unreadableDocumentsExplainWithoutAnEditorAndADismissed36aAsksAgain() {
        val transport = FakeIpcTransport()
        transport.answerNext("get-config-document", ConfigReplies.missing())
        runFlowTest(transport) { deps ->
            val vm = EditorViewModel(deps)
            vm.open()
            advanceUntilIdle()
            assertTrue(vm.state.value is EditorState.ConfirmCreate)
            // 36a dismissed without an answer: the next open reads again and creates nothing.
            transport.answerNext("get-config-document", ConfigReplies.valid())
            vm.open()
            advanceUntilIdle()
            assertTrue(vm.state.value is EditorState.Editing)
            assertEquals(0, transport.writes())
        }
    }
}
