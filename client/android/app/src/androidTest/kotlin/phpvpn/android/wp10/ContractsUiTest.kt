package phpvpn.android.wp10

import androidx.activity.ComponentActivity
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.hasAnyDescendant
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextReplacement
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import phpvpn.android.R
import phpvpn.android.bridge.ipc.StrictJson
import phpvpn.android.debug.ConfigReplies
import phpvpn.android.debug.ConfigReplies.answer
import phpvpn.android.debug.ConfigReplies.answerNext
import phpvpn.android.ui.navigation.openContracts
import phpvpn.android.ui.screens.contracts.TAG_APPLY
import phpvpn.android.ui.screens.contracts.TAG_ENABLE
import phpvpn.android.ui.screens.contracts.TAG_KEY
import phpvpn.android.ui.screens.contracts.TAG_PASSWORD
import phpvpn.android.ui.screens.contracts.TAG_ROW_PREFIX
import phpvpn.android.ui.screens.contracts.TAG_SAVE
import phpvpn.android.ui.screens.password.TAG_PASSWORD_FIELD
import phpvpn.android.ui.screens.password.TAG_USE_KEY
import phpvpn.android.ui.screens.save.TAG_SAVED_PRIMARY
import phpvpn.android.ui.screens.save.TAG_SAVE_AGAIN
import phpvpn.android.ui.screens.save.TAG_VERIFY_SAVE

/**
 * T-U03: Communication contracts on the emulator over the real navigation
 * graph: pagination with the draft, invalid key (22), key from a password
 * (21), save to 25, unknown save (33) and the conflict check (46).
 */
@RunWith(AndroidJUnit4::class)
class ContractsUiTest {
    @get:Rule
    val compose = createAndroidComposeRule<ComponentActivity>()

    private val keyA = "918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d"
    private val keyB = "f41f0ed508411aef592228e9d73ae137a4d31933f038160c04ae1e8d70597ebe"
    private val abcHash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"

    private fun open(harness: Wp10Harness): Wp10Harness {
        harness.transport.answerNext("get-config-document", ConfigReplies.valid())
        harness.navigate { openContracts() }
        harness.waitForTag(TAG_ROW_PREFIX + "10.99.99.3")
        return harness
    }

    private fun Wp10Harness.row(ip: String) = compose.onNodeWithTag(TAG_ROW_PREFIX + ip).performScrollTo().performClick()

    private fun Wp10Harness.enableWithKey(ip: String, key: String) {
        row(ip)
        compose.onNodeWithTag(TAG_ENABLE).performClick()
        field(TAG_KEY).performTextReplacement(key)
        hideKeyboard()
        compose.onNodeWithTag(TAG_APPLY).performClick()
        compose.waitForIdle()
    }

    @Test
    fun paginationKeepsTheDraftPerTargetIp() {
        val h = open(Wp10Harness(compose).launch())
        h.screenshot("19-contracts")
        h.enableWithKey("10.99.99.3", keyA)
        h.waitForText(h.plural(R.plurals.contracts_unsaved_changes, 1, 1))
        compose.onNodeWithContentDescription(h.text(R.string.contracts_page_next)).performScrollTo().performClick()
        h.waitForText(h.text(R.string.contracts_more_kept))
        compose.onNodeWithText(h.plural(R.plurals.contracts_page_range, 252, 21, 40, 252)).performScrollTo().assertExists()
        h.enableWithKey("10.99.99.30", keyB)
        h.waitForTagText(TAG_SAVE, h.plural(R.plurals.contracts_save_changes, 2, 2))
        h.screenshot("41-contracts-page-2")
        compose.onNodeWithContentDescription(h.text(R.string.contracts_page_previous)).performScrollTo().performClick()
        compose.onNodeWithText(h.plural(R.plurals.contracts_page_range, 252, 1, 20, 252)).performScrollTo().assertExists()
        compose.onNodeWithTag(TAG_ROW_PREFIX + "10.99.99.3").assert(hasAnyDescendant(hasText(h.text(R.string.contracts_peer_unsaved))))
        h.waitForTagText(TAG_SAVE, h.plural(R.plurals.contracts_save_changes, 2, 2))
        // The last page has the last address; the local IP is never a row.
        assertTrue(compose.onAllNodes(hasTestTag(TAG_ROW_PREFIX + "10.99.99.2")).fetchSemanticsNodes().isEmpty())
        assertEquals(0, h.transport.requests("set-config").size)
    }

