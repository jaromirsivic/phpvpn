package phpvpn.android.ui.screens.wizard

import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.ipc.WireFormats

/**
 * Pure rules shared with the native core, ported 1:1 so the Wizard screens
 * compute exactly what the service later verifies:
 * `defaultAccessKey`, `supportsDefaultContracts`, `defaultContractCandidates`
 * and `missingDefaultContractPeers` (core/wizard/wizard_defaults.cpp),
 * `parseServerUrl` (core/config/config_parser.cpp) and
 * `Ipv4Cidr::isValidClientAddress` (core/protocol/ipv4.cpp).
 */
object WizardRules {
    /** `kDefaultServerSecret` (core/config/config.h). */
    const val DEFAULT_SERVER_SECRET = "CHANGE_THIS_SERVER_SECRET"

    /** Host of `kWizardPlaceholderUrl`, compared case-insensitively before any DNS. */
    const val PLACEHOLDER_HOST = "modify-this-url"

    /** `kMaxConfigFileBytes`: the whole document, also the escaped header block. */
    const val MAX_CONFIG_FILE_BYTES = 524_288L

    /** First and last candidate octet of the default contracts and the IP suggestion. */
    private const val FIRST_DEFAULT_OCTET = 2L
    private const val LAST_DEFAULT_OCTET = 15L

    /**
     * The default access key: the lowercase hex of the last octet repeated 32
     * times (32 x "11" for .17). Empty for a text that is not a canonical IPv4.
     * The value is publicly derivable; the service verifies it in check-access.
     */
    fun defaultAccessKey(ip: String): SecretText {
        val value = WireFormats.parseIpv4(ip) ?: return SecretText.empty()
        val octet = (value and 255).toInt()
        val pair = charArrayOf(HEX[octet shr 4], HEX[octet and 15])
        return SecretText.adopt(CharArray(64) { pair[it and 1] })
    }

    /** `Ipv4Cidr::isValidClientAddress`: inside the CIDR, network + 2 up to broadcast - 1. */
    fun isValidClientAddress(ip: String, cidr: String?): Boolean {
        val address = WireFormats.parseIpv4(ip) ?: return false
        val (network, prefix) = WireFormats.parseCidr(cidr) ?: return false
        val hostMask = (1L shl (32 - prefix)) - 1
        if ((address and hostMask.inv() and 0xFFFFFFFFL) != network) return false
        val broadcast = network or hostMask
        return address >= network + 2 && address <= broadcast - 1
    }

    /** `supportsDefaultContracts`: exactly a /24 inside 10.0.0.0/8. */
    fun supportsDefaultContracts(cidr: String?): Boolean {
        val (network, prefix) = WireFormats.parseCidr(cidr) ?: return false
        return WireFormats.supportsDefaultContracts(network, prefix)
    }

    /** `.2` to `.15` of a supported CIDR, as numeric addresses. */
    fun defaultContractCandidates(cidr: String?): List<Long> {
        if (!supportsDefaultContracts(cidr)) return emptyList()
        val network = WireFormats.parseCidr(cidr)!!.first
        return (FIRST_DEFAULT_OCTET..LAST_DEFAULT_OCTET).map { network or it }
    }

    /** Candidates without the own address and without every original peer. */
    fun missingDefaultContractPeers(cidr: String?, own: String, existing: List<String>): List<Long> {
        val ownValue = WireFormats.parseIpv4(own)
        val existingValues = existing.mapNotNull { WireFormats.parseIpv4(it) }.toSet()
        return defaultContractCandidates(cidr).filter { it != ownValue && it !in existingValues }
    }

    /** Outcome of the local URL check before any network request. */
    enum class UrlCheck { VALID, INVALID, PLACEHOLDER }

    /**
     * `parseServerUrl` plus the Wizard placeholder rule: http or https, no
     * userinfo, query or fragment, a valid host and port; the placeholder host
     * `MODIFY-THIS-URL` is rejected with its own advice before DNS.
     */
    fun checkServerUrl(text: String): UrlCheck {
        val host = serverUrlHost(text) ?: return UrlCheck.INVALID
        return if (host == PLACEHOLDER_HOST) UrlCheck.PLACEHOLDER else UrlCheck.VALID
    }

    /** Lowercase host of a URL accepted by `parseServerUrl`, or null. */
    fun serverUrlHost(text: String): String? {
        if (text.isEmpty() || text.any { it.code <= 0x20 || it.code >= 0x7F }) return null
        val lowered = text.lowercase()
        val rest = when {
            lowered.startsWith("https://") -> text.substring(8)
            lowered.startsWith("http://") -> text.substring(7)
            else -> return null
        }
        if ('#' in rest || '?' in rest) return null
        val authorityEnd = rest.indexOf('/')
        val authority = if (authorityEnd < 0) rest else rest.substring(0, authorityEnd)
        if ('@' in authority) return null
        var host = authority
        val colon = authority.lastIndexOf(':')
        if (colon >= 0) {
            host = authority.substring(0, colon)
            val port = authority.substring(colon + 1)
            if (port.isEmpty() || port.length > 5 || port.any { it !in '0'..'9' }) return null
            if (port.toInt() !in 1..65535) return null
        }
        if (!validHost(host)) return null
        return host.lowercase()
    }

