package phpvpn.android.wizard

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertTextContains
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextReplacement
import androidx.navigation.NavDestination.Companion.hasRoute
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import phpvpn.android.R
import phpvpn.android.debug.FakeWizardService
import phpvpn.android.debug.FakeWizardService.Check
import phpvpn.android.debug.FakeWizardService.Finish
import phpvpn.android.ui.navigation.Connection
import phpvpn.android.ui.navigation.DiagnosticRequest
import phpvpn.android.ui.navigation.DiagnosticResponse
import phpvpn.android.ui.navigation.DiagnosticSource
import phpvpn.android.ui.navigation.SaveUnknown
import phpvpn.android.ui.navigation.WizardError
import phpvpn.android.ui.navigation.WizardExpired
import phpvpn.android.ui.navigation.WizardGate
import phpvpn.android.ui.navigation.WizardIdentity
import phpvpn.android.ui.navigation.WizardSaved
import phpvpn.android.ui.navigation.WizardSecret
import phpvpn.android.ui.navigation.WizardUrl

/**
 * T-U01 for WP09: the main path 12 → 14 → 15 → 16 → 17, the error branches
 * of the screen map (18, 30/31, 47, 33, 11 after a foreign Connect), Back and
 * the Unsaved changes dialog (32), Reset (40), the header editor (13) and
 * FLAG_SECURE on 13, 14 and 15 only.
 */
@RunWith(AndroidJUnit4::class)
class WizardFlowTest : WizardUiTestBase() {
    private val url = "https://vpn.example.com/phpvpn.php"

    private inline fun <reified T : Any> at(): Boolean = nav.currentDestination?.hasRoute<T>() == true

    private fun waitForRoute(timeoutMs: Long = 10_000, check: () -> Boolean) = compose.waitUntil(timeoutMs) { check() }

    private fun enterUrlAndVerify() {
        waitForText(R.string.wizard_url_heading)
        field(R.string.wizard_url_field).performTextReplacement(url)
        click(R.string.wizard_verify_continue)
    }

    @Test
    fun mainPathFromTheServerAddressToTheSavedConfiguration() {
        launch(WizardUrl)
        assertFalse("12 shows no secret", secure())
        enterUrlAndVerify()
        waitForText(R.string.wizard_secret_heading)
        assertTrue(at<WizardSecret>())
        waitForSecure(true)
        click(R.string.wizard_verify_continue)
        waitForText(R.string.wizard_identity_heading)
        assertTrue(at<WizardIdentity>())
        assertTrue("15 keeps FLAG_SECURE", secure())
        // The verified suggestion fills the disabled IP field.
        field(R.string.wizard_identity_ip_field).assertTextContains("10.99.99.10")
        // The default key preview: hex of the last octet (.10 = 0a), 32 times.
        field(R.string.wizard_identity_key_field).assertTextContains("0a".repeat(32))
        click(R.string.wizard_verify_continue)
        waitForText(R.string.wizard_finish_heading)
        waitForSecure(false)
        // 13 default contracts are missing and 13 are added on Finish (.2 to .15 without .10).
        assertEquals(2, compose.onAllNodesWithText("13").fetchSemanticsNodes().size)
        click(R.string.wizard_finish_save)
        waitForText(R.string.wizard_saved_heading)
        assertTrue(at<WizardSaved>())
        compose.onNodeWithText(url).assertIsDisplayed()
        assertEquals(1, service.requests("wizard-finish").size)
        assertEquals(1, service.requests("wizard-close").size)
        assertEquals(0, service.requests("connect").size)
    }

    @Test
    fun secretMismatchShowsTheVerificationErrorWithoutTheSecret() {
        launch(WizardUrl) { checks["secret"] = Check.Fail("crypto_tag_mismatch", "serverSecret") }
        enterUrlAndVerify()
        waitForText(R.string.wizard_secret_heading)
        field(R.string.wizard_secret_field).performTextReplacement("synthetic-secret-value")
        click(R.string.wizard_verify_continue)
        waitForText(R.string.wizard_error_heading)
        assertTrue(at<WizardError>())
        assertTrue("18 never repeats the secret", compose.onAllNodes(hasText("synthetic-secret-value", substring = true)).fetchSemanticsNodes().isEmpty())
        assertTrue("no HTTP detail without a capture", compose.onAllNodesWithText(text(R.string.entry_http_error_details)).fetchSemanticsNodes().isEmpty())
        click(R.string.wizard_error_correct)
        waitForText(R.string.wizard_secret_heading)
        assertTrue(at<WizardSecret>())
        compose.onNodeWithText(text(R.string.error_crypto_tag_mismatch)).performScrollTo().assertIsDisplayed()
    }

