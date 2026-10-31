package phpvpn.android

import android.app.Application
import android.content.Context
import androidx.annotation.VisibleForTesting
import kotlinx.coroutines.cancel
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.bridge.ipc.IpcTransport
import phpvpn.android.ui.state.SystemVpnConsent

/**
 * Owns the [AppContainer]. Until WP04 lands the JNI transport, the container
 * runs on a [FakeIpcTransport] without scripted replies, so every command
 * times out and the UI shows the "status unavailable" states (08, 27, 45):
 * the honest answer while no native runtime is attached.
 *
 * Open so that UI tests can install a test application with a scripted
 * transport (androidTest `PhpvpnTestRunner`).
 */
open class PhpvpnApplication : Application() {
    lateinit var container: AppContainer
        private set

    override fun onCreate() {
        super.onCreate()
        container = createContainer()
    }

    /** WP05 adds the service-backed Always-on status and wraps Connect here. */
    protected open fun createContainer(): AppContainer = AppContainer(createTransport(), SystemVpnConsent(this))

    /** WP04 replaces this with the JNI transport once the native runtime starts here. */
    protected open fun createTransport(): IpcTransport = FakeIpcTransport()

    /** UI tests: drop every repository and flag and start from a fresh container. */
    @VisibleForTesting
    fun resetContainer() {
        container.scope.cancel()
        container = createContainer()
    }
}

/** The process-wide container; valid for any Context of this application. */
val Context.appContainer: AppContainer
    get() = (applicationContext as PhpvpnApplication).container
