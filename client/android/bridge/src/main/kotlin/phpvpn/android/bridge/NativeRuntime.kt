package phpvpn.android.bridge

import android.content.Context

/**
 * The process-wide native runtime (plan-android-v001, "Proces a životnost").
 *
 * [ensureStarted] starts `ServiceRuntime` on first use from any entry point
 * (Application, Activity, Service or BootReceiver) and keeps it for the life
 * of the process; later calls return at once. Before the start it registers
 * the process-wide [AndroidNetworkHost] over the application context, so
 * `recoverPreviousRun` and the preflight see the networks from the first
 * snapshot on (point 18). The [TunnelHost] is registered by the VPN service
 * itself (WP05). The startup Connect is never armed here (life cycle step 7).
 */
object NativeRuntime {
    private val lock = Any()

    @Volatile
    var started: Boolean = false
        private set

    /**
     * Starts the runtime once per process. Returns a `NativeBridge.START_*`
     * code; [NativeBridge.START_OK] also when it already ran. After a failure
     * the next call tries again. Runs storage preparation and configuration
     * recovery on the calling thread (a few milliseconds of private file I/O).
     */
    fun ensureStarted(context: Context): Int {
        if (started) return NativeBridge.START_OK
        synchronized(lock) {
            if (started) return NativeBridge.START_OK
            val application = context.applicationContext
            VpnHosts.registerNetworkHost(AndroidNetworkHost(application))
            val code = NativeBridge.nativeStart(
                application.noBackupFilesDir.absolutePath.encodeToByteArray(),
                application.packageName.encodeToByteArray(),
            )
            if (code == NativeBridge.START_OK || code == NativeBridge.START_ALREADY_STARTED) {
                started = true
                return NativeBridge.START_OK
            }
            return code
        }
    }

    /** Stops the runtime (tests and an orderly process exit); [ensureStarted] may start it again. */
    fun stop() {
        synchronized(lock) {
            NativeBridge.nativeStop()
            started = false
        }
    }
}
