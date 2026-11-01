package phpvpn.android.ui.screens.diagnostic

import phpvpn.android.bridge.ipc.HttpDiagnosticMessage

/**
 * The read-only projection of one `http-octets-v1` message, the Kotlin port
 * of `diagnosticDisplayText` and `diagnosticSizeKiB`
 * (core/wizard/http_diagnostic.cpp): the transport characters U+0000..U+00FF
 * are first turned back into the original octets, then valid UTF-8 text and
 * line ends are kept while every byte of an invalid or incomplete sequence,
 * NUL, the other C0 controls except TAB/CR/LF, and DEL become `\xHH`. Prefix
 * and suffix are decoded separately and never joined across the omitted gap.
 * The content is shown as plain text only, never interpreted.
 */
object HttpDiagnosticText {
    /** Longest projection in UTF-16 units (65536 source bytes as `\xHH` plus the separator). */
    const val DISPLAY_LIMIT = 262_166

    /** [text] has CRLF and lone CR as LF, ready for a Compose text; [escaped] reports `\xHH`. */
    data class Display(val text: String, val escaped: Boolean)

    /** `validHttpMessage`: exact prefix and suffix lengths for the original size. */
    fun valid(message: HttpDiagnosticMessage): Boolean {
        val size = message.originalSizeBytes
        if (size == 0uL) return false
        if (message.prefix.any { it.code > 0xFF } || message.suffix.any { it.code > 0xFF }) return false
        return if (message.truncated) {
            message.prefix.length == HttpDiagnosticMessage.HTTP_DIAGNOSTIC_PREFIX &&
                message.suffix.length == HttpDiagnosticMessage.HTTP_DIAGNOSTIC_TAIL
        } else {
            message.prefix.length.toULong() == size && message.suffix.isEmpty()
        }
    }

    /** The projection, or null for an invalid message or one beyond [DISPLAY_LIMIT]. */
    fun display(message: HttpDiagnosticMessage): Display? {
        if (!valid(message)) return null
        val out = StringBuilder(message.prefix.length + message.suffix.length + HttpDiagnosticMessage.SEPARATOR.length)
        var escaped = segment(message.prefixOctets(), out)
        if (message.truncated) {
            out.append(HttpDiagnosticMessage.SEPARATOR)
            escaped = segment(message.suffixOctets(), out) || escaped
        }
        if (out.length > DISPLAY_LIMIT) return null
        return Display(lineEnds(out), escaped)
    }

    /** `diagnosticSizeKiB`: KiB with two decimals and a decimal point, rounded half up. */
    fun sizeKiB(bytes: ULong): String {
        var whole = bytes / 1024uL
        var hundredths = (100uL * (bytes % 1024uL) + 512uL) / 1024uL
        if (hundredths == 100uL) {
            whole++
            hundredths = 0uL
        }
        return "$whole." + (if (hundredths < 10uL) "0" else "") + hundredths
    }

    /** Bytes left out between prefix and suffix of a truncated message. */
    fun omittedBytes(message: HttpDiagnosticMessage): ULong =
        if (message.truncated) message.originalSizeBytes - HttpDiagnosticMessage.HTTP_DIAGNOSTIC_LIMIT.toULong() else 0uL

    /** True when the request line reports an unavailable HTTP version (`HTTP/?`). */
    fun unknownHttpVersion(message: HttpDiagnosticMessage): Boolean = message.prefix.contains(" HTTP/?\r\n")

    private fun segment(bytes: ByteArray, out: StringBuilder): Boolean {
        var escaped = false
        var i = 0
        while (i < bytes.size) {
            val b = bytes[i].toInt() and 0xFF
            val n = when {
                b < 0x80 -> 1
                b in 0xC2..0xDF -> 2
                b in 0xE0..0xEF -> 3
                b in 0xF0..0xF4 -> 4
                else -> 0
            }
            var valid = n != 0 && n <= bytes.size - i
            var cp = b
            if (valid && n > 1) {
                cp = b and (0x7F shr n)
                for (j in 1 until n) {
                    val c = bytes[i + j].toInt() and 0xFF
                    if ((c and 0xC0) != 0x80) valid = false
                    cp = (cp shl 6) or (c and 0x3F)
                }
                if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) ||
                    cp > 0x10FFFF || cp in 0xD800..0xDFFF
                ) valid = false
            }
            if (!valid || (b < 0x20 && b != 0x09 && b != 0x0D && b != 0x0A) || b == 0x7F) {
                out.append("\\x").append(HEX[b shr 4]).append(HEX[b and 15])
                escaped = true
                i++
            } else {
                out.appendCodePoint(cp)
                i += n
            }
        }
        return escaped
    }

    /** CRLF and a lone CR become LF: a Compose text breaks lines only on LF. */
    private fun lineEnds(text: CharSequence): String {
        val out = StringBuilder(text.length)
        var i = 0
        while (i < text.length) {
            val c = text[i]
            if (c == '\r') {
                out.append('\n')
                if (i + 1 < text.length && text[i + 1] == '\n') i++
            } else {
                out.append(c)
            }
            i++
        }
        return out.toString()
    }

    private val HEX = "0123456789ABCDEF".toCharArray()
}
