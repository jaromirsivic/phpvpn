package phpvpn.android.bridge

/**
 * Registration of the two VPN hosts in libphpvpn-android.so
 * (client/apps/android-jni/jni_vpn_bridge.cpp, plan-android-v001 point 18).
 * A registration reaches the running native runtime at once and is kept for a
 * runtime started later. Registering again replaces the previous host.
 */
object VpnHosts {
    init {
        // JNI_OnLoad registers the natives; loading twice is a no-op.
        System.loadLibrary("phpvpn-android")
    }

    /** Once per process, before the native runtime starts (WP04). */
    @JvmStatic
    external fun registerNetworkHost(host: NetworkHost)

    /** From `VpnService.onCreate` (WP05). */
    @JvmStatic
    external fun registerTunnelHost(host: TunnelHost)

    /** From `VpnService.onDestroy` (WP05). */
    @JvmStatic
    external fun unregisterTunnelHost()
}