    private fun validHost(host: String): Boolean {
        if (host.isEmpty() || host.length > 253) return false
        if (host.first() == '.' || host.last() == '.' || host.first() == '-') return false
        if (host.any { !(it in 'a'..'z' || it in 'A'..'Z' || it in '0'..'9' || it == '-' || it == '.') }) return false
        return !host.contains("..")
    }

    private val HEX = "0123456789abcdef".toCharArray()
}

/**
 * Display and parsing of a server secret with control characters as a JSON
 * string literal (`Json::escapeString` and the strict parser of
 * core/protocol/json.cpp), on [CharArray] so no extra immutable copy of the
 * secret is created. Every returned array belongs to the caller.
 */
object SecretLiteral {
    /** True when the exact secret cannot be shown in an ordinary single-line field. */
    fun needed(secret: CharArray): Boolean = secret.any { it.code < 0x20 || it.code == 0x7F }

    /** `Json::escapeString`: quotes, the short escapes and `\u00XX` for other C0 octets. */
    fun escape(secret: CharArray): CharArray {
        var size = 2
        for (c in secret) size += shortEscape(c)?.let { 2 } ?: if (c.code < 0x20) 6 else 1
        val out = CharArray(size)
        var n = 0
        out[n++] = '"'
        for (c in secret) {
            val short = shortEscape(c)
            when {
                short != null -> {
                    out[n++] = '\\'
                    out[n++] = short
                }
                c.code < 0x20 -> {
                    for (prefix in "\\u00") out[n++] = prefix
                    out[n++] = HEX[c.code shr 4]
                    out[n++] = HEX[c.code and 15]
                }
                else -> out[n++] = c
            }
        }
        out[n] = '"'
        return out
    }

    private fun shortEscape(c: Char): Char? = when (c) {
        '"' -> '"'
        '\\' -> '\\'
        '\b' -> 'b'
        '\u000C' -> 'f'
        '\n' -> 'n'
        '\r' -> 'r'
        '\t' -> 't'
        else -> null
    }

    /**
     * The value of one complete JSON string literal, optionally surrounded by
     * JSON whitespace; null for anything else (missing quote, raw control
     * character, invalid escape, unpaired surrogate).
     */
    fun parse(text: CharArray): CharArray? {
        var start = 0
        var end = text.size
        while (start < end && text[start] in JSON_WHITESPACE) start++
        while (end > start && text[end - 1] in JSON_WHITESPACE) end--
        if (end - start < 2 || text[start] != '"' || text[end - 1] != '"') return null
        val out = CharArray(end - start)
        var size = 0
        var i = start + 1
        val last = end - 1
        while (i < last) {
            val c = text[i]
            when {
                c == '"' || c.code < 0x20 -> return wiped(out)
                c != '\\' -> {
                    out[size++] = c
                    i++
                }
                else -> {
                    if (i + 1 >= last) return wiped(out)
                    when (text[i + 1]) {
                        '"' -> out[size++] = '"'
                        '\\' -> out[size++] = '\\'
                        '/' -> out[size++] = '/'
                        'b' -> out[size++] = '\b'
                        'f' -> out[size++] = '\u000C'
                        'n' -> out[size++] = '\n'
                        'r' -> out[size++] = '\r'
                        't' -> out[size++] = '\t'
                        'u' -> {
                            if (i + 6 > last) return wiped(out)
                            var code = 0
                            for (k in 2..5) {
                                val digit = Character.digit(text[i + k], 16)
                                if (digit < 0) return wiped(out)
                                code = code * 16 + digit
                            }
                            out[size++] = code.toChar()
                            i += 4
                        }
                        else -> return wiped(out)
                    }
                    i += 2
                }
            }
        }
        val result = out.copyOf(size)
        out.fill('\u0000')
        if (!pairedSurrogates(result)) return wiped(result)
        return result
    }

    /** True when every UTF-16 surrogate is part of a pair (strict UTF-8 is possible). */
    fun pairedSurrogates(text: CharArray): Boolean {
        var i = 0
        while (i < text.size) {
            val c = text[i]
            if (Character.isHighSurrogate(c)) {
                if (i + 1 >= text.size || !Character.isLowSurrogate(text[i + 1])) return false
                i += 2
                continue
            }
            if (Character.isLowSurrogate(c)) return false
            i++
        }
        return true
    }

    private fun wiped(buffer: CharArray): CharArray? {
        buffer.fill('\u0000')
        return null
    }

    private val JSON_WHITESPACE = charArrayOf(' ', '\t', '\n', '\r')
    private val HEX = "0123456789abcdef".toCharArray()
}