    @Test
    fun failedUrlCheckOpensTheHttpDetailTabs() {
        launch(WizardUrl) { checks["url"] = Check.Fail("response_invalid", "serverUrl", httpStatus = 200, diagnostic = true) }
        enterUrlAndVerify()
        waitForText(R.string.wp09_error_url_heading)
        compose.onNodeWithText(text(R.string.wp09_http_status, 200)).assertIsDisplayed()
        click(R.string.entry_http_error_details)
        waitForText(R.string.http_heading)
        assertTrue(at<DiagnosticResponse>())
        compose.onNodeWithText(text(R.string.http_response_notice_title)).assertIsDisplayed()
        compose.onNodeWithText("HTTP/1.1 503 Service Unavailable", substring = true).assertIsDisplayed()
        compose.onNodeWithText("\\x01", substring = true).assertExists()
        compose.onNodeWithText(text(R.string.wp09_diag_source_wizard)).assertIsDisplayed()
        compose.onNodeWithText(text(R.string.http_tab_request)).performClick()
        waitForText(R.string.http_request_title)
        assertTrue(at<DiagnosticRequest>())
        compose.onNodeWithText(text(R.string.http_request_notice_title)).assertIsDisplayed()
        compose.onNodeWithText(text(R.string.wp09_diag_http_unknown), substring = true).assertIsDisplayed()
        pressBack()
        waitForRoute { at<WizardError>() }
        click(R.string.wp09_error_correct_url)
        waitForRoute { at<WizardUrl>() }
        field(R.string.wizard_url_field).assertTextContains(url)
    }

    @Test
    fun incompatibleServerVersionNamesBothVersionsAndMarksTheUrl() {
        launch(WizardUrl) {
            checks["url"] = Check.Fail(
                "server_version_incompatible",
                "serverUrl",
                description = "The server uses a different major version. Client version 1.0.3, server version 2.0.0.",
            )
        }
        enterUrlAndVerify()
        waitForText(R.string.wp09_error_version_heading)
        compose.onNodeWithText("2.0.0", substring = true).assertIsDisplayed()
        click(R.string.wp09_error_correct_url)
        waitForRoute { at<WizardUrl>() }
        compose.onNodeWithText(text(R.string.error_server_version_incompatible)).assertIsDisplayed()
    }

    @Test
    fun backOnTheFirstStepAsksBeforeDiscarding() {
        launch(WizardUrl)
        waitForText(R.string.wizard_url_heading)
        field(R.string.wizard_url_field).performTextReplacement(url)
        pressBack()
        waitForText(R.string.discard_heading)
        click(R.string.action_keep_editing)
        assertTrue(at<WizardUrl>())
        field(R.string.wizard_url_field).assertTextContains(url)
        pressBack()
        waitForText(R.string.discard_heading)
        click(R.string.action_discard_changes)
        waitForRoute { at<Connection>() }
        compose.waitUntil(5_000) { service.requests("wizard-close").size == 1 }
    }

    @Test
    fun backOnALaterStepReturnsToThePreviousStepAndCancelAsks() {
        launch(WizardUrl)
        enterUrlAndVerify()
        waitForText(R.string.wizard_secret_heading)
        pressBack()
        waitForRoute { at<WizardUrl>() }
        field(R.string.wizard_url_field).assertTextContains(url)
        assertTrue("Back is not a discard", compose.onAllNodesWithText(text(R.string.discard_heading)).fetchSemanticsNodes().isEmpty())
        click(R.string.wizard_verify_continue)
        waitForText(R.string.wizard_secret_heading)
        click(R.string.wizard_cancel_setup)
        waitForText(R.string.discard_heading)
        click(R.string.action_discard_changes)
        waitForRoute { at<Connection>() }
        compose.waitUntil(5_000) { !secure() }
    }

    @Test
    fun cleanWizardLeavesWithoutTheDialog() {
        launch(WizardUrl)
        waitForText(R.string.wizard_url_heading)
        click(R.string.wizard_cancel_setup)
        waitForRoute { at<Connection>() }
    }

    @Test
    fun gateDisconnectsOnceAndContinuesToTheServerAddress() {
        launch(WizardGate) {
            vpnState = "connected"
            vpnPhase = "running"
            epoch = 3uL
            disconnectReads = 1
        }
        waitForText(R.string.gate_heading)
        compose.onNodeWithText(text(R.string.tag_connected_short)).assertIsDisplayed()
        click(R.string.gate_disconnect_continue)
        waitForText(R.string.wizard_url_heading)
        assertTrue(at<WizardUrl>())
        assertEquals(1, service.requests("disconnect").size)
        assertEquals(1, service.requests("wizard-resume").size)
        // The gate is not on the back stack: Back leaves the clean Wizard.
        pressBack()
        waitForRoute { at<Connection>() }
    }

    @Test
    fun disconnectedClientSkipsTheGate() {
        launch(WizardGate)
        waitForText(R.string.wizard_url_heading)
        assertTrue(at<WizardUrl>())
        assertEquals(0, service.requests("disconnect").size)
    }

