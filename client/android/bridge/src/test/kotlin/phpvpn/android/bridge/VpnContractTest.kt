package phpvpn.android.bridge

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

/**
 * The two JSON contracts between Kotlin and the native platform layer
 * (plan-android-v001 WP03). The literals are the same as in
 * client/tests/android/test_netconfig_android.cpp
 * (ParsesTheTextOfTheKotlinEncoder) and test_vpn_context.cpp
 * (SpecSerializesTheBuilderContract), so both sides pin one text.
 */
class VpnContractTest {
    @Test
    fun snapshotEncoderWritesTheNativeSchema() {
        val wifi = SnapshotNetwork(
            handle = 434026158090L,
            interfaceName = "wlan0",
            vpn = false,
            transports = listOf("wifi"),
            addresses = listOf(
                SnapshotAddress("wlan0", 16, "10.0.2.16", 24),
                SnapshotAddress("wlan0", 16, "fec0::d980:4e45:9fac:c5a9", 64),
            ),
            routes = listOf(
                SnapshotRoute("wlan0", 16, "0.0.0.0", 0, "10.0.2.2", "unicast", true),
                SnapshotRoute("wlan0", 16, "10.0.2.0", 24, null, "unicast", false),
                SnapshotRoute("wlan0", 16, "::", 0, "fe80::2", "unicast", true),
                SnapshotRoute("wlan0", 16, "fec0::", 64, null, "unicast", false),
            ),
            dnsServers = listOf("10.0.2.3", "fec0::3"),
        )
        val vpn = SnapshotNetwork(
            handle = 438321125386L,
            interfaceName = "tun0",
            vpn = true,
            transports = listOf("vpn"),
            addresses = listOf(SnapshotAddress("tun0", 30, "10.99.99.2", 24)),
            routes = listOf(SnapshotRoute(null, 0, "10.99.99.0", 24, null, "unicast", false)),
            dnsServers = emptyList(),
        )
        assertEquals(EMULATOR_SNAPSHOT, NetworkSnapshotJson.encode(NetworkSnapshotData(434026158090L, listOf(wifi, vpn))))
        assertEquals(
            """{"version":1,"activeNetwork":null,"networks":[]}""",
            NetworkSnapshotJson.encode(NetworkSnapshotData(null, emptyList())),
        )
    }

    @Test
    fun vpnSpecParsesTheNativeSerialization() {
        val spec = VpnSpec.parse(NATIVE_SPEC.toByteArray())
        assertEquals(
            VpnSpec(
                session = "phpvpn",
                address = "10.99.99.2",
                prefixLength = 24,
                route = "10.99.99.0",
                routePrefixLength = 24,
                mtu = 1500,
                disallowedApplications = listOf("io.github.jaromirsivic.phpvpn"),
                allowIpv6 = true,
                metered = false,
                dnsServers = emptyList(),
            ),
            spec,
        )
    }

    @Test
    fun vpnSpecIsStrict() {
        val broken = listOf(
            "",
            "[]",
            NATIVE_SPEC.replace("\"version\":1", "\"version\":2"),
            NATIVE_SPEC.replace("\"mtu\":1500", "\"mtu\":\"1500\""),
            NATIVE_SPEC.replace("\"address\":\"10.99.99.2\"", "\"address\":10"),
            NATIVE_SPEC.replace(",\"metered\":false", ""),
            NATIVE_SPEC.replace("}", ",\"extra\":0}"),
            NATIVE_SPEC.replace("\"allowIpv6\":true", "\"allowIpv6\":\"true\""),
            NATIVE_SPEC.replace("\"dnsServers\":[]", "\"dnsServers\":[1]"),
        )
        for (text in broken) {
            assertThrows(text, IllegalArgumentException::class.java) { VpnSpec.parse(text.toByteArray()) }
        }
    }

    private companion object {
        const val EMULATOR_SNAPSHOT =
            """{"version":1,"activeNetwork":434026158090,"networks":[""" +
                """{"handle":434026158090,"interfaceName":"wlan0","vpn":false,"transports":["wifi"],""" +
                """"addresses":[{"interfaceName":"wlan0","ifindex":16,"address":"10.0.2.16","prefixLength":24},""" +
                """{"interfaceName":"wlan0","ifindex":16,"address":"fec0::d980:4e45:9fac:c5a9","prefixLength":64}],""" +
                """"routes":[{"interfaceName":"wlan0","ifindex":16,"destination":"0.0.0.0","prefixLength":0,""" +
                """"gateway":"10.0.2.2","type":"unicast","isDefault":true},""" +
                """{"interfaceName":"wlan0","ifindex":16,"destination":"10.0.2.0","prefixLength":24,""" +
                """"gateway":null,"type":"unicast","isDefault":false},""" +
                """{"interfaceName":"wlan0","ifindex":16,"destination":"::","prefixLength":0,""" +
                """"gateway":"fe80::2","type":"unicast","isDefault":true},""" +
                """{"interfaceName":"wlan0","ifindex":16,"destination":"fec0::","prefixLength":64,""" +
                """"gateway":null,"type":"unicast","isDefault":false}],""" +
                """"dnsServers":["10.0.2.3","fec0::3"]},""" +
                """{"handle":438321125386,"interfaceName":"tun0","vpn":true,"transports":["vpn"],""" +
                """"addresses":[{"interfaceName":"tun0","ifindex":30,"address":"10.99.99.2","prefixLength":24}],""" +
                """"routes":[{"interfaceName":null,"ifindex":0,"destination":"10.99.99.0","prefixLength":24,""" +
                """"gateway":null,"type":"unicast","isDefault":false}],"dnsServers":[]}]}"""

        const val NATIVE_SPEC =
            """{"version":1,"session":"phpvpn","address":"10.99.99.2","prefixLength":24,""" +
                """"route":"10.99.99.0","routePrefixLength":24,"mtu":1500,""" +
                """"disallowedApplications":["io.github.jaromirsivic.phpvpn"],""" +
                """"allowIpv6":true,"metered":false,"dnsServers":[]}"""
    }
}
