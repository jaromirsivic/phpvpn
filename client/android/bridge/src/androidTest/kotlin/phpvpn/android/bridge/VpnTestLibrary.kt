package phpvpn.android.bridge

/**
 * Native half of the VpnService tests in libphpvpn-android-tests.so
 * (client/apps/android-jni/vpn_tests_jni.cpp). Results come back as UTF-8
 * text starting with "ok" or "error <code> (<detail>)".
 */
object VpnTestLibrary {
    init {
        System.loadLibrary("phpvpn-android-tests")
    }

    /** T-A01: TunnelSession over the production platform objects, 10.99.99.2/24. */
    @JvmStatic
    external fun vpnStart(networkHost: NetworkHost, tunnelHost: TunnelHost, packageName: ByteArray, directory: ByteArray): ByteArray

    /** Starts answering ICMP echo requests to the VPN prefix and counting packets. */
    @JvmStatic
    external fun vpnResponderStart(): ByteArray

    /** Stops the responder and returns its counters. */
    @JvmStatic
    external fun vpnResponderStop(): ByteArray

    /** TunnelSession.stop() and the release of the context; "... stopMs=<n>". */
    @JvmStatic
    external fun vpnStop(): ByteArray

    /** recoverPreviousRun over a stale journal with the real snapshot. */
    @JvmStatic
    external fun vpnRecoveryCheck(networkHost: NetworkHost, packageName: ByteArray, directory: ByteArray): ByteArray

    /** loadAndVerify with the real NetworkHost and no TunnelHost. */
    @JvmStatic
    external fun vpnLoadCheck(networkHost: NetworkHost, packageName: ByteArray): ByteArray

    /** One POST of the production curl transport to [url]. */
    @JvmStatic
    external fun tlsRequest(url: ByteArray, trustAny: Boolean): ByteArray

    /** The process-wide system CA store. */
    @JvmStatic
    external fun tlsStoreInfo(): ByteArray

    fun text(bytes: ByteArray): String = bytes.toString(Charsets.UTF_8)
}