    @Test
    fun foreignConnectReturnsToTheGateAndKeepsTheEntries() {
        launch(WizardUrl) { checks["secret"] = Check.Interrupt }
        enterUrlAndVerify()
        waitForText(R.string.wizard_secret_heading)
        click(R.string.wizard_verify_continue)
        waitForText(R.string.wp09_gate_interrupted_title)
        assertTrue(at<WizardGate>())
        waitForSecure(false)
        click(R.string.gate_disconnect_continue)
        waitForText(R.string.wizard_url_heading)
        field(R.string.wizard_url_field).assertTextContains(url)
        assertEquals(1, service.requests("disconnect").size)
    }

    @Test
    fun expiredSessionOffersANewSession() {
        launch(WizardUrl) { checks["url"] = Check.Fail("wizard_session_expired", null) }
        enterUrlAndVerify()
        waitForText(R.string.session_expired_heading)
        assertTrue(at<WizardExpired>())
        service.checks["url"] = Check.Succeed
        click(R.string.session_expired_reopen)
        waitForText(R.string.wizard_url_heading)
        compose.waitUntil(5_000) { service.requests("wizard-open").size == 2 }
    }

    @Test
    fun unknownSaveGoesToSaveUnknownAndTheReceiptConfirmsIt() {
        launch(WizardUrl) { finish = Finish.ReplyLost; commitState = "confirmed" }
        enterUrlAndVerify()
        waitForText(R.string.wizard_secret_heading)
        click(R.string.wizard_verify_continue)
        waitForText(R.string.wizard_identity_heading)
        click(R.string.wizard_verify_continue)
        waitForText(R.string.wizard_finish_heading)
        click(R.string.wizard_finish_save)
        waitForRoute { at<SaveUnknown>() }
        pressBack()
        waitForText(R.string.wp09_finish_unknown_title)
        click(R.string.save_unknown_verify)
        waitForText(R.string.wizard_saved_heading)
        assertEquals(1, service.requests("wizard-finish").size)
        assertEquals(1, service.requests("wizard-get-commit").size)
    }

    @Test
    fun resetLoadsTheDefaultPlaceholder() {
        launch(WizardUrl)
        waitForText(R.string.wizard_url_heading)
        field(R.string.wizard_url_field).performTextReplacement(url)
        click(R.string.wizard_url_defaults)
        waitForText(R.string.wizard_reset_heading)
        click(R.string.wizard_reset_confirm)
        compose.waitUntil(5_000) { service.requests("wizard-reset").size == 1 }
        compose.waitUntil(5_000) {
            runCatching { field(R.string.wizard_url_field).assertTextContains(FakeWizardService.PLACEHOLDER_URL) }.isSuccess
        }
        click(R.string.wizard_verify_continue)
        compose.onNodeWithText(text(R.string.wp09_url_placeholder)).assertIsDisplayed()
        assertEquals("the placeholder never reaches the network", 0, service.requests("wizard-check-url").size)
    }

    @Test
    fun headerEditorValidatesAndIsSecure() {
        launch(WizardUrl)
        waitForText(R.string.wizard_url_heading)
        compose.onAllNodesWithText(text(R.string.wizard_url_headers))[0].performClick()
        waitForText(R.string.headers_heading)
        waitForSecure(true)
        field(R.string.headers_field).performTextReplacement("Host: example")
        click(R.string.headers_use)
        compose.onNodeWithText(text(R.string.wp09_headers_error_managed, 1, "Host")).assertIsDisplayed()
        field(R.string.headers_field).performTextReplacement("Cookie: synthetic=1\nUser-Agent: test")
        click(R.string.headers_use)
        waitForText(R.string.wizard_url_heading)
        waitForSecure(false)
        field(R.string.wizard_url_field).performTextReplacement(url)
        click(R.string.wizard_verify_continue)
        compose.waitUntil(5_000) { service.requests("wizard-check-url").isNotEmpty() }
        assertEquals(
            "the accepted headers are used by the next check",
            "Cookie: synthetic=1\r\nUser-Agent: test",
            phpvpn.android.bridge.ipc.JsonChecks.string(service.requests("wizard-check-url").last().json["httpHeaders"]),
        )
    }

    @Test
    fun connectDiagnosticShowsTheCapturedAttempt() {
        launch(DiagnosticResponse(DiagnosticSource.Connect)) {
            vpnState = "error"
            vpnPhase = "idle"
            epoch = 1uL
            connectDiagnostic = true
        }
        waitForText(R.string.wp09_diag_source_connect)
        compose.onNodeWithText("HTTP/1.1 503 Service Unavailable", substring = true).assertIsDisplayed()
        compose.onNodeWithText(text(R.string.http_response_hint)).performScrollTo().assertIsDisplayed()
    }
}
