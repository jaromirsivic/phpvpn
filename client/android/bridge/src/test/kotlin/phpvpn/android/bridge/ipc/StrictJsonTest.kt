package phpvpn.android.bridge.ipc

import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** The strict reader follows core/protocol/json.cpp, which kotlinx alone does not. */
class StrictJsonTest {
    @Test
    fun rejectsDuplicateMembersAtEveryLevel() {
        assertNull(StrictJson.parse("""{"a":1,"a":1}"""))
        assertNull(StrictJson.parse("""{"a":{"b":1,"b":2}}"""))
        assertNotNull(StrictJson.parse("""{"a":{"b":1},"b":{"b":1}}"""))
    }

    @Test
    fun rejectsTrailingBytesControlCharactersAndBadEscapes() {
        for (text in listOf("{} x", "{\"a\":\"\u0001\"}", "{\"a\":\"\\x\"}", "{\"a\":\"\\u12\"}", "[1,]", "{\"a\":1,}", "01", "-", "1.", "1e", "+1", "NaN", "'a'")) {
            assertNull(text, StrictJson.parse(text))
        }
        assertNotNull(StrictJson.parse(" \t\r\n{} \n"))
    }

    @Test
    fun surrogatesMustBePaired() {
        assertNull(StrictJson.parse("\"\\ud800\""))
        assertNull(StrictJson.parse("\"\\udc00\""))
        assertNull(StrictJson.parse("\"\\ud800\\u0041\""))
        assertEquals("\uD83D\uDE00", (StrictJson.parse("\"\\ud83d\\ude00\"") as JsonPrimitive).content)
    }

    @Test
    fun rejectsInvalidUtf8() {
        for (bytes in listOf(byteArrayOf(0x22, 0xC3.toByte(), 0x22), byteArrayOf(0x22, 0xED.toByte(), 0xA0.toByte(), 0x80.toByte(), 0x22),
            byteArrayOf(0x22, 0xC0.toByte(), 0xAF.toByte(), 0x22), byteArrayOf(0x22, 0xF5.toByte(), 0x80.toByte(), 0x80.toByte(), 0x80.toByte(), 0x22))) {
            assertNull(StrictJson.parse(bytes))
        }
        assertEquals("\u017e", (StrictJson.parse("\"\u017e\"".toByteArray()) as JsonPrimitive).content)
    }

    @Test
    fun depthIsLimitedLikeTheNativeParser() {
        assertNotNull(StrictJson.parse("[".repeat(33) + "]".repeat(33)))
        assertNull(StrictJson.parse("[".repeat(34) + "]".repeat(34)))
    }

    @Test
    fun numbersKeepTheirExactLiteral() {
        val root = StrictJson.parse("""{"a":18446744073709551615,"b":1.0,"c":-0,"d":18446744073709551616,"e":1e3}""") as JsonObject
        assertEquals(ULong.MAX_VALUE, JsonChecks.uint64(root["a"]))
        assertNull(JsonChecks.uint64(root["b"]))
        assertEquals(0uL, JsonChecks.uint64(root["c"]))
        assertNull(JsonChecks.uint64(root["d"]))
        assertNull(JsonChecks.uint64(root["e"]))
        assertNull(JsonChecks.uint64(root["c"], positive = true))
        assertNull(JsonChecks.int64(root["a"]))
        assertEquals(Long.MIN_VALUE, JsonChecks.int64(StrictJson.parse("-9223372036854775808")))
        assertTrue(JsonChecks.string(JsonPrimitive("1")) == "1" && JsonChecks.uint64(JsonPrimitive("1")) == null)
    }

    @Test
    fun wireFormatsMatchTheNativeSpelling() {
        assertTrue(WireFormats.isUuidV4("3f2504e0-4f89-41d3-9a0c-0305e82c3301"))
        for (bad in listOf("", "not-a-uuid", "3f2504e0-4f89-11d3-9a0c-0305e82c3301", "3F2504E0-4F89-41D3-9A0C-0305E82C3301", "3f2504e0-4f89-41d3-ca0c-0305e82c3301")) {
            assertTrue(bad, !WireFormats.isUuidV4(bad))
        }
        assertEquals(0x0A636302L, WireFormats.parseIpv4("10.99.99.2"))
        for (bad in listOf("10.99.99", "10.99.99.256", "10.099.99.2", "10.99.99.2.1", "a.b.c.d", " 10.99.99.2")) assertNull(bad, WireFormats.parseIpv4(bad))
        assertEquals(java.time.Instant.parse("2025-09-04T19:00:00.123Z").toEpochMilli(), WireFormats.parseIso8601("2025-09-04T19:00:00.123Z"))
        assertEquals(java.time.Instant.parse("1969-12-31T23:59:59.999Z").toEpochMilli(), WireFormats.parseIso8601("1969-12-31T23:59:59.999Z"))
        for (bad in listOf("2026-02-29T00:00:00.000Z", "2026-09-04T24:00:00.000Z", "2026-09-04T18:52:60.000Z", "2026-09-04T18:52:31Z", "2026-09-04 18:52:31.412Z")) {
            assertNull(bad, WireFormats.parseIso8601(bad))
        }
        assertNotNull(WireFormats.parseIso8601("2024-02-29T23:59:59.999Z"))
        assertEquals(4L, WireFormats.utf8Length("\uD83D\uDE00"))
        assertEquals(2L, WireFormats.utf8Length("\u017e"))
    }

    @Test
    fun httpHeaderGrammarMatchesNormalizeHttpHeaders() {
        assertEquals("Cookie: synthetic=1\r\nUser-Agent: test", HttpHeaders.normalize("Cookie: synthetic=1\nUser-Agent: test\n"))
        assertEquals(HttpHeaders.DEFAULT, HttpHeaders.normalize(HttpHeaders.DEFAULT))
        for (bad in listOf("", "Host: secret", "X: a\n\nY: b", "Cookie: a\ncOoKiE: b", "X: a\rY: b", "Bad Name: v", "X: \u007f", "NoColon", "content-length: 1")) {
            assertNull(bad, HttpHeaders.normalize(bad))
        }
        assertNotNull(HttpHeaders.normalize("X: \ttab and \u017e"))
    }
}
