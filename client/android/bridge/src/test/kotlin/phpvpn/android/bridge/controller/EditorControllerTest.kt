package phpvpn.android.bridge.controller

import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.Fixtures
import phpvpn.android.bridge.SequentialIds
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.MonotonicClock
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.respondFixture

/**
 * Edit Config flows of plan step 10 and T-U04 at the controller level:
 * missing → 36a → create-default-config, No creates nothing, created:false
 * never overwrites, Cancel after creation deletes nothing, Reset changes only
 * the draft, Save with the native classification, unknown → check → 46, and a
 * conflict is never overwritten silently.
 */
class EditorControllerTest {
    private val defaultDocument = Fixtures.text("default-config-document.json")

    private fun TestScope.editor(transport: FakeIpcTransport): EditorController {
        val clock = MonotonicClock { testScheduler.currentTime }
        return EditorController(IpcClient(transport, SequentialIds(), clock = clock), ReferenceSaveResponseClassifier, clock)
    }

    private fun FakeIpcTransport.writes() = requests("set-config").size + requests("create-default-config").size

    @Test
    fun missingConfigurationAsksBeforeCreatingAndNoCreatesNothing() = runTest {
        val transport = FakeIpcTransport()
        transport.respondFixture("get-config-document", "config-document-missing.json")
        val editor = editor(transport)
        editor.open()
        val confirm = editor.state.value as EditorState.ConfirmCreate
        assertEquals(Fixtures.CONFIG_PATH, confirm.configPath) // the actual path from the service
        editor.answerCreate(yes = false)
        assertEquals(EditorState.Closed, editor.state.value)
        assertEquals(0, transport.writes())
        assertEquals(5_000, transport.requests("get-config-document").single().timeoutMs)
    }

    @Test
    fun yesCreatesTheDefaultFileAndOpensTheActualResult() = runTest {
        val transport = FakeIpcTransport()
        transport.respondFixture("get-config-document", "config-document-missing.json")
        transport.respondFixture("create-default-config", "create-default-config-created.json")
        val editor = editor(transport)
        editor.open()
        editor.answerCreate(yes = true)
        val editing = editor.state.value as EditorState.Editing
        assertTrue(editing.session.created)
        assertEquals(defaultDocument, editing.session.draft.reveal())
        assertFalse(editing.session.dirty)
        // create-default-config carries neither a path nor content.
        assertEquals(setOf("ipcVersion", "requestId", "command"), transport.requests("create-default-config").single().json.keys)
        // Cancel after creation closes locally; nothing is deleted or written.
        editor.close()
        assertEquals(EditorState.Closed, editor.state.value)
        assertEquals(1, transport.writes())
        assertTrue(editing.session.draft.isWiped)
    }

    @Test
    fun createdFalseOpensTheDocumentThatAppearedWithoutOverwriting() = runTest {
        val transport = FakeIpcTransport()
        transport.respondFixture("get-config-document", "config-document-missing.json")
        transport.respondFixture("create-default-config", "create-default-config-race.json")
        val editor = editor(transport)
        editor.open()
        editor.answerCreate(yes = true)
        val editing = editor.state.value as EditorState.Editing
        assertFalse(editing.session.created)
        assertEquals("broken-json", editing.session.draft.reveal())
        assertEquals(ConfigAvailability.INVALID, editing.session.availability)
        assertEquals(0, transport.requests("set-config").size)
    }

    @Test
    fun readableDocumentsOpenAndUnreadableOnesExplain() = runTest {
        for ((fixture, availability) in listOf(
            "config-document-valid.json" to ConfigAvailability.VALID,
            "config-document-repairable.json" to ConfigAvailability.REPAIRABLE,
            "config-document-invalid.json" to ConfigAvailability.INVALID,
        )) {
            val transport = FakeIpcTransport()
            transport.respondFixture("get-config-document", fixture)
            val editor = editor(transport)
            editor.open()
            assertEquals(availability, (editor.state.value as EditorState.Editing).session.availability)
        }
        val transport = FakeIpcTransport()
        transport.respondFixture("get-config-document", "wizard-error-directory-security.json")
        val editor = editor(transport)
        editor.open()
        val failed = editor.state.value as EditorState.Failed
        assertEquals("config_directory_security_invalid", failed.failure.code)
        assertEquals(Fixtures.CONFIG_PATH, failed.failure.configPath)
        val unreadable = FakeIpcTransport()
        unreadable.respond("get-config-document") {
            FakeIpcTransport.Reply.Text(Fixtures.text("wizard-error-directory-security.json").replace("config_directory_security_invalid", "config_unreadable"))
        }
        val second = editor(unreadable)
        second.open()
        assertEquals(ConfigAvailability.UNREADABLE, (second.state.value as EditorState.Unavailable).availability)
        val timeout = FakeIpcTransport()
        val third = editor(timeout)
        third.open()
        assertTrue((third.state.value as EditorState.Failed).failure.timedOut)
    }

