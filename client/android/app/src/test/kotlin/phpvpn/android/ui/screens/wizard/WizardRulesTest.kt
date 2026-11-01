package phpvpn.android.ui.screens.wizard

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.ipc.HttpHeaders
import phpvpn.android.bridge.ipc.WireFormats

/** The pure rules shared with core/wizard/wizard_defaults.cpp, config_parser.cpp and http_headers.cpp. */
class WizardRulesTest {
    @Test
    fun defaultAccessKeyIsTheLastOctetRepeated() {
        assertTrue(WizardRules.defaultAccessKey("10.99.99.17").contentEquals(phpvpn.android.bridge.ipc.SecretText.of("11".repeat(32))))
        assertTrue(WizardRules.defaultAccessKey("10.99.99.2").contentEquals(phpvpn.android.bridge.ipc.SecretText.of("02".repeat(32))))
        assertTrue(WizardRules.defaultAccessKey("10.99.99.255").contentEquals(phpvpn.android.bridge.ipc.SecretText.of("ff".repeat(32))))
        assertTrue(WizardRules.defaultAccessKey("10.99.99.017").isEmpty())
        assertTrue(WizardRules.defaultAccessKey("").isEmpty())
    }

    @Test
    fun clientAddressesExcludeNetworkFirstHostAndBroadcast() {
        val cidr = "10.99.99.0/24"
        assertFalse(WizardRules.isValidClientAddress("10.99.99.0", cidr))
        assertFalse(WizardRules.isValidClientAddress("10.99.99.1", cidr))
        assertTrue(WizardRules.isValidClientAddress("10.99.99.2", cidr))
        assertTrue(WizardRules.isValidClientAddress("10.99.99.254", cidr))
        assertFalse(WizardRules.isValidClientAddress("10.99.99.255", cidr))
        assertFalse(WizardRules.isValidClientAddress("10.99.98.20", cidr))
        assertTrue(WizardRules.isValidClientAddress("10.20.30.6", "10.20.30.0/29"))
        assertFalse(WizardRules.isValidClientAddress("10.20.30.7", "10.20.30.0/29"))
        assertFalse(WizardRules.isValidClientAddress("10.20.30.6", null))
    }

    @Test
    fun defaultContractCandidatesAndMissingPeers() {
        assertEquals(14, WizardRules.defaultContractCandidates("10.99.99.0/24").size)
        assertTrue(WizardRules.defaultContractCandidates("192.168.1.0/24").isEmpty())
        assertTrue(WizardRules.defaultContractCandidates("10.99.99.0/25").isEmpty())
        assertEquals(13, WizardRules.missingDefaultContractPeers("10.99.99.0/24", "10.99.99.10", emptyList()).size)
        assertEquals(14, WizardRules.missingDefaultContractPeers("10.99.99.0/24", "10.99.99.20", emptyList()).size)
        val missing = WizardRules.missingDefaultContractPeers("10.99.99.0/24", "10.99.99.2", listOf("10.99.99.3", "10.99.99.15"))
        assertEquals(11, missing.size)
        assertEquals("10.99.99.4", WireFormats.formatIpv4(missing.first()))
        assertEquals("10.99.99.14", WireFormats.formatIpv4(missing.last()))
    }

    @Test
    fun serverUrlRulesOfParseServerUrl() {
        assertEquals(WizardRules.UrlCheck.VALID, WizardRules.checkServerUrl("https://vpn.example.com/phpvpn.php"))
        assertEquals(WizardRules.UrlCheck.VALID, WizardRules.checkServerUrl("http://127.0.0.1:8080/relay.php"))
        assertEquals(WizardRules.UrlCheck.VALID, WizardRules.checkServerUrl("HTTPS://VPN.EXAMPLE.COM"))
        assertEquals(WizardRules.UrlCheck.PLACEHOLDER, WizardRules.checkServerUrl("http://MODIFY-THIS-URL/phpvpn.php"))
        assertEquals(WizardRules.UrlCheck.PLACEHOLDER, WizardRules.checkServerUrl("https://modify-this-url:443/x.php"))
        for (bad in listOf(
            "", "vpn.example.com", "ftp://h/", "https://u:p@h/", "https://h/?q", "https://h/#f", "https://h:/",
            "https://h:65536/", "https://h:12a/", "https://-h/", "https://h../", "https://[::1]/", "https://h/a b",
            "https://h/\u00e9",
        )) {
            assertEquals(bad, WizardRules.UrlCheck.INVALID, WizardRules.checkServerUrl(bad))
        }
        assertEquals("vpn.example.com", WizardRules.serverUrlHost("https://VPN.example.com:8443/phpvpn.php"))
        assertNull(WizardRules.serverUrlHost("nonsense"))
    }