    @Test
    fun invalidKeyBlocksSaveOn22AndOtherChangesStay() {
        val h = open(Wp10Harness(compose).launch())
        h.enableWithKey("10.99.99.4", keyA)
        h.row("10.99.99.3")
        compose.onNodeWithTag(TAG_ENABLE).performClick()
        h.field(TAG_KEY).performTextReplacement("a29f00")
        h.waitForText(h.text(R.string.contract_invalid_error, 6))
        assertTrue(h.secure)
        h.screenshot("20-contract-detail")
        compose.onNodeWithTag(TAG_APPLY).performClick()
        h.waitForText(h.text(R.string.contract_invalid_notice_text, "10.99.99.3"))
        compose.onNodeWithTag(TAG_SAVE).performClick()
        // 22: the error belongs to that contract and field.
        h.waitForText(h.text(R.string.contract_invalid_heading))
        h.waitForText(h.text(R.string.contract_invalid_error, 6))
        h.screenshot("22-contract-invalid")
        assertEquals(0, h.transport.requests("set-config").size)
        h.field(TAG_KEY).performTextReplacement(keyB)
        compose.onNodeWithTag(TAG_APPLY).performClick()
        h.waitForTagText(TAG_SAVE, h.plural(R.plurals.contracts_save_changes, 2, 2))
        h.transport.answer("set-config", ConfigReplies.ACK)
        // A connected VPN: 25 offers Reconnect.
        h.transport.answer("get-status", ConfigReplies.CONNECTED)
        compose.onNodeWithTag(TAG_SAVE).performClick()
        compose.waitForIdle()
        h.waitForTag(TAG_SAVED_PRIMARY)
        val submitted = StrictJson.parse(ConfigReplies.submitted(h.transport.requests("set-config").single())) as JsonObject
        assertEquals(
            JsonObject(
                mapOf(
                    "10.99.99.3" to JsonObject(mapOf("payloadKey" to JsonPrimitive(keyB))),
                    "10.99.99.4" to JsonObject(mapOf("payloadKey" to JsonPrimitive(keyA))),
                ),
            ),
            submitted["communicationContracts"],
        )
        h.screenshot("25-reconnect-save")
    }

    @Test
    fun keyFromPasswordGoesToTheEnabledContract() {
        val h = open(Wp10Harness(compose).launch())
        h.row("10.99.99.10")
        // A disabled contract has a read-only key and no password action.
        compose.onNodeWithTag(TAG_PASSWORD).assertIsNotEnabled()
        compose.onNodeWithTag(TAG_ENABLE).performClick()
        compose.onNodeWithTag(TAG_PASSWORD).performScrollTo().performClick()
        assertTrue(h.secure)
        h.waitForText(h.text(R.string.password_key_eyebrow, "10.99.99.10"))
        h.field(TAG_PASSWORD_FIELD).performTextReplacement("abc")
        h.waitForText(abcHash)
        h.screenshot("21-password-key")
        compose.onNodeWithTag(TAG_USE_KEY).assertIsEnabled().performClick()
        h.waitForText(h.text(R.string.contract_detail_title))
        compose.onNodeWithText(h.text(R.string.common_show)).performScrollTo().performClick()
        h.field(TAG_KEY).assert(hasText(abcHash))
        compose.onNodeWithTag(TAG_APPLY).performClick()
        h.waitForTagText(TAG_SAVE, h.plural(R.plurals.contracts_save_changes, 1, 1))
        assertEquals(0, h.transport.requests("set-config").size)
    }

    @Test
    fun unknownSaveIsVerifiedAndAConflictIsOnlyResavedExplicitly() {
        val h = open(Wp10Harness(compose).launch())
        h.enableWithKey("10.99.99.3", keyA)
        // No reply: the write may have happened.
        h.waitForText(h.plural(R.plurals.contracts_save_changes, 1, 1))
        compose.onNodeWithTag(TAG_SAVE).performClick()
        compose.waitForIdle()
        h.waitForText(h.text(R.string.save_unknown_heading))
        h.screenshot("33-save-unknown")
        assertEquals(1, h.transport.requests("set-config").size)
        // Someone else saved a different document meanwhile.
        h.transport.answerNext("get-config-document", ConfigReplies.valid(ConfigReplies.document(contracts = mapOf("10.99.99.5" to keyB))))
        compose.onNodeWithTag(TAG_VERIFY_SAVE).performClick()
        compose.waitForIdle()
        h.waitForText(h.text(R.string.save_checked_heading))
        h.screenshot("46-save-checked")
        assertEquals(1, h.transport.requests("set-config").size)
        h.transport.answer("set-config", ConfigReplies.ACK)
        compose.onNodeWithTag(TAG_SAVE_AGAIN).performClick()
        compose.waitForIdle()
        h.waitForTag(TAG_SAVED_PRIMARY)
        assertEquals(2, h.transport.requests("set-config").size)
    }

    @Test
    fun aMatchingCheckIsASuccessNotARetry() {
        val h = open(Wp10Harness(compose).launch())
        h.enableWithKey("10.99.99.3", keyA)
        h.waitForText(h.plural(R.plurals.contracts_save_changes, 1, 1))
        compose.onNodeWithTag(TAG_SAVE).performClick()
        compose.waitForIdle()
        h.waitForText(h.text(R.string.save_unknown_heading))
        val submitted = ConfigReplies.submitted(h.transport.requests("set-config").single())
        h.transport.answerNext("get-config-document", ConfigReplies.valid(submitted))
        compose.onNodeWithTag(TAG_VERIFY_SAVE).performClick()
        compose.waitForIdle()
        // CONFIRMED is success: followSaveOutcome leads straight to 25, no Save again anywhere.
        h.waitForTag(TAG_SAVED_PRIMARY)
        assertTrue(compose.onAllNodes(hasTestTag(TAG_SAVE_AGAIN)).fetchSemanticsNodes().isEmpty())
        assertEquals(1, h.transport.requests("set-config").size)
    }
}
