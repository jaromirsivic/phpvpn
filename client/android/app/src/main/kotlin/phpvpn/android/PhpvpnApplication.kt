package phpvpn.android

import android.app.Application
import android.content.Context
import phpvpn.android.bridge.NativeRuntime
import phpvpn.android.bridge.jni.JniIpcTransport
import phpvpn.android.bridge.jni.NativeConfigDocumentCheck
import phpvpn.android.bridge.jni.NativeServerInfoHook

/**
 * Owns the [AppContainer]. The native runtime starts here, once per process
 * (plan-android-v001, "Proces a životnost"); the service and the boot receiver
 * of WP05 call [NativeRuntime.ensureStarted] as well, which then returns at
 * once. The container talks to the runtime through the in-process JNI
 * transport and checks configuration documents with the native validator.
 * If the start failed, every exchange returns no reply and the UI shows the
 * "status unavailable" states (08, 27, 45), never a success.
 */
class PhpvpnApplication : Application() {
    lateinit var container: AppContainer
        private set

    override fun onCreate() {
        super.onCreate()
        NativeRuntime.ensureStarted(this)
        container = AppContainer(
            transport = JniIpcTransport(),
            documentCheck = NativeConfigDocumentCheck,
            serverInfoHook = NativeServerInfoHook(),
        )
    }
}

/** The process-wide container; valid for any Context of this application. */
val Context.appContainer: AppContainer
    get() = (applicationContext as PhpvpnApplication).container
