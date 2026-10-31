package phpvpn.android.bridge.ipc

/**
 * Port of `normalizeHttpHeaders` (core/protocol/http_headers.cpp), used to
 * validate the `httpHeaders` strings of Wizard views and requests exactly like
 * the desktop parser. Returns the normalised CRLF block, or null.
 */
object HttpHeaders {
    const val DEFAULT =
        "Cache-Control: no-cache\r\nPragma: no-cache\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:155.0) Gecko/20100101 Firefox/155.0"

    private val managed = listOf("content-type", "content-length", "connection", "host", "transfer-encoding")
    private const val TOKEN_SYMBOLS = "!#$%&'*+-.^_`|~"

    private fun token(c: Char): Boolean =
        c in 'a'..'z' || c in 'A'..'Z' || c in '0'..'9' || TOKEN_SYMBOLS.indexOf(c) >= 0

    fun normalize(input: String): String? {
        if (input.isEmpty()) return null
        val output = StringBuilder(input.length)
        val names = HashSet<String>()
        var nameStart = 0
        var nameSize = 0
        var value = false
        var i = 0
        while (i < input.length) {
            val c = input[i]
            if (c == '\r' || c == '\n') {
                if (!value) return null
                if (c == '\r') {
                    if (i + 1 == input.length || input[i + 1] != '\n') return null
                    i++
                }
                if (i + 1 == input.length) return output.toString()
                output.append("\r\n")
                nameSize = 0
                nameStart = i + 1
                value = false
                i++
                continue
            }
            if (!value) {
                if (c == ':' && nameSize != 0) {
                    val name = input.substring(nameStart, nameStart + nameSize).lowercase()
                    if (name in managed) return null
                    if (!names.add(name)) return null
                    value = true
                } else if (!token(c)) {
                    return null
                } else {
                    nameSize++
                }
            } else if ((c.code < 0x20 && c != '\t') || c.code == 0x7f) {
                return null
            }
            output.append(c)
            i++
        }
        return if (value) output.toString() else null
    }

    fun isValid(input: String): Boolean = normalize(input) != null
}
