package phpvpn.android.service

import android.app.Notification
import android.app.NotificationManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.LinkProperties
import android.net.VpnService
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.util.Log
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.uiautomator.By
import androidx.test.uiautomator.UiDevice
import androidx.test.uiautomator.UiObject2
import androidx.test.uiautomator.Until
import java.net.InetSocketAddress
import java.net.Socket
import java.net.URI
import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.JsonPrimitive
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import phpvpn.android.AppContainer
import phpvpn.android.MainActivity
import phpvpn.android.TestApplication
import phpvpn.android.bridge.VpnInterfaces
import phpvpn.android.bridge.ipc.IpcOutcome
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.model.TrayStatus
import phpvpn.android.bridge.repository.ConnectionStatus

/**
 * The real app in the instrumented WP05 tests: the production container
 * (JNI runtime, [PhpvpnVpnService], system consent), the relay of WP04 and
 * the system dialogs through UiAutomator, as WP03 did for T-A01.
 */
class ServiceHarness {
    val instrumentation = InstrumentationRegistry.getInstrumentation()
    val context: Context = instrumentation.targetContext
    val app = context.applicationContext as TestApplication
    val device: UiDevice = UiDevice.getInstance(instrumentation)
    val container: AppContainer get() = app.container
    val packageName: String = context.packageName
    val testPackage: String = instrumentation.context.packageName

    /**
     * The relay: `http://10.0.2.2/phpvpn.php` (nginx on the host) or the
     * WP04 `php -S` relay with its proxy, given as the instrumentation argument
     * `phpvpnServerUrl`.
     */
    val serverUrl: String = InstrumentationRegistry.getArguments().getString("phpvpnServerUrl") ?: "http://10.0.2.2/phpvpn.php"

    fun log(message: String) {
        Log.i(TAG, message)
    }

    fun shell(command: String): String {
        val descriptor = instrumentation.uiAutomation.executeShellCommand(command)
        return ParcelFileDescriptor.AutoCloseInputStream(descriptor).use { it.readBytes().toString(Charsets.UTF_8) }.trim()
    }

    fun relayReachable(): Boolean {
        val uri = URI(serverUrl)
        return try {
            Socket().use { it.connect(InetSocketAddress(uri.host, if (uri.port > 0) uri.port else 80), 2_000) }
            true
        } catch (_: Exception) {
            false
        }
    }

    /** The default document of the runtime with the relay URL and [autoConnect] (no secret leaves the process). */
    fun configure(autoConnect: Boolean = false) = runBlocking {
        val client = container.ipcClient
        val default = (client.getDefaultConfig() as? IpcOutcome.Success)?.value?.document
        assertNotNull("get-default-config", default)
        val text = default!!.read { String(it) }
            .replace(Regex("\"serverUrl\"\\s*:\\s*\"[^\"]*\""), "\"serverUrl\": ${JsonPrimitive(serverUrl)}")
            .replace(Regex("\"autoConnectOnStartup\"\\s*:\\s*(true|false)"), "\"autoConnectOnStartup\": $autoConnect")
        default.wipe()
        val document = SecretText.adopt(text.toCharArray())
        val outcome = client.setConfig(document)
        document.wipe()
        assertTrue("set-config: $outcome", outcome is IpcOutcome.Success)
    }

    // --- VPN consent ----------------------------------------------------------

    /** Revokes the consent like the system settings do (`ACTIVATE_VPN` app op). */
    fun revokeConsent() {
        shell("appops set $packageName ACTIVATE_VPN ignore")
        // prepare() now unprepares the package and asks again.
        waitUntil(5_000, "consent revoked") { VpnService.prepare(context) != null }
    }

    /** Pre-consent without the dialog (only where the dialog itself is not under test). */
    fun preConsent() {
        shell("appops set $packageName ACTIVATE_VPN allow")
        waitUntil(5_000, "consent granted") { VpnService.prepare(context) == null }
    }

    fun consentGranted(): Boolean = VpnService.prepare(context) == null

