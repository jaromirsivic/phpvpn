package phpvpn.android.service

import android.Manifest
import android.app.Notification
import android.content.pm.PackageManager
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.uiautomator.By
import androidx.test.uiautomator.Direction
import androidx.test.uiautomator.Until
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
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
import phpvpn.android.bridge.model.TrayStatus
import phpvpn.android.ui.state.AlwaysOnState
import phpvpn.android.ui.state.DisconnectReason

/**
 * WP05 instrumented tests of the real service against the relay
 * (plan-android-v001 T-A02, T-A05, T-A09, T-A11, Exit 35 and the notification
 * permission). The process-killing scenarios (T-A10, T-A12, T-A16) cannot run
 * inside the instrumented process; client/tests/android/service-lifecycle-tests.sh
 * drives them with adb. Run with the instrumentation argument
 * `phpvpnServerUrl` when the relay is not nginx on the host.
 */
@RunWith(AndroidJUnit4::class)
@FixMethodOrder(MethodSorters.NAME_ASCENDING)
class VpnServiceTest {
    private lateinit var h: ServiceHarness

    @Before
    fun setUp() {
        h = ServiceHarness()
        h.app.useNativeRuntime()
        assumeTrue("relay ${h.serverUrl} is not reachable", h.relayReachable())
        h.configure()
        h.ensureDisconnected()
        h.container.connectionEvents.clear()
        h.log("--- setUp done, relay ${h.serverUrl}")
    }

    @After
    fun tearDown() {
        runCatching { h.ensureDisconnected() }
        h.closeMain()
        h.app.useFakeTransport()
    }

    private fun connectFromUi(expectConsentDialog: Boolean) {
        h.launchMain()
        h.click(h.text(R.string.action_connect_vpn))
        if (expectConsentDialog) h.answerConsentDialog(accept = true)
    }

    private fun waitConnected(): TrayStatus {
        val started = SystemClock.elapsedRealtime()
        val status = h.waitStatus(CONNECT_MS, "connected") { it.healthyConnected }
        h.log("connected after ${SystemClock.elapsedRealtime() - started} ms: ${h.describe(status)}")
        return status
    }

    @Test
    fun a_notificationPermissionDeniedIsExplainedOnceAndTheVpnStillRuns() {
        assumeTrue(
            "POST_NOTIFICATIONS is already granted (the APK was installed with it); the denial path is not testable without killing the process",
            h.context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED,
        )
        h.context.getSharedPreferences("phpvpn-ui", 0).edit().clear().commit()
        h.preConsent()
        h.launchMain()
        h.click(h.text(R.string.action_connect_vpn))
        val deny = h.device.wait(Until.findObject(By.res("com.android.permissioncontroller", "permission_deny_button")), ServiceHarness.DIALOG_MS)
        assertNotNull("the notification permission dialog did not appear", deny)
        deny.click()
        h.waitText(h.text(R.string.notifications_off_title))
        h.click(h.text(R.string.notifications_off_ok))
        waitConnected()
        assertTrue("the foreground service runs without the notification permission", VpnServiceCommands.running.value)
        assertFalse("asked only once", h.container.notificationPermission.shouldRequest())
        h.log("notification without permission: ${h.serviceNotification() != null}")
        h.shell("pm grant ${h.packageName} ${Manifest.permission.POST_NOTIFICATIONS}")
    }

    @Test
    fun b_ta02_connectThroughTheSystemConsentDialog() {
        h.shell("pm grant ${h.packageName} ${Manifest.permission.POST_NOTIFICATIONS}")
        h.revokeConsent()
        connectFromUi(expectConsentDialog = true)
        val status = waitConnected()
        assertTrue(h.consentGranted())
        for (check in listOf(status.serverUrl, status.serverSecret, status.accessKey, status.communicationContracts)) {
            assertEquals(h.describe(status), CheckpointResult.SUCCESS, check)
        }
        // The interface: tunN with the virtual IP of the default configuration (10.99.99.x/24).
        val vpn = h.ownVpn()
        assertNotNull("no VPN network of the app", vpn)
        h.log("VPN network: ${vpn!!.first} ${vpn.second}")
        assertTrue(vpn.first.orEmpty().startsWith("tun"))
        assertTrue(vpn.second.toString(), vpn.second.any { it.startsWith("10.99.99.") && it.endsWith("/24") })
        // 09: title = state, one Disconnect action, text = server and virtual IP.
        h.waitUntil(3_000, "notification 'Connected'") { h.notificationTitle() == h.text(R.string.notification_title_connected) }
        val notification = h.serviceNotification()!!
        assertEquals(listOf(h.text(R.string.notification_action_disconnect)), notification.actions.orEmpty().map { it.title.toString() })
        val text = notification.extras.getCharSequence(Notification.EXTRA_TEXT)?.toString().orEmpty()
        h.log("notification text: $text")
        assertTrue(text, text.contains("10.99.99."))
        assertTrue("ongoing", notification.flags and Notification.FLAG_ONGOING_EVENT != 0)
        // 01 in the UI, the R9 flag, the service and the Always-on report.
        h.waitText(h.text(R.string.connected_heading))
        assertTrue(IntentStore.get(h.context).read().wantConnected)
        assertTrue(VpnServiceCommands.running.value)
        assertEquals(AlwaysOnState(alwaysOn = false, lockdown = false), ServiceAlwaysOnStatus.state.value)
    }

