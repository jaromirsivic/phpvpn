package phpvpn.android.ui

import androidx.activity.ComponentActivity
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.assertIsDisplayed
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import phpvpn.android.R
import phpvpn.android.TestApplication
import phpvpn.android.testing.IpcReplies
import phpvpn.android.testing.respondText
import phpvpn.android.testing.respondTimeout
import phpvpn.android.ui.components.MainDestination
import phpvpn.android.ui.screens.connection.CHECKS_TAG
import phpvpn.android.ui.screens.connection.ConnectionTags
import phpvpn.android.ui.screens.server.ServerTags
import phpvpn.android.ui.screens.settings.ABOUT_TAG
import phpvpn.android.ui.screens.settings.ATTENTION_TAG
import phpvpn.android.ui.screens.settings.EXIT_TAG
import phpvpn.android.ui.screens.settings.SETTINGS_TAG
import phpvpn.android.ui.screens.settings.WELCOME_TAG
import phpvpn.android.ui.screens.traffic.TrafficTags
import phpvpn.android.ui.state.DisconnectReason

/**
 * T-U01 (WP08 part): the `flow` scenarios of assets/android-en/manifest.json
 * that touch Connection, Traffic, Server and Settings, against a scripted
 * FakeIpcTransport in a test AppContainer. Real time: the repositories poll
 * every 500 ms during transitions and every 5 s otherwise.
 */
@RunWith(AndroidJUnit4::class)
class MainFlowsTest {
    @get:Rule
    val rule: UiRule = createAndroidComposeRule<ComponentActivity>()

    private lateinit var app: TestApplication
    private val transport get() = app.transport

    @Before
    fun setUp() {
        app = freshApp()
        transport.respondText("get-config-state", IpcReplies.configState())
        transport.respondText("get-config-document", IpcReplies.configDocument())
        transport.respondText("get-status", IpcReplies.disconnected())
    }

    @Test
    fun disconnect_01_07_02() {
        transport.respondText("get-status", IpcReplies.connected())
        rule.showApp(app)
        rule.waitForTag(ConnectionTags.CONNECTED)
        transport.respondText("disconnect", IpcReplies.stopping())
        transport.respondText("get-status", IpcReplies.stopping())
        rule.click(rule.text(R.string.action_disconnect_vpn))
        rule.waitForTag(ConnectionTags.DISCONNECTING)
        transport.respondText("get-status", IpcReplies.disconnected())
        rule.waitForTag(ConnectionTags.DISCONNECTED)
        assertEquals(1, transport.count("disconnect"))
    }

    @Test
    fun connect_02_04_01_withGrantedConsent() {
        rule.showApp(app)
        rule.waitForTag(ConnectionTags.DISCONNECTED)
        transport.respondText("connect", IpcReplies.connecting())
        transport.respondText("get-status", IpcReplies.connecting())
        rule.click(rule.text(R.string.action_connect_vpn))
        rule.waitForTag(ConnectionTags.CONNECTING)
        // The four checks and the note that the screen can be left (04).
        rule.waitForText(rule.text(R.string.connecting_notice_title))
        rule.waitForText(rule.text(R.string.check_state_pending))
        assertEquals(0, app.consent.requests)
        transport.respondText("get-status", IpcReplies.connected())
        rule.waitForTag(ConnectionTags.CONNECTED)
        assertEquals(1, transport.count("connect"))
    }

    @Test
    fun connectionError_06_withoutDiagnostic() {
        transport.respondText("get-status", IpcReplies.failed())
        rule.showApp(app)
        rule.waitForTag(ConnectionTags.FAILED)
        rule.waitForText(rule.text(R.string.error_connection_endpoint_unavailable_or_invalid_title))
        rule.waitForText(rule.text(R.string.connection_error_check_url))
        assertTrue(rule.hasNoText(rule.text(R.string.entry_http_error_details)))
    }

    @Test
    fun connectionError_06_withDiagnosticAndInputSeparation() {
        transport.respondText("get-status", IpcReplies.failed(diagnostic = true))
        rule.showApp(app)
        rule.waitForTag(ConnectionTags.FAILED)
        rule.waitForText(rule.text(R.string.entry_http_error_details))
        rule.waitForText(rule.text(R.string.connection_error_eyebrow))
        transport.respondText("get-status", IpcReplies.failed("access_key_rejected"))
        rule.waitForText(rule.text(R.string.connection_error_eyebrow_input), 12_000)
        assertTrue(rule.hasNoText(rule.text(R.string.entry_http_error_details)))
        // 06 → 12: the configuration route opens the wizard URL step.
        rule.click(rule.text(R.string.connection_error_check_configuration))
        rule.waitForText(rule.text(R.string.wizard_url_title))
    }