    @Test
    fun secretLiteralEscapesAndParsesStrictly() {
        val raw = "a\"\\\b\u000C\n\r\t\u0001\u007F".toCharArray()
        assertTrue(SecretLiteral.needed(raw))
        assertFalse(SecretLiteral.needed(" a b ".toCharArray()))
        val escaped = SecretLiteral.escape(raw)
        assertEquals("\"a\\\"\\\\\\b\\f\\n\\r\\t\\u0001\u007F\"", String(escaped))
        assertArrayEquals(raw, SecretLiteral.parse(escaped))
        assertArrayEquals("x".toCharArray(), SecretLiteral.parse("  \"x\"\n".toCharArray()))
        assertArrayEquals("/\u00e9".toCharArray(), SecretLiteral.parse("\"\\/\\u00e9\"".toCharArray()))
        for (bad in listOf("", "\"", "x", "\"x", "\"a\"b\"", "\"\\x\"", "\"\\u12\"", "\"\u0001\"", "\"\\ud800\"", "\"a\" \"b\"")) {
            assertNull(bad, SecretLiteral.parse(bad.toCharArray()))
        }
    }

    /** The header validator gives the same canonical block as the bridge port of normalizeHttpHeaders. */
    @Test
    fun headerRulesMatchTheSharedNormalisation() {
        val inputs = listOf(
            HttpHeaders.DEFAULT,
            "Cookie: a=1\nUser-Agent: x\n",
            "X-A: 1\r\nX-B:\t2",
            "Only: one\r\n",
            "Bad Name: x",
            "X: a\rb",
            "X: \u0001",
            "Transfer-encoding: chunked",
            "connection: close",
            "A: 1\na: 2",
            ":nameless",
            "\n",
        )
        for (input in inputs) {
            val check = HeaderRules.validate(input)
            val shared = HttpHeaders.normalize(input)
            if (shared == null) {
                assertTrue(input, check is HeaderCheck.Invalid)
            } else {
                assertEquals(input, HeaderCheck.Valid(shared), check)
            }
        }
        assertEquals(HeaderCheck.Invalid(HeaderProblem.INVALID_NAME, 1), HeaderRules.validate("Bad Name: x"))
        assertEquals(HeaderCheck.Invalid(HeaderProblem.BARE_CR, 1), HeaderRules.validate("X: a\rb"))
        assertEquals(HeaderCheck.Invalid(HeaderProblem.CONTROL_OCTET, 1), HeaderRules.validate("X: \u0001"))
        assertEquals(HeaderCheck.Invalid(HeaderProblem.MANAGED, 1, "Transfer-encoding"), HeaderRules.validate("Transfer-encoding: chunked"))
        assertEquals(HeaderCheck.Invalid(HeaderProblem.ENCODING, 1), HeaderRules.validate("X: \ud800"))
        // The escaped block must fit the 512 KiB document.
        val huge = "X: " + "a".repeat(WizardRules.MAX_CONFIG_FILE_BYTES.toInt())
        assertEquals(HeaderCheck.Invalid(HeaderProblem.TOO_LARGE, 1), HeaderRules.validate(huge))
        assertEquals("A: 1\nB: 2", HeaderRules.displayText("A: 1\r\nB: 2"))
    }
}
