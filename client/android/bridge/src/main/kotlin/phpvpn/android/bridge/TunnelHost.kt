package phpvpn.android.bridge

/**
 * Service host of the native client (plan-android-v001 point 18), implemented
 * by the live VpnService: registered with [VpnHosts.registerTunnelHost] in
 * `onCreate`, removed with [VpnHosts.unregisterTunnelHost] in `onDestroy`.
 *
 * Called from native threads (the engine network thread and the teardown),
 * one call at a time; an implementation must not call back into the native
 * library and must not wait for the main thread. [VpnInterfaces] holds the
 * shared Builder logic, so an implementation stays a thin adapter.
 */
interface TunnelHost {
    /**
     * `Builder.establish()` for the VpnSpec JSON (vpn_context.h) and
     * `ParcelFileDescriptor.detachFd()`. Returns the descriptor, which then
     * belongs to the native driver (Kotlin never closes it), or -1; then
     * [lastEstablishError] tells why.
     */
    fun establish(specJson: ByteArray): Int

    /** Reason of the last failed [establish], one of the `ESTABLISH_*` values. */
    fun lastEstablishError(): Int

    /** Waits at most [timeoutMs] until no VPN network of this app exists. */
    fun waitVpnNetworkGone(timeoutMs: Int): Boolean

    companion object {
        const val ESTABLISH_OK = 0

        /** The consent is missing or was revoked: tunnel_permission_missing. */
        const val ESTABLISH_NOT_PREPARED = 1

        /** The system refused a parameter: tunnel_adapter_failed. */
        const val ESTABLISH_REJECTED = 2

        /** Any other failure: tunnel_adapter_failed. */
        const val ESTABLISH_FAILED = 3
    }
}
