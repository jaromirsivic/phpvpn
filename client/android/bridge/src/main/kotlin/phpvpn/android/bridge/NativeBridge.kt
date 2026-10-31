package phpvpn.android.bridge

/**
 * JNI boundary of libphpvpn-android.so (plan-android-v001, WP00 and WP04).
 *
 * Every native method is registered by the library in `JNI_OnLoad`. Strings
 * cross the boundary as UTF-8 byte arrays, never as `String`, because JNI
 * strings use modified UTF-8 and would corrupt configuration text.
 *
 * Threads: [nativeStart], [nativeStop] and [nativeStatus] are serialized by
 * one native lifecycle lock. [nativeDispatch] blocks until the reply or the
 * caller's timeout; call it from any thread except the main thread
 * ([phpvpn.android.bridge.jni.JniIpcTransport] runs it on a limited
 * `Dispatchers.IO` dispatcher). The native code does not check the thread.
 */
object NativeBridge {
    init {
        System.loadLibrary("phpvpn-android")
    }

    /** [nativeStart]: the runtime runs. */
    const val START_OK = 0

    /** [nativeStart]: the runtime already ran; nothing changed. */
    const val START_ALREADY_STARTED = 1

    /** [nativeStart]: the directory is not an absolute normal path or the package name is invalid. */
    const val START_INVALID_ARGUMENT = 2

    /** [nativeStart]: `ServiceRuntime::start` failed (logged by code); nothing is left running. */
    const val START_FAILED = 3

    /** Correlation id of the [nativeStatus] reply, a fixed UUIDv4. */
    const val STATUS_REQUEST_ID = "00000000-0000-4000-8000-0000000005a7"

    /**
     * Version of the native client, `PHPVPN_CLIENT_VERSION` from
     * client/CMakeLists.txt, as UTF-8 bytes.
     */
    @JvmStatic
    external fun nativeVersion(): ByteArray

    /**
     * Creates the `AndroidVpnContext` (with [packageName] as the application
     * excluded from the VPN), gives it the hosts registered through
     * [VpnHosts] and starts the shared `ServiceRuntime` with its storage in
     * `<noBackupFilesDir>/phpvpn`. Never arms the startup Connect (the boot
     * receiver of WP05 decides). Both arguments are UTF-8. Returns START_*.
     * Use [NativeRuntime.ensureStarted] instead of calling it directly.
     */
    @JvmStatic
    external fun nativeStart(noBackupFilesDir: ByteArray, packageName: ByteArray): Int

    /** Ordered teardown of the runtime and the context; idempotent. */
    @JvmStatic
    external fun nativeStop()

    /**
     * One IPC v1 exchange with the in-process server: [request] is the
     * complete UTF-8 JSON request (at most 4 MiB, checked before anything is
     * copied), the result the exact UTF-8 reply. Null when [timeoutMs] passed,
     * when the runtime is not running and for a request the desktop service
     * would answer by closing the connection (oversized, unclassifiable).
     * Blocks the calling thread; never call it on the main thread.
     */
    @JvmStatic
    external fun nativeDispatch(request: ByteArray, timeoutMs: Int): ByteArray?

    /**
     * The engine status as the `get-status` reply correlated with
     * [STATUS_REQUEST_ID] (for the service notification of WP05), or null
     * while the runtime is not running.
     */
    @JvmStatic
    external fun nativeStatus(): ByteArray?

    fun version(): String = nativeVersion().toString(Charsets.UTF_8)
}
