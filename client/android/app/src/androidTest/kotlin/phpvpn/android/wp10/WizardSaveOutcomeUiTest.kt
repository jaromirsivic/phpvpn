package phpvpn.android.wp10

import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTextReplacement
import androidx.navigation.NavDestination.Companion.hasRoute
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import phpvpn.android.R
import phpvpn.android.debug.FakeWizardService.Finish
import phpvpn.android.ui.navigation.SaveUnknown
import phpvpn.android.ui.navigation.WizardFinish
import phpvpn.android.ui.navigation.WizardSaved
import phpvpn.android.ui.navigation.WizardUrl
import phpvpn.android.ui.screens.save.TAG_SAVE_AGAIN
import phpvpn.android.ui.screens.save.TAG_VERIFY_SAVE
import phpvpn.android.wizard.WizardUiTestBase

/**
 * The Wizard's path through the shared screen 33 of WP10 (WP09 contract):
 * origin Wizard never offers a repeat, "Verify save" sends one
 * wizard-get-commit, CONFIRMED leads to 17 and NOT_SAVED back to 16 with the
 * draft; Finish is never sent a second time.
 */
@RunWith(AndroidJUnit4::class)
class WizardSaveOutcomeUiTest : WizardUiTestBase() {
    private fun toUnknownSave(commit: String) {
        launch(WizardUrl) {
            finish = Finish.ReplyLost
            commitState = commit
        }
        waitForText(R.string.wizard_url_heading)
        field(R.string.wizard_url_field).performTextReplacement("https://vpn.example.com/phpvpn.php")
        click(R.string.wizard_verify_continue)
        waitForText(R.string.wizard_secret_heading)
        click(R.string.wizard_verify_continue)
        waitForText(R.string.wizard_identity_heading)
        click(R.string.wizard_verify_continue)
        waitForText(R.string.wizard_finish_heading)
        click(R.string.wizard_finish_save)
        compose.waitUntil(10_000) { nav.currentDestination?.hasRoute<SaveUnknown>() == true }
        waitForText(R.string.save_unknown_heading)
        assertTrue(compose.onAllNodes(hasTestTag(TAG_SAVE_AGAIN)).fetchSemanticsNodes().isEmpty())
    }

    @Test
    fun confirmedReceiptOnTheShared33LeadsTo17() {
        toUnknownSave("confirmed")
        compose.onNodeWithTag(TAG_VERIFY_SAVE).performClick()
        compose.waitUntil(10_000) { nav.currentDestination?.hasRoute<WizardSaved>() == true }
        waitForText(R.string.wizard_saved_heading)
        assertEquals(1, service.requests("wizard-finish").size)
        assertEquals(1, service.requests("wizard-get-commit").size)
    }

    @Test
    fun failedReceiptOnTheShared33ReturnsTo16WithoutASecondFinish() {
        toUnknownSave("failed")
        compose.onNodeWithTag(TAG_VERIFY_SAVE).performClick()
        compose.waitUntil(10_000) { nav.currentDestination?.hasRoute<WizardFinish>() == true }
        waitForText(R.string.wizard_finish_heading)
        assertEquals(1, service.requests("wizard-finish").size)
        assertEquals(1, service.requests("wizard-get-commit").size)
    }
}
