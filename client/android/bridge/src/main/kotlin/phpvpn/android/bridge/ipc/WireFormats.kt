package phpvpn.android.bridge.ipc

/**
 * Exact spelling rules shared with the native core: UUIDv4 (core/protocol/uuid.cpp),
 * IPv4 and CIDR (core/protocol/ipv4.cpp), the millisecond UTC timestamp
 * (core/protocol/iso8601.cpp) and the absolute POSIX configuration path
 * (`absoluteConfigPath` in core/ipc/wizard_ipc.cpp, non-Windows branch).
 */
object WireFormats {
    /** Lowercase UUID version 4 with an RFC 4122 variant. */
    fun isUuidV4(text: String?): Boolean {
        if (text == null || text.length != 36) return false
        for (i in text.indices) {
            val c = text[i]
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                if (c != '-') return false
            } else if (!(c in '0'..'9' || c in 'a'..'f')) {
                return false
            }
        }
        return text[14] == '4' && text[19] in "89ab"
    }

    /** Dotted IPv4 without leading zeros, as an unsigned 32-bit value. */
    fun parseIpv4(text: String?): Long? {
        if (text == null) return null
        val parts = text.split('.')
        if (parts.size != 4) return null
        var value = 0L
        for (part in parts) {
            if (part.isEmpty() || part.length > 3 || (part.length > 1 && part[0] == '0')) return null
            if (!part.all { it in '0'..'9' }) return null
            val octet = part.toInt()
            if (octet > 255) return null
            value = (value shl 8) or octet.toLong()
        }
        return value
    }

    fun formatIpv4(value: Long): String =
        "${(value shr 24) and 255}.${(value shr 16) and 255}.${(value shr 8) and 255}.${value and 255}"

    /** A canonical VPN CIDR: /24 to /29 without host bits. Returns (network, prefix). */
    fun parseCidr(text: String?): Pair<Long, Int>? {
        if (text == null) return null
        val slash = text.indexOf('/')
        if (slash < 0) return null
        val network = parseIpv4(text.substring(0, slash)) ?: return null
        val lengthText = text.substring(slash + 1)
        if (lengthText.isEmpty() || lengthText.length > 2 || (lengthText.length > 1 && lengthText[0] == '0')) return null
        if (!lengthText.all { it in '0'..'9' }) return null
        val prefix = lengthText.toInt()
        if (prefix < 24 || prefix > 29) return null
        val mask = (0xFFFFFFFFL shl (32 - prefix)) and 0xFFFFFFFFL
        if ((network and mask) != network) return null
        return network to prefix
    }

    /** Client addresses of a prefix: network + 2 up to broadcast - 1. */
    fun clientAddressCount(prefix: Int): Long {
        val hosts = 1L shl (32 - prefix)
        return (hosts - 3).coerceAtLeast(0)
    }

    /** `supportsDefaultContracts`: exactly a /24 inside 10.0.0.0/8. */
    fun supportsDefaultContracts(network: Long, prefix: Int): Boolean = prefix == 24 && (network shr 24) == 10L

    /** `YYYY-MM-DDTHH:MM:SS.mmmZ` to epoch milliseconds, or null. */
    fun parseIso8601(text: String?): Long? {
        if (text == null || text.length != 24) return null
        if (text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':' ||
            text[19] != '.' || text[23] != 'Z'
        ) return null
        fun number(offset: Int, count: Int): Int? {
            var value = 0
            for (i in offset until offset + count) {
                val c = text[i]
                if (c !in '0'..'9') return null
                value = value * 10 + (c - '0')
            }
            return value
        }
        val year = number(0, 4) ?: return null
        val month = number(5, 2) ?: return null
        val day = number(8, 2) ?: return null
        val hour = number(11, 2) ?: return null
        val minute = number(14, 2) ?: return null
        val second = number(17, 2) ?: return null
        val milli = number(20, 3) ?: return null
        if (month !in 1..12) return null
        val leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0
        val days = intArrayOf(31, if (leap) 29 else 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)[month - 1]
        if (day < 1 || day > days) return null
        if (hour > 23 || minute > 59 || second > 59) return null
        val epochDays = java.time.LocalDate.of(year, month, day).toEpochDay()
        return ((epochDays * 86400 + hour * 3600L + minute * 60L + second) * 1000) + milli
    }

    /** Absolute normalised POSIX file path, the Android configuration location. */
    fun isAbsoluteConfigPath(path: String?): Boolean {
        if (path == null || path.any { it.code < 32 }) return false
        if (path.length < 2 || path[0] != '/' || path.last() == '/') return false
        for (segment in path.substring(1).split('/')) {
            if (segment.isEmpty() || segment == "." || segment == "..") return false
        }
        return true
    }

    /** Lowercase 64-digit hexadecimal (masked contract hash, key text). */
    fun isLowercaseHex64(text: String?): Boolean =
        text != null && text.length == 64 && text.all { it in '0'..'9' || it in 'a'..'f' }

    /** Length of the UTF-8 encoding without allocating it. */
    fun utf8Length(text: String): Long {
        var bytes = 0L
        var i = 0
        while (i < text.length) {
            val c = text[i]
            bytes += when {
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
        return bytes
    }
}
