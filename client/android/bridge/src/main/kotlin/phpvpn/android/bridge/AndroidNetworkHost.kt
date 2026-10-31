package phpvpn.android.bridge

import android.content.Context
import android.net.ConnectivityManager
import android.net.LinkProperties
import android.net.Network
import android.net.NetworkCapabilities
import android.net.RouteInfo
import android.net.VpnService
import java.net.NetworkInterface
import java.net.SocketException

/**
 * [NetworkHost] over ConnectivityManager (plan-android-v001 points 4 and 18).
 *
 * Apps targeting API 30+ may not dump links or routes over netlink, so the
 * snapshot comes from the LinkProperties of every network. VPN networks are
 * reported with `vpn: true` and left out by the native side; the active
 * network is `getActiveNetwork()` of this app, which the VPN never covers
 * (the native spec disallows the own package), so it stays the underlying
 * network while the tunnel is up. Needs ACCESS_NETWORK_STATE.
 */
class AndroidNetworkHost(context: Context) : NetworkHost {
    private val appContext: Context = context.applicationContext
    private val connectivity: ConnectivityManager =
        requireNotNull(appContext.getSystemService(ConnectivityManager::class.java)) { "no ConnectivityManager" }

    override fun snapshotNetworks(): ByteArray = NetworkSnapshotJson.encode(collect()).toByteArray(Charsets.UTF_8)

    override fun isConsentGranted(): Boolean = VpnService.prepare(appContext) == null

    /** The networks as the native snapshot sees them. */
    fun collect(): NetworkSnapshotData {
        val indexes = HashMap<String, Int>()
        @Suppress("DEPRECATION") // getAllNetworks: still the only list without a callback
        val all = connectivity.allNetworks
        val networks = all.mapNotNull { describe(it, indexes) }
        return NetworkSnapshotData(connectivity.activeNetwork?.networkHandle, networks)
    }

    private fun describe(network: Network, indexes: MutableMap<String, Int>): SnapshotNetwork? {
        // A network that vanished between the list and this call is skipped.
        val properties: LinkProperties = connectivity.getLinkProperties(network) ?: return null
        val capabilities = connectivity.getNetworkCapabilities(network)
        val link = properties.interfaceName
        val index = { name: String? -> if (name == null) 0 else indexes.getOrPut(name) { ifindex(name) } }
        return SnapshotNetwork(
            handle = network.networkHandle,
            interfaceName = link,
            vpn = capabilities?.hasTransport(NetworkCapabilities.TRANSPORT_VPN) ?: false,
            transports = transports(capabilities),
            addresses = properties.linkAddresses.mapNotNull { address ->
                val text = address.address.hostAddress ?: return@mapNotNull null
                SnapshotAddress(link ?: "", index(link), text, address.prefixLength)
            },
            routes = properties.routes.mapNotNull { route ->
                val destination = route.destination
                val text = destination.address.hostAddress ?: return@mapNotNull null
                val name = route.`interface` ?: link
                SnapshotRoute(
                    interfaceName = name,
                    ifindex = index(name),
                    destination = text,
                    prefixLength = destination.prefixLength,
                    gateway = if (route.hasGateway()) route.gateway?.hostAddress else null,
                    type = routeType(route.type),
                    isDefault = route.isDefaultRoute,
                )
            },
            dnsServers = properties.dnsServers.mapNotNull { it.hostAddress },
        )
    }

    private companion object {
        // NetworkInterface reads the index through getifaddrs, which works for
        // apps targeting API 30+ for every interface with an address. 0 means
        // unknown; the native side never lets it become identity 0.
        fun ifindex(name: String): Int = try {
            NetworkInterface.getByName(name)?.index?.takeIf { it > 0 } ?: 0
        } catch (error: SocketException) {
            0
        }

        fun routeType(type: Int): String = when (type) {
            RouteInfo.RTN_UNICAST -> "unicast"
            RouteInfo.RTN_UNREACHABLE -> "unreachable"
            RouteInfo.RTN_THROW -> "throw"
            else -> "other"
        }

        fun transports(capabilities: NetworkCapabilities?): List<String> {
            if (capabilities == null) return emptyList()
            val known = listOf(
                NetworkCapabilities.TRANSPORT_WIFI to "wifi",
                NetworkCapabilities.TRANSPORT_CELLULAR to "cellular",
                NetworkCapabilities.TRANSPORT_ETHERNET to "ethernet",
                NetworkCapabilities.TRANSPORT_VPN to "vpn",
                NetworkCapabilities.TRANSPORT_BLUETOOTH to "bluetooth",
            )
            // Informative only; the native side reads the "vpn" flag.
            return known.filter { capabilities.hasTransport(it.first) }.map { it.second }.ifEmpty { listOf("other") }
        }
    }
}
