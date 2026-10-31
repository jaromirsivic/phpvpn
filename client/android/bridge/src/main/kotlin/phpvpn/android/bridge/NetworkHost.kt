package phpvpn.android.bridge

/**
 * Process-wide network host of the native client (plan-android-v001 point 18).
 *
 * Implemented over the application context ([AndroidNetworkHost]) and
 * registered once per process with [VpnHosts.registerNetworkHost], before the
 * native runtime starts, because the startup recovery needs the snapshot
 * without any VpnService. Called from native threads (the engine network
 * thread and the teardown), never from the main thread; an implementation must
 * not call back into the native library and must not depend on the main looper.
 */
interface NetworkHost {
    /**
     * UTF-8 JSON of every network, schema version 1 of
     * client/src/platform/android/netconfig_android.h (at most 1 MiB).
     */
    fun snapshotNetworks(): ByteArray

    /** `VpnService.prepare(applicationContext) == null`. */
    fun isConsentGranted(): Boolean
}
