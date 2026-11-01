package phpvpn.android.wp11

import android.Manifest
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.uiautomator.By
import androidx.test.uiautomator.Direction
import androidx.test.uiautomator.UiObject2
import androidx.test.uiautomator.Until
import java.io.File
import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.JsonPrimitive
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.FixMethodOrder
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.MethodSorters
import phpvpn.android.R
import phpvpn.android.bridge.ipc.CheckpointResult
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.IpcOutcome
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.model.TrayStatus
import phpvpn.android.service.ServiceHarness

/**
 * WP11 end-to-end scenarios of the real app against a real relay (plan-android-v001
 * T-A06, T-A07, T-A14): the production container (JNI runtime, VPN service,
 * system consent) driven through the UI with UiAutomator, like the WP05
 * [phpvpn.android.service.VpnServiceTest].
 *
 * Instrumentation arguments: `phpvpnServerUrl` (the relay, profile
 * wizard-default of tools/deploy-test.ps1) and `phpvpnV2ServerUrl` (a copy of
 * the relay with SERVER_VERSION 2.0.0, client/tests/android/deploy-wp11-relays.ps1;
 * derived from `/wp11/apiNN/` → `/wp11/v2/` when not given).
 */
@RunWith(AndroidJUnit4::class)
@FixMethodOrder(MethodSorters.NAME_ASCENDING)
class RealRelayTest {
    private lateinit var h: ServiceHarness

    private val v2ServerUrl: String? by lazy {
        InstrumentationRegistry.getArguments().getString("phpvpnV2ServerUrl")
            ?: Regex("/wp11/api[0-9]+/").find(h.serverUrl)?.let { h.serverUrl.replace(it.value, "/wp11/v2/") }
    }

    @Before
    fun setUp() {
        h = ServiceHarness()
        h.app.useNativeRuntime()
        assumeTrue("relay ${h.serverUrl} is not reachable", h.relayReachable())
        h.shell("pm grant ${h.packageName} ${Manifest.permission.POST_NOTIFICATIONS}")
        h.ensureDisconnected()
        h.container.connectionEvents.clear()
        h.log("--- WP11 setUp done, relay ${h.serverUrl}")
    }

    @After
    fun tearDown() {
        runCatching { h.ensureDisconnected() }
        h.closeMain()
        h.app.useFakeTransport()
    }

    // --- T-A06 -------------------------------------------------------------------

    @Test
    fun a_ta06_wizardMainPathFromWelcomeToConnected() {
        h.revokeConsent()
        // 00 appears only for a missing configuration. The runtime creates the
        // default document when it starts (WP11 finding), so the file is
        // removed while the runtime runs.
        removeConfiguration()
        h.launchMain()
        h.click(h.text(R.string.welcome_set_up))
        verifyUrl(h.serverUrl)
        h.waitText(h.text(R.string.wizard_secret_heading))
        h.click(h.text(R.string.wizard_verify_continue))
        h.waitText(h.text(R.string.wizard_identity_heading))
        // The suggested IP fills the (disabled) IP field; wait past the screen transition.
        val ipField = h.device.wait(Until.findObject(By.clazz("android.widget.EditText").textStartsWith("10.99.99.")), UI_MS)
        assertNotNull("15 shows no suggested virtual IP", ipField)
        val suggested = ipField!!.text
        h.log("15 suggested virtual IP $suggested")
        assertTrue(suggested, suggested.startsWith("10.99.99."))
        h.click(h.text(R.string.wizard_verify_continue))
        h.waitText(h.text(R.string.wizard_finish_heading))
        h.click(h.text(R.string.wizard_finish_save))
        h.waitText(h.text(R.string.wizard_saved_heading))
        h.waitText(h.serverUrl)
        assertTrue("17 must not connect by itself", h.status()?.offline == true)
        h.click(h.text(R.string.action_connect_vpn))
        h.answerConsentDialog(accept = true)
        val started = SystemClock.elapsedRealtime()
        val status = h.waitStatus(CONNECT_MS, "connected") { it.healthyConnected }
        h.log("T-A06 connected ${SystemClock.elapsedRealtime() - started} ms after the consent: ${h.describe(status)}")
        for (check in listOf(status.serverUrl, status.serverSecret, status.accessKey, status.communicationContracts)) {
            assertEquals(h.describe(status), CheckpointResult.SUCCESS, check)
        }
        h.waitText(h.text(R.string.connected_heading))
        h.waitText(suggested)
        assertTrue("${h.ownVpn()}", h.ownVpn()?.second.orEmpty().any { it.startsWith("$suggested/") })
    }

