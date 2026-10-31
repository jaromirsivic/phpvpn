package phpvpn.android.bridge

import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.addJsonObject
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put
import kotlinx.serialization.json.putJsonArray

/** One address row: a LinkAddress of the link [interfaceName]. */
data class SnapshotAddress(
    val interfaceName: String,
    val ifindex: Int,
    val address: String,
    val prefixLength: Int,
)

/** One RouteInfo of a network. */
data class SnapshotRoute(
    val interfaceName: String?,
    val ifindex: Int,
    val destination: String,
    val prefixLength: Int,
    val gateway: String?,
    val type: String,
    val isDefault: Boolean,
)

/** One network of ConnectivityManager with its LinkProperties. */
data class SnapshotNetwork(
    val handle: Long,
    val interfaceName: String?,
    val vpn: Boolean,
    val transports: List<String>,
    val addresses: List<SnapshotAddress>,
    val routes: List<SnapshotRoute>,
    val dnsServers: List<String>,
)

data class NetworkSnapshotData(val activeNetwork: Long?, val networks: List<SnapshotNetwork>)

/**
 * Writer of the snapshot JSON, schema version 1 of
 * client/src/platform/android/netconfig_android.h. Plain Kotlin, so the JVM
 * unit test pins the exact text the native parser reads.
 */
object NetworkSnapshotJson {
    const val VERSION = 1

    fun encode(snapshot: NetworkSnapshotData): String = buildJsonObject {
        put("version", VERSION)
        put("activeNetwork", snapshot.activeNetwork?.let { JsonPrimitive(it) } ?: JsonNull)
        putJsonArray("networks") {
            snapshot.networks.forEach { network ->
                addJsonObject {
                    put("handle", network.handle)
                    put("interfaceName", network.interfaceName)
                    put("vpn", network.vpn)
                    putJsonArray("transports") { network.transports.forEach { add(JsonPrimitive(it)) } }
                    putJsonArray("addresses") {
                        network.addresses.forEach { row ->
                            addJsonObject {
                                put("interfaceName", row.interfaceName)
                                put("ifindex", row.ifindex)
                                put("address", row.address)
                                put("prefixLength", row.prefixLength)
                            }
                        }
                    }
                    putJsonArray("routes") {
                        network.routes.forEach { row ->
                            addJsonObject {
                                put("interfaceName", row.interfaceName)
                                put("ifindex", row.ifindex)
                                put("destination", row.destination)
                                put("prefixLength", row.prefixLength)
                                put("gateway", row.gateway)
                                put("type", row.type)
                                put("isDefault", row.isDefault)
                            }
                        }
                    }
                    putJsonArray("dnsServers") { network.dnsServers.forEach { add(JsonPrimitive(it)) } }
                }
            }
        }
    }.toString()
}