    @Test
    fun statusUnavailable_08_thenAnyConfirmedState() {
        transport.respondTimeout("get-status")
        rule.showApp(app)
        rule.waitForTag(ConnectionTags.UNAVAILABLE)
        rule.waitForText(rule.text(R.string.unavailable_last_known_none))
        transport.respondText("get-status", IpcReplies.connected())
        rule.click(rule.text(R.string.unavailable_check_again))
        rule.waitForTag(ConnectionTags.CONNECTED)
        assertEquals(0, transport.count("connect"))
    }

    @Test
    fun permissionDenied_37_retryGrantsAndConnects() {
        app.consent.granted = false
        app.consent.nextResult = false
        val nav = rule.showApp(app)
        rule.waitForTag(ConnectionTags.DISCONNECTED)
        rule.click(rule.text(R.string.action_connect_vpn))
        rule.waitForTag(ConnectionTags.PERMISSION)
        assertEquals(0, transport.count("connect"))
        // A second denial stays on 37.
        rule.click(rule.text(R.string.permission_denied_retry))
        rule.waitForIdle()
        rule.waitForTag(ConnectionTags.PERMISSION)
        assertEquals(2, app.consent.requests)
        // Granted: Connect starts and the status screen returns.
        app.consent.nextResult = true
        transport.respondText("connect", IpcReplies.connecting())
        transport.respondText("get-status", IpcReplies.connecting())
        rule.click(rule.text(R.string.permission_denied_retry))
        rule.waitForTag(ConnectionTags.CONNECTING)
        assertEquals(1, transport.count("connect"))
        rule.runOnUiThread { assertTrue(nav.currentDestination?.route?.startsWith("connection") == true) }
    }

    @Test
    fun permissionDenied_37_stayDisconnected() {
        app.consent.granted = false
        rule.showApp(app)
        rule.waitForTag(ConnectionTags.DISCONNECTED)
        rule.click(rule.text(R.string.action_connect_vpn))
        rule.waitForTag(ConnectionTags.PERMISSION)
        rule.click(rule.text(R.string.permission_denied_stay))
        rule.waitForTag(ConnectionTags.DISCONNECTED)
        assertEquals(0, transport.count("connect"))
    }

    @Test
    fun disconnectUnconfirmed_38_checkStatus() {
        transport.respondText("get-status", IpcReplies.connected())
        rule.showApp(app)
        rule.waitForTag(ConnectionTags.CONNECTED)
        transport.respondText("disconnect", IpcReplies.error("busy", recoverable = true))
        rule.click(rule.text(R.string.action_disconnect_vpn))
        rule.waitForTag(ConnectionTags.UNCONFIRMED)
        rule.click(rule.text(R.string.disconnect_unknown_check))
        rule.waitForTag(ConnectionTags.CONNECTED)
    }

    @Test
    fun revokedConnectionExplainsItselfOn02() {
        app.container.connectionEvents.report(DisconnectReason.REVOKED)
        rule.showApp(app)
        rule.waitForTag(ConnectionTags.DISCONNECTED)
        rule.waitForText(rule.text(R.string.disconnected_revoked_title))
    }

    @Test
    fun checks_48_neverConnects() {
        transport.respondText("get-status", IpcReplies.connected())
        rule.showApp(app)
        rule.waitForTag(ConnectionTags.CONNECTED)
        rule.click(rule.text(R.string.connected_checks_entry))
        rule.waitForTag(CHECKS_TAG)
        rule.waitForText(rule.text(R.string.checks_heading))
        rule.pressBack()
        rule.waitForTag(ConnectionTags.CONNECTED)
        assertEquals(0, transport.count("connect"))
    }

    @Test
    fun backFromTrafficServerSettingsReturnsToConnection() {
        transport.respondText("get-server-info", IpcReplies.serverInfoSample())
        val nav = rule.showApp(app)
        rule.waitForTag(ConnectionTags.DISCONNECTED)
        for ((label, tag) in listOf(
            R.string.nav_traffic to TrafficTags.CURRENT,
            R.string.nav_server to ServerTags.LIST,
            R.string.nav_settings to SETTINGS_TAG,
        )) {
            rule.clickNav(rule.text(label))
            rule.waitForTag(tag)
            rule.pressBack()
            rule.waitForTag(ConnectionTags.DISCONNECTED)
            rule.runOnUiThread { assertEquals("connection", nav.currentDestination?.route) }
        }
        rule.runOnUiThread { assertEquals(MainDestination.Connection.name.lowercase(), nav.currentDestination?.route) }
    }