    @Test
    fun b_ta06_wrongSecretShowsTheVerificationError18() {
        openWizardFromSettings()
        verifyUrl(h.serverUrl)
        h.waitText(h.text(R.string.wizard_secret_heading))
        // The stored default document has useDefaultServerSecretInWizard false: the field is editable.
        if (!editText().isEnabled) h.click(h.text(R.string.wizard_secret_use_default))
        val wrong = "wp11-wrong-secret-${SystemClock.elapsedRealtime()}"
        setField(wrong, expectShown = false)
        h.click(h.text(R.string.wizard_verify_continue))
        h.waitText(h.text(R.string.wizard_error_heading), CHECK_MS)
        h.waitText(h.text(R.string.wizard_error_intro))
        assertTrue("18 never repeats the secret", !h.device.hasObject(By.textContains(wrong)))
        h.click(h.text(R.string.wizard_error_correct))
        h.waitText(h.text(R.string.wizard_secret_heading))
        h.waitText(h.text(R.string.error_crypto_tag_mismatch))
    }

    @Test
    fun c_ta06_unreachableScriptShowsTheHttpDetails30And31() {
        openWizardFromSettings()
        val missing = h.serverUrl.substringBeforeLast('/') + "/wp11-missing.php"
        verifyUrl(missing)
        h.waitText(h.text(R.string.wp09_error_url_heading), CHECK_MS)
        h.waitText(h.text(R.string.wp09_http_status, 404))
        h.click(h.text(R.string.entry_http_error_details))
        h.waitText(h.text(R.string.http_heading))
        h.waitText(h.text(R.string.http_response_title))
        h.waitText(h.text(R.string.wp09_diag_source_wizard))
        h.click(h.text(R.string.http_tab_request))
        h.waitText(h.text(R.string.http_request_title))
        assertNotNull("the request line names the script", h.device.wait(Until.findObject(By.textContains("wp11-missing.php")), UI_MS))
    }

    // --- T-A14 -------------------------------------------------------------------

    @Test
    fun d_ta14_serverOfAnotherMajorVersionIsRefused() {
        val v2 = v2ServerUrl
        assumeTrue("no relay with SERVER_VERSION 2.0.0 (phpvpnV2ServerUrl)", v2 != null)
        openWizardFromSettings()
        verifyUrl(v2!!)
        h.waitText(h.text(R.string.wp09_error_version_heading), CHECK_MS)
        val versionText = h.device.wait(Until.findObject(By.textContains("2.0.0")), UI_MS)
        assertNotNull("the error names the server version 2.0.0", versionText)
        val text = versionText!!.text
        h.log("T-A14 wizard: $text")
        h.closeMain()
        // Connect with a stored configuration that points to the v2 relay.
        configure(v2)
        h.preConsent()
        runBlocking { h.container.connectActions.connect() }
        val status = h.waitStatus(CONNECT_MS, "server_version_incompatible") { it.state == ConnectionState.ERROR }
        h.log("T-A14 connect: ${h.describe(status)}")
        assertEquals(h.describe(status), "server_version_incompatible", status.errorCode)
        assertTrue("never a tunnel", h.ownVpn() == null)
        configure(h.serverUrl)
    }

    // --- T-A07 -------------------------------------------------------------------

