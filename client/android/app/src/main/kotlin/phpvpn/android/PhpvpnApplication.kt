package phpvpn.android

import android.app.Application
import android.content.Context
import phpvpn.android.bridge.ipc.FakeIpcTransport

/**
 * Owns the [AppContainer]. Until WP04 lands the JNI transport, the container
 * runs on a [FakeIpcTransport] without scripted replies, so every command
 * times out and the UI shows the "status unavailable" states (08, 27, 45):
 * the honest answer while no native runtime is attached.
 */
class PhpvpnApplication : Application() {
    lateinit var container: AppContainer
        private set

    override fun onCreate() {
        super.onCreate()
        container = AppContainer(createTransport())
    }

    /** WP04 replaces this with the JNI transport once the native runtime starts here. */
    private fun createTransport() = FakeIpcTransport()
}

/** The process-wide container; valid for any Context of this application. */
val Context.appContainer: AppContainer
    get() = (applicationContext as PhpvpnApplication).container