    @Test
    fun resetChangesOnlyTheDraft() = runTest {
        val transport = FakeIpcTransport()
        transport.respondFixture("get-config-document", "config-document-invalid.json")
        transport.respondFixture("get-default-config", "default-config.json")
        val editor = editor(transport)
        editor.open()
        editor.reset()
        val editing = editor.state.value as EditorState.Editing
        assertEquals(defaultDocument, editing.session.draft.reveal())
        assertTrue(editing.session.dirty)
        assertEquals(0, transport.writes())
        assertEquals(5_000, transport.requests("get-default-config").single().timeoutMs)
        transport.respond("get-default-config") { FakeIpcTransport.Reply.Timeout }
        editor.reset()
        val failed = editor.state.value as EditorState.Editing
        assertTrue(failed.notice is EditorNotice.ResetFailed)
        assertEquals(defaultDocument, failed.session.draft.reveal())
    }

    @Test
    fun confirmedSaveAndRejectedSave() = runTest {
        val transport = FakeIpcTransport()
        transport.respondFixture("get-config-document", "config-document-valid.json")
        transport.respondFixture("set-config", "ack.json")
        val editor = editor(transport)
        editor.open()
        editor.edit(SecretText.of(defaultDocument.replace("10.99.99.2", "10.99.99.3")))
        editor.save()
        assertEquals(EditorState.Saved(Fixtures.CONFIG_PATH), editor.state.value)
        val sent = transport.requests("set-config").single()
        assertEquals(30_000, sent.timeoutMs)
        assertTrue(phpvpn.android.bridge.ipc.JsonChecks.string(sent.json["config"])!!.contains("10.99.99.3"))

        val rejecting = FakeIpcTransport()
        rejecting.respondFixture("get-config-document", "config-document-valid.json")
        rejecting.respond("set-config") {
            FakeIpcTransport.Reply.Text("""{"ipcVersion":1,"requestId":"${'$'}ID","error":{"code":"config_invalid_value","description":"masterSetup.httpHeaders line 2: invalid field name","recoverable":false}}""")
        }
        val second = editor(rejecting)
        second.open()
        second.save()
        val editing = second.state.value as EditorState.Editing
        val notice = editing.notice as EditorNotice.Rejected
        assertEquals(ErrorCode.CONFIG_INVALID_VALUE, notice.code)
        assertEquals("masterSetup.httpHeaders line 2: invalid field name", notice.validationDescription)
    }

    @Test
    fun unknownSaveIsCheckedAndMatchedWriteIsSuccess() = runTest {
        val transport = FakeIpcTransport()
        transport.respondFixture("get-config-document", "config-document-valid.json")
        transport.respond("set-config") { FakeIpcTransport.Reply.Timeout }
        val editor = editor(transport)
        editor.open()
        editor.save()
        val unknown = editor.state.value as EditorState.SaveUnknown
        // While unknown, Save is not possible and nothing is retried automatically.
        editor.save()
        assertEquals(1, transport.requests("set-config").size)
        editor.checkSaveResult()
        val checked = editor.state.value as EditorState.SaveChecked
        assertTrue(checked.matched)
        assertEquals(unknown.session, checked.session)
        assertEquals(1, transport.requests("set-config").size)
    }

    @Test
    fun conflictAfterUnknownSaveIsNeverOverwrittenSilently() = runTest {
        val transport = FakeIpcTransport()
        transport.respondFixture("get-config-document", "config-document-valid.json")
        transport.respond("set-config") { FakeIpcTransport.Reply.Text("""{"ipcVersion":1,"requestId":"${'$'}ID","error":{"code":"config_write_failed","description":"The configuration file could not be written.","recoverable":false}}""") }
        val editor = editor(transport)
        editor.open()
        editor.edit(SecretText.of(defaultDocument.replace("10.99.99.2", "10.99.99.9")))
        editor.save()
        // config_write_failed does not prove an unchanged disk: unknown, not rejected.
        assertTrue(editor.state.value is EditorState.SaveUnknown)
        editor.checkSaveResult()
        val checked = editor.state.value as EditorState.SaveChecked
        assertFalse(checked.matched)
        assertEquals(1, transport.requests("set-config").size)
        // Only the explicit decision writes again.
        transport.respondFixture("set-config", "ack.json")
        editor.repeatSave()
        assertEquals(2, transport.requests("set-config").size)
        assertTrue(editor.state.value is EditorState.Saved)
    }

    @Test
    fun checkFailureKeepsTheUnknownState() = runTest {
        val transport = FakeIpcTransport()
        transport.respondFixture("get-config-document", "config-document-valid.json")
        transport.respond("set-config") { FakeIpcTransport.Reply.Timeout }
        val editor = editor(transport)
        editor.open()
        editor.save()
        transport.respond("get-config-document") { FakeIpcTransport.Reply.Timeout }
        editor.checkSaveResult()
        val unknown = editor.state.value as EditorState.SaveUnknown
        assertTrue((unknown.notice as EditorNotice.CheckFailed).failure.timedOut)
        transport.respondFixture("get-config-document", "config-document-missing.json")
        editor.checkSaveResult()
        assertFalse((editor.state.value as EditorState.SaveChecked).matched)
    }
}
