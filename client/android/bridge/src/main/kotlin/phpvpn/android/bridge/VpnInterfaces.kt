package phpvpn.android.bridge

import android.content.Context
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.VpnService
import android.os.Process
import android.os.SystemClock
import android.system.OsConstants

/**
 * The VpnService.Builder side of [TunnelHost] (plan-android-v001, "Životní
 * cyklus připojení", step 2), shared by every VpnService that hosts the
 * native tunnel so the interface never depends on which service built it.
 */
object VpnInterfaces {
    /** Result of [establish]: a detached descriptor, or -1 and the reason. */
    data class Established(val descriptor: Int, val error: Int)

    private const val POLL_MS = 20L

    /**
     * Builds the interface of [specJson] from [service] and detaches its
     * descriptor. [configure] adds what only the service knows, such as the
     * configure intent of the notification (WP05).
     */
    fun establish(
        service: VpnService,
        specJson: ByteArray,
        configure: (VpnService.Builder) -> Unit = {},
    ): Established {
        val spec = try {
            VpnSpec.parse(specJson)
        } catch (error: IllegalArgumentException) {
            return Established(-1, TunnelHost.ESTABLISH_FAILED)
        } catch (error: IllegalStateException) {
            return Established(-1, TunnelHost.ESTABLISH_FAILED)
        }
        if (VpnService.prepare(service) != null) return Established(-1, TunnelHost.ESTABLISH_NOT_PREPARED)
        return try {
            val builder = service.Builder()
                .setSession(spec.session)
                .addAddress(spec.address, spec.prefixLength)
                .addRoute(spec.route, spec.routePrefixLength)
                .setMtu(spec.mtu)
                // The native FdTunnel sets O_NONBLOCK itself; null tracks the
                // system default network as the underlying network.
                .setBlocking(false)
                .setUnderlyingNetworks(null)
                .setMetered(spec.metered)
            // Without it Android blocks the IPv6 traffic of every covered app,
            // because the VPN has no IPv6 address or route.
            if (spec.allowIpv6) builder.allowFamily(OsConstants.AF_INET6)
            spec.dnsServers.forEach { builder.addDnsServer(it) }
            spec.disallowedApplications.forEach { builder.addDisallowedApplication(it) }
            configure(builder)
            // null: the app is not (or no longer) prepared.
            val descriptor = builder.establish() ?: return Established(-1, TunnelHost.ESTABLISH_NOT_PREPARED)
            Established(descriptor.detachFd(), TunnelHost.ESTABLISH_OK)
        } catch (error: PackageManager.NameNotFoundException) {
            Established(-1, TunnelHost.ESTABLISH_REJECTED)
        } catch (error: IllegalArgumentException) {
            Established(-1, TunnelHost.ESTABLISH_REJECTED)
        } catch (error: IllegalStateException) {
            Established(-1, TunnelHost.ESTABLISH_REJECTED)
        } catch (error: RuntimeException) {
            Established(-1, TunnelHost.ESTABLISH_FAILED)
        }
    }

    /** The VPN networks of this app (the owner uid is visible to the owner only). */
    fun ownVpnNetworks(context: Context): List<Network> {
        val connectivity = context.getSystemService(ConnectivityManager::class.java) ?: return emptyList()
        @Suppress("DEPRECATION") // getAllNetworks: still the only list without a callback
        val all = connectivity.allNetworks
        return all.filter { network ->
            val capabilities = connectivity.getNetworkCapabilities(network)
            capabilities != null && capabilities.hasTransport(NetworkCapabilities.TRANSPORT_VPN) &&
                (capabilities.ownerUid == Process.myUid() || capabilities.ownerUid == Process.INVALID_UID)
        }
    }

    /**
     * Polls until no VPN network of this app exists, at most [timeoutMs]. Runs
     * on the calling thread, never on the main looper, because the native
     * teardown calls it while it waits.
     */
    fun waitVpnNetworkGone(context: Context, timeoutMs: Int): Boolean {
        val deadline = SystemClock.elapsedRealtime() + timeoutMs.coerceAtLeast(0)
        while (true) {
            if (ownVpnNetworks(context).isEmpty()) return true
            if (SystemClock.elapsedRealtime() >= deadline) return false
            try {
                Thread.sleep(POLL_MS)
            } catch (interrupted: InterruptedException) {
                Thread.currentThread().interrupt()
                return ownVpnNetworks(context).isEmpty()
            }
        }
    }
}