    @Test
    fun e_ta07_saveWhileConnectedAndReconnect() {
        configure(h.serverUrl)
        h.preConsent()
        h.launchMain()
        h.click(h.text(R.string.action_connect_vpn))
        val first = h.waitStatus(CONNECT_MS, "connected") { it.healthyConnected }
        h.waitText(h.text(R.string.connected_heading))
        h.click(h.text(R.string.nav_settings))
        h.click(h.text(R.string.contracts_title))
        val peer = "10.99.99.5"
        scrollTo(peer).click()
        // The default document has no contracts: enable one with a key.
        h.click(h.text(R.string.contract_detail_enable))
        setField("5a".repeat(32), expectShown = false)
        h.click(h.text(R.string.contract_detail_apply))
        h.click(h.context.resources.getQuantityString(R.plurals.contracts_save_changes, 1, 1))
        h.waitText(h.text(R.string.saved_heading), CHECK_MS)
        h.waitText(h.text(R.string.saved_subtitle))
        assertTrue("still connected after the save", h.status()?.healthyConnected == true)
        val reconnectAt = SystemClock.elapsedRealtime()
        h.click(h.text(R.string.saved_reconnect))
        val second = h.waitStatus(CONNECT_MS, "connected again") {
            it.healthyConnected && it.connectionEpoch > first.connectionEpoch
        }
        h.log("T-A07 reconnected after ${SystemClock.elapsedRealtime() - reconnectAt} ms, epoch ${first.connectionEpoch} -> ${second.connectionEpoch}")
        h.waitText(h.text(R.string.connected_heading))
        val document = runBlocking { (h.container.ipcClient.getConfigDocument() as? IpcOutcome.Success)?.value?.document }
        assertNotNull(document)
        val text = document!!.read { String(it) }
        document.wipe()
        // The new contract is in the stored document (no key is logged).
        assertTrue("contract $peer stored", text.contains("\"$peer\""))
        configure(h.serverUrl)
    }

    // --- helpers -------------------------------------------------------------------

    private fun removeConfiguration() {
        val directory = File(h.context.noBackupFilesDir, "phpvpn/config")
        directory.listFiles()?.forEach { it.delete() }
        val state = runBlocking { h.container.ipcClient.getConfigState() }
        h.log("configuration removed: $state")
    }

    private fun openWizardFromSettings() {
        configure(h.serverUrl)
        h.launchMain()
        h.click(h.text(R.string.nav_settings))
        h.click(h.text(R.string.settings_wizard))
        h.waitText(h.text(R.string.wizard_url_heading))
    }

    private fun verifyUrl(url: String) {
        h.waitText(h.text(R.string.wizard_url_heading))
        setField(url)
        h.click(h.text(R.string.wizard_verify_continue))
    }

    /** Replaces the text of the only text field (ACTION_SET_TEXT, no keyboard, so no Back afterwards). */
    private fun setField(value: String, expectShown: Boolean = true) {
        editText().text = value
        if (expectShown) h.waitText(value)
    }

    private fun editText(): UiObject2 {
        val field = h.device.wait(Until.findObject(By.clazz("android.widget.EditText")), UI_MS)
        assertNotNull("no text field", field)
        return field!!
    }

    private fun scrollTo(text: String): UiObject2 {
        repeat(8) {
            h.device.findObject(By.text(text))?.let { return it }
            h.device.findObject(By.scrollable(true))?.scroll(Direction.DOWN, 0.5f)
        }
        return h.waitText(text)
    }

    /** The default document with [url] (the access keys of profile wizard-default). */
    private fun configure(url: String) = runBlocking {
        val client = h.container.ipcClient
        val default = (client.getDefaultConfig() as? IpcOutcome.Success)?.value?.document
        assertNotNull("get-default-config", default)
        val text = default!!.read { String(it) }
            .replace(Regex("\"serverUrl\"\\s*:\\s*\"[^\"]*\""), "\"serverUrl\": ${JsonPrimitive(url)}")
        default.wipe()
        val document = SecretText.adopt(text.toCharArray())
        val outcome = client.setConfig(document)
        document.wipe()
        assertTrue("set-config: $outcome", outcome is IpcOutcome.Success)
    }

    @Suppress("unused")
    private fun describe(status: TrayStatus?) = h.describe(status)

    private companion object {
        const val CONNECT_MS = 60_000L
        const val CHECK_MS = 30_000L
        const val UI_MS = 15_000L
    }
}