    @Test
    fun c_ta05_disconnectRemovesTheNotificationAndTheVpnNetwork() {
        h.preConsent()
        connectFromUi(expectConsentDialog = false)
        waitConnected()
        h.waitText(h.text(R.string.connected_heading))
        h.click(h.text(R.string.action_disconnect_vpn))
        val started = SystemClock.elapsedRealtime()
        h.waitStatus(30_000, "disconnected") { it.offline }
        val confirmed = SystemClock.elapsedRealtime() - started
        h.waitUntil(5_000, "notification removed") { h.serviceNotification() == null }
        val notificationGone = SystemClock.elapsedRealtime() - started
        h.waitUntil(5_000, "VPN network removed") { h.ownVpn() == null }
        val networkGone = SystemClock.elapsedRealtime() - started
        h.waitUntil(5_000, "service stopped") { !VpnServiceCommands.running.value }
        h.log("disconnect: confirmed after $confirmed ms, notification gone $notificationGone ms, network gone $networkGone ms")
        h.waitText(h.text(R.string.disconnected_heading))
        assertFalse(IntentStore.get(h.context).read().wantConnected)
    }

    @Test
    fun d_ta09_anotherVpnAppTakesTheSlot() {
        h.preConsent()
        connectFromUi(expectConsentDialog = false)
        waitConnected()
        h.closeMain()
        try {
            h.shell("appops set ${h.testPackage} ACTIVATE_VPN allow")
            h.startRival()
            val started = SystemClock.elapsedRealtime()
            h.waitStatus(20_000, "disconnected after onRevoke") { it.offline }
            h.log("onRevoke: disconnected after ${SystemClock.elapsedRealtime() - started} ms")
            assertEquals(DisconnectReason.REVOKED, h.container.connectionEvents.reason.value)
            val record = IntentStore.get(h.context).read()
            assertFalse(record.wantConnected)
            assertEquals(DisconnectReason.REVOKED, record.notice)
            h.waitUntil(5_000, "service stopped") { !VpnServiceCommands.running.value }
            h.launchMain()
            h.waitText(h.text(R.string.disconnected_revoked_title))
            // Never a reconnect: 10 s later still disconnected, no interface, no service.
            SystemClock.sleep(10_000)
            val later = h.status()
            assertTrue(h.describe(later), later?.offline == true)
            // The other app's VPN network shows with a hidden owner uid; ours (10.99.99.x) is gone.
            assertTrue("${h.ownVpn()}", h.ownVpn()?.second.orEmpty().none { it.startsWith("10.99.99.") })
            assertFalse(VpnServiceCommands.running.value)
            // phpvpn never took the slot back (prepare() would): the other app's VPN is still up.
            // (Not checked with prepare() here, which would take it back itself.)
            assertTrue("${h.ownVpn()}", h.ownVpn()?.second.orEmpty().any { it.startsWith("10.111.0.2") })
        } finally {
            h.startRival(stop = true)
            h.shell("appops set ${h.testPackage} ACTIVATE_VPN default")
            h.preConsent()
        }
    }

    @Test
    fun e_ta11_deniedConsentShows37() {
        h.revokeConsent()
        connectFromUi(expectConsentDialog = false)
        h.answerConsentDialog(accept = false)
        h.waitText(h.text(R.string.permission_denied_heading))
        val status = h.status()
        assertTrue(h.describe(status), status?.offline == true)
        assertFalse(VpnServiceCommands.running.value)
        assertFalse(IntentStore.get(h.context).read().wantConnected)
        assertFalse(h.consentGranted())
    }

    @Test
    fun f_ta11_revokedConsentEndsInTunnelPermissionMissing() {
        h.preConsent()
        // "Forget VPN" in the settings: the consent is gone, the UI did not ask again.
        h.revokeConsent()
        // The Connect of 25 (Changes saved) does not check the consent first.
        val reply = runBlocking { h.container.connectActions.connect() }
        h.log("connect reply: $reply")
        val status = h.waitStatus(CONNECT_MS, "tunnel_permission_missing") { it.state == ConnectionState.ERROR }
        assertEquals(h.describe(status), "tunnel_permission_missing", status.errorCode)
        assertFalse(status.errorRecoverable)
        assertEquals(CheckpointResult.SUCCESS, status.accessKey)
        h.waitUntil(5_000, "service stopped after the final error") { !VpnServiceCommands.running.value }
        assertNull(h.ownVpn())
        assertNull(h.serviceNotification())
        h.launchMain()
        h.waitText(h.text(R.string.permission_denied_heading))
    }

    @Test
    fun g_exitCloseWindowKeepsTheVpnAndDisconnectAndCloseStopsIt() {
        h.preConsent()
        connectFromUi(expectConsentDialog = false)
        waitConnected()
        openExit()
        h.click(h.text(R.string.exit_close_window))
        assertTrue("the window closes", h.device.wait(Until.gone(By.pkg(h.packageName).depth(0)), 10_000))
        SystemClock.sleep(1_000)
        assertTrue(h.status()?.healthyConnected == true)
        assertTrue(VpnServiceCommands.running.value)
        assertNotNull(h.serviceNotification())

        openExit()
        h.click(h.text(R.string.exit_disconnect_close))
        assertTrue("the task closes", h.device.wait(Until.gone(By.pkg(h.packageName).depth(0)), 40_000))
        assertTrue(h.describe(h.status()), h.status()?.offline == true)
        assertFalse("the service stopped before the task closed", VpnServiceCommands.running.value)
        assertNull(h.serviceNotification())
        assertFalse(IntentStore.get(h.context).read().wantConnected)
    }

    private fun openExit() {
        h.launchMain()
        h.click(h.text(R.string.nav_settings))
        val exit = h.text(R.string.settings_exit)
        repeat(5) {
            if (h.device.hasObject(By.text(exit))) return@repeat
            h.device.findObject(By.scrollable(true))?.scroll(Direction.DOWN, 1.0f)
        }
        h.click(exit)
    }

    private companion object {
        const val CONNECT_MS = 60_000L
    }
}
