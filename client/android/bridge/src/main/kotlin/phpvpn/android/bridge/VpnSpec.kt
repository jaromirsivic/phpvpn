package phpvpn.android.bridge

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.boolean
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/**
 * What the native driver asks [TunnelHost.establish] to build, the JSON of
 * `serializeVpnSpec` in client/src/platform/android/vpn_context.h, version 1:
 * only the address of the VPN prefix and the single route `vpnCidr`, no
 * default route and no DNS (plan-android-v001, "Chování v síti").
 */
data class VpnSpec(
    val session: String,
    val address: String,
    val prefixLength: Int,
    val route: String,
    val routePrefixLength: Int,
    val mtu: Int,
    val disallowedApplications: List<String>,
    val allowIpv6: Boolean,
    val metered: Boolean,
    val dnsServers: List<String>,
) {
    companion object {
        private val MEMBERS = setOf(
            "version", "session", "address", "prefixLength", "route", "routePrefixLength", "mtu",
            "disallowedApplications", "allowIpv6", "metered", "dnsServers",
        )

        /** Strict: every member, nothing else; IllegalArgumentException otherwise. */
        fun parse(json: ByteArray): VpnSpec {
            val root = try {
                Json.parseToJsonElement(json.toString(Charsets.UTF_8)).jsonObject
            } catch (error: IllegalArgumentException) {
                throw IllegalArgumentException("the VPN specification is not a JSON object", error)
            }
            require(root.keys == MEMBERS) { "the VPN specification has unexpected members" }
            require(root.integer("version") == 1) { "the VPN specification version is not supported" }
            return VpnSpec(
                session = root.text("session"),
                address = root.text("address"),
                prefixLength = root.integer("prefixLength"),
                route = root.text("route"),
                routePrefixLength = root.integer("routePrefixLength"),
                mtu = root.integer("mtu"),
                disallowedApplications = root.texts("disallowedApplications"),
                allowIpv6 = root.flag("allowIpv6"),
                metered = root.flag("metered"),
                dnsServers = root.texts("dnsServers"),
            )
        }

        private fun JsonObject.primitive(name: String): JsonPrimitive =
            (getValue(name) as? JsonPrimitive) ?: throw IllegalArgumentException("$name is not a value")

        private fun JsonObject.text(name: String): String {
            val value = primitive(name)
            require(value.isString) { "$name is not a string" }
            return value.content
        }

        private fun JsonObject.integer(name: String): Int {
            val value = primitive(name)
            require(!value.isString) { "$name is not a number" }
            return value.int
        }

        private fun JsonObject.flag(name: String): Boolean {
            val value = primitive(name)
            require(!value.isString) { "$name is not a boolean" }
            return value.boolean
        }

        private fun JsonObject.texts(name: String): List<String> =
            getValue(name).jsonArray.map { element: JsonElement ->
                val value = element.jsonPrimitive
                require(value.isString) { "$name holds a non-string" }
                value.content
            }
    }
}
