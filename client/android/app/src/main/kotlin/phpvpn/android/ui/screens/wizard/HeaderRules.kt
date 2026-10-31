package phpvpn.android.ui.screens.wizard

/**
 * Reason of a rejected header block, the closed reason list of
 * `normalizeHttpHeaders` (core/protocol/http_headers.cpp). The screen turns
 * it into a resource text with the line number and, for [MANAGED] and
 * [DUPLICATE], the header name; a value is never shown.
 */
enum class HeaderProblem {
    REQUIRED, LINE_FORMAT, BARE_CR, INVALID_NAME, CONTROL_OCTET, MANAGED, DUPLICATE, TOO_LARGE, ENCODING,

    /** The draft cannot change now (a check, save or reset is running); not a validator reason. */
    BUSY,
}

/** Result of [HeaderRules.validate]. */
sealed interface HeaderCheck {
    /** The canonical CRLF block, without a trailing separator. */
    data class Valid(val normalized: String) : HeaderCheck

    data class Invalid(val problem: HeaderProblem, val line: Int, val name: String? = null) : HeaderCheck
}

/**
 * Port of `normalizeHttpHeaders` with its error details, plus the Wizard draft
 * limit of `WizardModel::setHttpHeaders`: the JSON-escaped block must fit
 * into the 512 KiB configuration document.
 */
object HeaderRules {
    private val managed = listOf("Content-Type", "Content-Length", "Connection", "Host", "Transfer-Encoding")
    private const val TOKEN_SYMBOLS = "!#$%&'*+-.^_`|~"

    fun validate(input: String): HeaderCheck {
        if (input.isEmpty()) return HeaderCheck.Invalid(HeaderProblem.REQUIRED, 1)
        // isStrictUtf8: an unpaired surrogate has no UTF-8 form (config_encoding_error).
        if (!SecretLiteral.pairedSurrogates(input.toCharArray())) return HeaderCheck.Invalid(HeaderProblem.ENCODING, 1)
        val output = StringBuilder(input.length)
        val names = HashSet<String>()
        var line = 1
        var nameStart = 0
        var nameSize = 0
        var value = false
        var i = 0
        while (i < input.length) {
            val c = input[i]
            if (c == '\r' || c == '\n') {
                if (!value) return HeaderCheck.Invalid(HeaderProblem.LINE_FORMAT, line)
                if (c == '\r') {
                    if (i + 1 == input.length || input[i + 1] != '\n') return HeaderCheck.Invalid(HeaderProblem.BARE_CR, line)
                    i++
                }
                if (i + 1 == input.length) return finish(output.toString())
                output.append("\r\n")
                line++
                nameSize = 0
                nameStart = i + 1
                value = false
                i++
                continue
            }
            if (!value) {
                if (c == ':' && nameSize != 0) {
                    val name = input.substring(nameStart, nameStart + nameSize)
                    if (managed.any { it.equals(name, ignoreCase = true) && it.length == name.length }) {
                        return HeaderCheck.Invalid(HeaderProblem.MANAGED, line, name)
                    }
                    if (!names.add(asciiLower(name))) return HeaderCheck.Invalid(HeaderProblem.DUPLICATE, line, name)
                    value = true
                } else if (!token(c)) {
                    return HeaderCheck.Invalid(HeaderProblem.INVALID_NAME, line)
                } else {
                    nameSize++
                }
            } else if ((c.code < 0x20 && c != '\t') || c.code == 0x7F) {
                return HeaderCheck.Invalid(HeaderProblem.CONTROL_OCTET, line)
            }
            output.append(c)
            i++
        }
        if (!value) return HeaderCheck.Invalid(HeaderProblem.LINE_FORMAT, line)
        return finish(output.toString())
    }

    private fun finish(normalized: String): HeaderCheck =
        if (escapedUtf8Size(normalized) > WizardRules.MAX_CONFIG_FILE_BYTES) {
            HeaderCheck.Invalid(HeaderProblem.TOO_LARGE, 1)
        } else {
            HeaderCheck.Valid(normalized)
        }

    /** Size of `Json::escapeString(text)` in UTF-8 bytes, quotes included. */
    fun escapedUtf8Size(text: String): Long {
        var size = 2L
        var i = 0
        while (i < text.length) {
            val c = text[i]
            size += when {
                c == '"' || c == '\\' || c == '\b' || c == '\u000C' || c == '\n' || c == '\r' || c == '\t' -> 2
                c.code < 0x20 -> 6
                c.code < 0x80 -> 1
                c.code < 0x800 -> 2
                Character.isHighSurrogate(c) && i + 1 < text.length && Character.isLowSurrogate(text[i + 1]) -> {
                    i++
                    4
                }
                else -> 3
            }
            i++
        }
        return size
    }

    /** The draft text the editor shows: one header per line with LF. */
    fun displayText(block: String): String = block.replace("\r\n", "\n")

    private fun token(c: Char): Boolean = c in 'a'..'z' || c in 'A'..'Z' || c in '0'..'9' || TOKEN_SYMBOLS.indexOf(c) >= 0

    private fun asciiLower(text: String): String = buildString(text.length) {
        for (c in text) append(if (c in 'A'..'Z') c + ('a' - 'A') else c)
    }
}
