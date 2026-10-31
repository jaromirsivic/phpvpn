package phpvpn.android

import android.app.Application
import android.content.Context
import android.content.Intent
import androidx.activity.result.ActivityResultLauncher
import androidx.test.runner.AndroidJUnitRunner
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.bridge.ipc.IpcTransport
import phpvpn.android.ui.state.VpnConsent

/** Instrumentation runner that installs [TestApplication] instead of the real application. */
class PhpvpnTestRunner : AndroidJUnitRunner() {
    override fun newApplication(cl: ClassLoader?, className: String?, context: Context?): Application =
        super.newApplication(cl, TestApplication::class.java.name, context)
}

/**
 * The application of the UI tests: every container gets a fresh scripted
 * [FakeIpcTransport] and the [TestConsent] instead of `VpnService.prepare`.
 */
class TestApplication : PhpvpnApplication() {
    lateinit var transport: FakeIpcTransport
        private set
    val consent = TestConsent()

    override fun createContainer(): AppContainer = AppContainer(createTransport(), consent)

    override fun createTransport(): IpcTransport = FakeIpcTransport().also { transport = it }
}

/**
 * Consent fake: [granted] is the `prepare() == null` answer; otherwise
 * [request] launches a placeholder intent that the test's
 * ActivityResultRegistry answers with [nextResult] (03 is a system dialog).
 */
class TestConsent : VpnConsent {
    @Volatile var granted = true

    @Volatile var nextResult = false

    @Volatile var requests = 0

    override fun isGranted(): Boolean = granted

    override fun request(launcher: ActivityResultLauncher<Intent>): Boolean {
        if (granted) return false
        requests++
        launcher.launch(Intent(ACTION))
        return true
    }

    companion object {
        const val ACTION = "phpvpn.test.VPN_CONSENT"
    }
}