    @Test
    fun exit_35_closeWindowOnlyFinishesTheActivity() {
        transport.respondText("get-status", IpcReplies.connected())
        val nav = rule.showApp(app)
        rule.waitForTag(ConnectionTags.CONNECTED)
        rule.runOnUiThread { nav.navigate(phpvpn.android.ui.navigation.Exit) }
        rule.waitForTag(EXIT_TAG)
        rule.click(rule.text(R.string.exit_close_window))
        rule.waitUntil(5_000) { rule.activity.isFinishing }
        assertEquals(0, transport.count("disconnect"))
    }

    @Test
    fun exit_35_disconnectAndCloseWaitsForConfirmation() {
        transport.respondText("get-status", IpcReplies.connected())
        rule.showApp(app)
        rule.waitForTag(ConnectionTags.CONNECTED)
        rule.clickNav(rule.text(R.string.nav_settings))
        rule.click(rule.text(R.string.settings_exit))
        rule.waitForTag(EXIT_TAG)
        transport.respondText("disconnect", IpcReplies.stopping())
        transport.respondText("get-status", IpcReplies.stopping())
        rule.click(rule.text(R.string.exit_disconnect_close))
        rule.waitForText(rule.text(R.string.disconnecting_progress))
        Thread.sleep(1_500)
        assertTrue("must not close before the confirmed disconnect", !rule.activity.isFinishing)
        transport.respondText("get-status", IpcReplies.disconnected())
        rule.waitUntil(10_000) { rule.activity.isFinishing }
        assertEquals(1, transport.count("disconnect"))
    }

    @Test
    fun trafficAndServerVariants() {
        transport.respondTimeout("get-status")
        transport.respondText("get-server-info", IpcReplies.error("timeout", recoverable = true))
        rule.showApp(app)
        rule.clickNav(rule.text(R.string.nav_traffic))
        rule.waitForTag(TrafficTags.WAITING)
        transport.respondText("get-status", IpcReplies.connected(IpcReplies.Counters(bytesReceived = 18_420_000, packetsReceived = 14_826)))
        rule.waitForTag(TrafficTags.CURRENT, 12_000)
        rule.waitForText("18.42 MB")
        transport.respondTimeout("get-status")
        rule.waitForTag(TrafficTags.STALE, 12_000)
        rule.clickNav(rule.text(R.string.nav_server))
        rule.waitForTag(ServerTags.FAILED)
        transport.respondText("get-server-info", IpcReplies.serverInfo())
        rule.click(rule.text(R.string.server_stale_retry))
        rule.waitForTag(ServerTags.EMPTY)
        transport.respondText("get-server-info", IpcReplies.serverInfoSample())
        rule.click(rule.text(R.string.action_refresh_now))
        rule.waitForTag(ServerTags.LIST)
        rule.waitForText(rule.text(R.string.server_contract_invalid))
        transport.respondTimeout("get-server-info")
        rule.click(rule.text(R.string.action_refresh_now))
        rule.waitForTag(ServerTags.STALE)
        rule.waitForText(rule.text(R.string.tag_last_status_online))
        rule.click(rule.text(R.string.server_tab_raw))
        rule.waitForTag(ServerTags.RAW)
    }

    @Test
    fun welcome_00_whenNoConfigurationExists() {
        transport.respondText("get-config-state", IpcReplies.configState("missing", "config_not_found"))
        val nav = rule.showApp(app)
        rule.waitForTag(WELCOME_TAG)
        rule.runOnUiThread { assertEquals("welcome", nav.currentDestination?.route) }
        // Editor entry goes through 36a with the real path; nothing is created yet.
        rule.click(rule.text(R.string.welcome_open_editor))
        rule.waitForText(rule.text(R.string.create_default_title))
        assertEquals(0, transport.count("create-default-config"))
    }

    @Test
    fun attention_36_forAnInvalidDocumentAndAbout_34() {
        transport.respondText("get-config-state", IpcReplies.configState("invalid", "config_invalid_json"))
        rule.showApp(app)
        rule.waitForTag(ATTENTION_TAG)
        rule.waitForText(rule.text(R.string.attention_repair))
        rule.pressBack()
        rule.waitForTag(ConnectionTags.DISCONNECTED)
        rule.clickNav(rule.text(R.string.nav_settings))
        rule.click(rule.text(R.string.settings_about))
        rule.waitForTag(ABOUT_TAG)
        rule.onNodeWithTag(ABOUT_TAG, useUnmergedTree = true).assertIsDisplayed()
        rule.waitForText("1.0.3-test")
    }
}
