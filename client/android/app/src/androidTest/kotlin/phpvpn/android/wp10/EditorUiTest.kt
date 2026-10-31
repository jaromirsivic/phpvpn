package phpvpn.android.wp10

import androidx.activity.ComponentActivity
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.hasAnyAncestor
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.isDialog
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextReplacement
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import phpvpn.android.R
import phpvpn.android.debug.ConfigReplies
import phpvpn.android.debug.ConfigReplies.answer
import phpvpn.android.debug.ConfigReplies.answerNext
import phpvpn.android.ui.navigation.openEditor
import phpvpn.android.ui.screens.editor.TAG_CREATE_DIALOG
import phpvpn.android.ui.screens.editor.TAG_CREATE_NO
import phpvpn.android.ui.screens.editor.TAG_CREATE_YES
import phpvpn.android.ui.screens.editor.TAG_EDITOR_BACK
import phpvpn.android.ui.screens.editor.TAG_EDITOR_ERROR
import phpvpn.android.ui.screens.editor.TAG_EDITOR_RESET
import phpvpn.android.ui.screens.editor.TAG_EDITOR_SAVE
import phpvpn.android.ui.screens.editor.TAG_EDITOR_TEXT
import phpvpn.android.ui.screens.editor.TAG_RESET_CONFIRM
import phpvpn.android.ui.screens.editor.TAG_RESET_CONTINUE
import phpvpn.android.ui.screens.save.TAG_SAVED_PRIMARY

/**
 * T-U04: the editor with a missing configuration (36a: No creates nothing,
 * Yes creates and opens the actual document, created:false never
 * overwrites, Cancel after the creation deletes nothing) and the editor
 * flows 23, 24, 42, 39 and 25 over the real navigation graph.
 */
@RunWith(AndroidJUnit4::class)
class EditorUiTest {
    @get:Rule
    val compose = createAndroidComposeRule<ComponentActivity>()

    private val path = ConfigReplies.CONFIG_PATH

    private fun missing(): Wp10Harness {
        val h = Wp10Harness(compose).launch()
        h.transport.answer("get-config-document", ConfigReplies.missing())
        h.navigate { openEditor() }
        compose.onNodeWithTag(TAG_CREATE_DIALOG).assertExists()
        // The actual absolute path reported by the service.
        compose.onNodeWithText(h.text(R.string.create_default_text, path)).assertExists()
        return h
    }

    private fun Wp10Harness.commands() = transport.requests.map { it.command }

    @Test
    fun noCreatesNothingAndReturns() {
        val h = missing()
        h.screenshot("36a-create-default")
        compose.onNodeWithTag(TAG_CREATE_NO).performScrollTo().performClick()
        compose.waitForIdle()
        assertTrue(compose.onAllNodes(hasTestTag(TAG_CREATE_DIALOG)).fetchSemanticsNodes().isEmpty())
        assertEquals(listOf("get-config-document"), h.commands())
        // Not the editor: the screen it was opened from.
        assertTrue(compose.onAllNodes(hasTestTag(TAG_EDITOR_SAVE)).fetchSemanticsNodes().isEmpty())
    }

    @Test
    fun yesCreatesAndOpensTheActualDocumentAndCancelDeletesNothing() {
        val h = missing()
        h.transport.answer("create-default-config", ConfigReplies.createDefault(created = true))
        compose.onNodeWithTag(TAG_CREATE_YES).performScrollTo().performClick()
        compose.waitForIdle()
        compose.onNodeWithText(h.text(R.string.editor_created_title)).assertExists()
        h.field(TAG_EDITOR_TEXT).assert(hasText(ConfigReplies.DEFAULT_DOCUMENT))
        assertTrue(h.secure)
        h.screenshot("23-editor-created")
        compose.onNodeWithText(h.text(R.string.editor_path, path)).performScrollTo().assertExists()
        h.screenshot("23-editor-created-path")
        // Edit, then Back → 32 → Discard: the created file stays, nothing else is sent.
        h.field(TAG_EDITOR_TEXT).performTextReplacement(ConfigReplies.document(virtualIp = "10.99.99.9"))
        compose.waitForIdle()
        compose.runOnUiThread { compose.activity.onBackPressedDispatcher.onBackPressed() }
        compose.onNodeWithText(h.text(R.string.discard_heading)).assertExists()
        compose.onNode(hasText(h.text(R.string.action_discard_changes)) and hasAnyAncestor(isDialog())).performClick()
        compose.waitForIdle()
        assertTrue(compose.onAllNodes(hasTestTag(TAG_EDITOR_SAVE)).fetchSemanticsNodes().isEmpty())
        assertEquals(listOf("get-config-document", "create-default-config"), h.commands())
        assertFalse(h.secure)
    }

