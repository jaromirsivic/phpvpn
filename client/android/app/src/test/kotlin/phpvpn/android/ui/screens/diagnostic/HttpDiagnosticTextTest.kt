package phpvpn.android.ui.screens.diagnostic

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.ipc.HttpDiagnosticMessage

/** Port of the display cases of the desktop http-octets-v1 projection (http_diagnostic.cpp). */
class HttpDiagnosticTextTest {
    private fun octets(vararg bytes: Int): String = String(CharArray(bytes.size) { bytes[it].toChar() })

    private fun message(text: String) = HttpDiagnosticMessage(text.length.toULong(), text, "", "")

    @Test
    fun validUtf8AndLineEndsAreKept() {
        // "é" and "€" as UTF-8 octets, CRLF and a lone CR become LF for Compose.
        val text = "HTTP/1.1 200 OK\r\n\r\n" + octets(0xC3, 0xA9) + octets(0xE2, 0x82, 0xAC) + "\rend\ttab"
        val display = HttpDiagnosticText.display(message(text))!!
        assertEquals("HTTP/1.1 200 OK\n\né€\nend\ttab", display.text)
        assertFalse(display.escaped)
    }

    @Test
    fun invalidAndControlBytesAreEscaped() {
        val cases = mapOf(
            octets(0x00) to "\\x00",
            octets(0x01, 0x41) to "\\x01A",
            octets(0x7F) to "\\x7F",
            octets(0xC3) to "\\xC3",
            octets(0xC0, 0x80) to "\\xC0\\x80",
            octets(0xE0, 0x80, 0x80) to "\\xE0\\x80\\x80",
            octets(0xED, 0xA0, 0x80) to "\\xED\\xA0\\x80",
            octets(0xF4, 0x90, 0x80, 0x80) to "\\xF4\\x90\\x80\\x80",
            octets(0xF0, 0x9F, 0x98, 0x80) to "😀",
            "<script>" to "<script>",
        )
        for ((input, expected) in cases) {
            val display = HttpDiagnosticText.display(message(input))!!
            assertEquals(expected, display.text)
            assertEquals(expected, expected != input && expected.contains("\\x"), display.escaped)
        }
    }

    @Test
    fun truncatedMessagesDecodePrefixAndSuffixSeparately() {
        // A two-byte sequence split by the gap is escaped on both sides, never joined.
        val prefix = "a".repeat(HttpDiagnosticMessage.HTTP_DIAGNOSTIC_PREFIX - 1) + octets(0xC3)
        val suffix = octets(0xA9) + "b".repeat(HttpDiagnosticMessage.HTTP_DIAGNOSTIC_TAIL - 1)
        val message = HttpDiagnosticMessage(200_000uL, prefix, HttpDiagnosticMessage.SEPARATOR, suffix)
        assertTrue(message.truncated)
        val display = HttpDiagnosticText.display(message)!!
        assertTrue(display.escaped)
        assertTrue(display.text.contains("a\\xC3\n...\n...\n...\n...\n\\xA9b"))
        assertEquals((200_000 - 65_536).toULong(), HttpDiagnosticText.omittedBytes(message))
    }

    @Test
    fun invalidLengthsAreRejected() {
        assertNull(HttpDiagnosticText.display(HttpDiagnosticMessage(5uL, "abcd", "", "")))
        assertNull(HttpDiagnosticText.display(HttpDiagnosticMessage(0uL, "", "", "")))
        assertNull(HttpDiagnosticText.display(HttpDiagnosticMessage(70_000uL, "a".repeat(100), HttpDiagnosticMessage.SEPARATOR, "")))
        assertNull(HttpDiagnosticText.display(HttpDiagnosticMessage(1uL, "Ā", "", "")))
    }

    @Test
    fun displayLimitHoldsForTheWorstCase() {
        // Every byte escaped: 64512 + 1024 bytes as \xHH plus the separator is the 262166-unit maximum.
        val prefix = octets(*IntArray(HttpDiagnosticMessage.HTTP_DIAGNOSTIC_PREFIX) { 0xFF })
        val suffix = octets(*IntArray(HttpDiagnosticMessage.HTTP_DIAGNOSTIC_TAIL) { 0xFF })
        val display = HttpDiagnosticText.display(HttpDiagnosticMessage(1_000_000uL, prefix, HttpDiagnosticMessage.SEPARATOR, suffix))!!
        assertTrue(display.escaped)
        // Line ends of the separator are counted as CRLF before the projection to LF.
        assertEquals(HttpDiagnosticText.DISPLAY_LIMIT - 5, display.text.length)
    }

    @Test
    fun sizeInKibWithTwoDecimals() {
        val cases = mapOf(0uL to "0.00", 1uL to "0.00", 6uL to "0.01", 512uL to "0.50", 1023uL to "1.00", 1024uL to "1.00", 1536uL to "1.50", 65_536uL to "64.00", ULong.MAX_VALUE to "18014398509481984.00")
        for ((bytes, expected) in cases) assertEquals(bytes.toString(), expected, HttpDiagnosticText.sizeKiB(bytes))
    }

    @Test
    fun unknownHttpVersionIsDetected() {
        assertTrue(HttpDiagnosticText.unknownHttpVersion(message("POST /p.php HTTP/?\r\nA: b\r\n\r\n")))
        assertFalse(HttpDiagnosticText.unknownHttpVersion(message("POST /p.php HTTP/1.1\r\n\r\n")))
    }
}