    /** Answers the system ConfirmDialog (03): OK (button1) or Cancel (button2). */
    fun answerConsentDialog(accept: Boolean) {
        val button = device.wait(Until.findObject(By.res("android", if (accept) "button1" else "button2")), DIALOG_MS)
        assertNotNull("the VPN consent dialog did not appear", button)
        log("VPN consent dialog: ${if (accept) "accept" else "cancel"} (${button.text})")
        button.click()
    }

    // --- UI -------------------------------------------------------------------

    fun launchMain() {
        device.wakeUp()
        shell("wm dismiss-keyguard")
        context.startActivity(Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK))
        assertNotNull("MainActivity did not show", device.wait(Until.hasObject(By.pkg(packageName).depth(0)), UI_MS))
    }

    fun closeMain() {
        device.pressHome()
    }

    fun text(id: Int, vararg args: Any): String = context.getString(id, *args)

    fun waitText(text: String, timeoutMs: Long = UI_MS): UiObject2 {
        val found = device.wait(Until.findObject(By.text(text)), timeoutMs)
        assertNotNull("text not shown: $text", found)
        return found!!
    }

    fun click(text: String) {
        waitText(text).click()
    }

    fun startRival(stop: Boolean = false) {
        context.startActivity(
            Intent().setComponent(ComponentName(testPackage, RivalVpnActivity::class.java.name))
                .putExtra(RivalVpnActivity.EXTRA_STOP, stop)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
        )
    }

    // --- status, network, notification ---------------------------------------

    fun status(): TrayStatus? = runBlocking { (container.connection.readOnce() as? ConnectionStatus.Available)?.status }

    fun waitStatus(timeoutMs: Long, message: String, condition: (TrayStatus) -> Boolean): TrayStatus {
        var last: TrayStatus? = null
        waitUntil(timeoutMs, { "$message (last: ${describe(last)})" }) {
            last = status()
            last?.let(condition) == true
        }
        return last!!
    }

    fun describe(status: TrayStatus?): String = status?.let {
        "state=${it.state} phase=${it.phase} tunnel=${it.tunnelReady} error=${it.errorCode.ifEmpty { "none" }} " +
            "checks=${it.serverUrl}/${it.serverSecret}/${it.accessKey}/${it.communicationContracts}"
    } ?: "unavailable"

    /** The interface name and IPv4 addresses of this app's VPN network, or null without one. */
    fun ownVpn(): Pair<String?, List<String>>? {
        val connectivity = context.getSystemService(ConnectivityManager::class.java)
        val network = VpnInterfaces.ownVpnNetworks(context).firstOrNull() ?: return null
        val properties: LinkProperties = connectivity.getLinkProperties(network) ?: return null
        return properties.interfaceName to properties.linkAddresses.map { it.toString() }
    }

    fun serviceNotification(): Notification? = context.getSystemService(NotificationManager::class.java)
        .activeNotifications.firstOrNull { it.id == VpnNotifications.NOTIFICATION_ID }?.notification

    fun notificationTitle(): String? = serviceNotification()?.extras?.getCharSequence(Notification.EXTRA_TITLE)?.toString()

    /** Back to disconnected with the production path; the service then stops by itself. */
    fun ensureDisconnected() {
        val current = status()
        if (current != null && !(current.offline)) {
            runBlocking { container.connectActions.disconnect() }
        }
        waitStatus(30_000, "disconnected") { it.offline }
        waitUntil(10_000, "the VPN service stopped") { !VpnServiceCommands.running.value }
    }

    fun waitUntil(timeoutMs: Long, message: String, condition: () -> Boolean) = waitUntil(timeoutMs, { message }, condition)

    fun waitUntil(timeoutMs: Long, message: () -> String, condition: () -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + timeoutMs
        while (!condition()) {
            assertTrue("timed out: ${message()}", SystemClock.elapsedRealtime() < deadline)
            SystemClock.sleep(100)
        }
    }

    companion object {
        const val TAG = "phpvpn-test"
        const val DIALOG_MS = 15_000L
        const val UI_MS = 15_000L
    }
}