    @Test
    fun createdFalseOpensTheFileThatAppearedWithoutOverwriting() {
        val h = missing()
        val appeared = ConfigReplies.document(virtualIp = "10.99.99.77")
        h.transport.answer("create-default-config", ConfigReplies.createDefault(created = false, document = appeared))
        compose.onNodeWithTag(TAG_CREATE_YES).performScrollTo().performClick()
        compose.waitForIdle()
        compose.onNodeWithText(h.text(R.string.editor_existing_title)).assertExists()
        h.field(TAG_EDITOR_TEXT).assert(hasText(appeared))
        assertEquals(0, h.transport.requests("set-config").size)
        assertEquals(1, h.transport.requests("create-default-config").size)
    }

    @Test
    fun resetChangesOnlyTheDraftAndSaveLeadsTo25() {
        val h = Wp10Harness(compose).launch()
        val current = ConfigReplies.document(virtualIp = "10.99.99.9")
        h.transport.answer("get-config-document", ConfigReplies.valid(current))
        h.transport.answer("get-default-config", ConfigReplies.defaultConfig())
        h.transport.answer("get-status", ConfigReplies.DISCONNECTED)
        h.navigate { openEditor() }
        h.field(TAG_EDITOR_TEXT).assert(hasText(current))
        compose.onNodeWithTag(TAG_EDITOR_RESET).performScrollTo().performClick()
        compose.onNodeWithText(h.text(R.string.reset_heading)).assertExists()
        h.screenshot("24-reset")
        compose.onNodeWithTag(TAG_RESET_CONFIRM).performScrollTo().performClick()
        compose.waitForIdle()
        compose.onNodeWithText(h.text(R.string.editor_default_heading)).assertExists()
        h.screenshot("42-editor-default")
        assertEquals(0, h.transport.requests("set-config").size)
        compose.onNodeWithTag(TAG_RESET_CONTINUE).performClick()
        compose.waitForIdle()
        h.field(TAG_EDITOR_TEXT).assert(hasText(ConfigReplies.DEFAULT_DOCUMENT))
        compose.onNodeWithText(h.text(R.string.editor_field_dirty)).performScrollTo().assertExists()
        h.transport.answer("set-config", ConfigReplies.ACK)
        compose.onNodeWithTag(TAG_EDITOR_SAVE).performClick()
        compose.waitForIdle()
        compose.onNodeWithTag(TAG_SAVED_PRIMARY).assertExists()
        // Not connected: Connect instead of Reconnect.
        compose.onNodeWithText(h.text(R.string.saved_connect_notice_title)).assertExists()
        h.screenshot("25-saved-inactive")
        assertEquals(ConfigReplies.DEFAULT_DOCUMENT, ConfigReplies.submitted(h.transport.requests("set-config").single()))
    }

    @Test
    fun rejectedSaveShows39WithTheValidatorTextAndKeepsTheDraft() {
        val h = Wp10Harness(compose).launch()
        h.transport.answer("get-config-document", ConfigReplies.valid())
        h.transport.answerNext("set-config", ConfigReplies.INVALID_HEADER)
        h.navigate { openEditor() }
        val draft = ConfigReplies.document(virtualIp = "10.99.99.9")
        h.field(TAG_EDITOR_TEXT).performTextReplacement(draft)
        compose.onNodeWithTag(TAG_EDITOR_SAVE).performClick()
        compose.waitForIdle()
        compose.onNodeWithText(h.text(R.string.editor_invalid_heading)).assertExists()
        compose.onNodeWithTag(TAG_EDITOR_ERROR).assert(
            hasText("masterSetup.httpHeaders line 2: empty line or missing colon " + h.text(R.string.editor_invalid_changes_kept), substring = true),
        )
        assertTrue(h.secure)
        h.screenshot("39-editor-invalid")
        compose.onNodeWithTag(TAG_EDITOR_BACK).performClick()
        h.field(TAG_EDITOR_TEXT).assert(hasText(draft))
        assertEquals(1, h.transport.requests("set-config").size)
    }
}
