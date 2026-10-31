package phpvpn.android.standin

import java.security.MessageDigest
import phpvpn.android.bridge.controller.PasswordKeyError
import phpvpn.android.bridge.controller.PasswordKeyModelHandle
import phpvpn.android.bridge.ipc.SecretText

/** `CryptoProvider::sha256` of the stand-in; null is a provider failure. */
fun interface Sha256 {
    fun digest(input: ByteArray, length: Int): ByteArray?

    companion object {
        val PLATFORM: Sha256 = Sha256 { input, length ->
            MessageDigest.getInstance("SHA-256").run {
                update(input, 0, length)
                digest()
            }
        }
    }
}

/**
 * Kotlin stand-in of `PasswordKeyModel` (core/presentation/password_key_model.cpp)
 * for screen 21 until WP04 binds the native model. The UTF-16 password is
 * encoded to UTF-8 here exactly as typed (no trimming, normalization or case
 * change), limited to 255 Unicode scalar values, and hashed with SHA-256.
 * The working UTF-8 buffer and the digest are wiped after every call.
 */
class StandInPasswordKeyModel(private val sha256: Sha256 = Sha256.PLATFORM) : PasswordKeyModelHandle {
    private var keyChars = CharArray(0)
    private var scalars = 0
    private var lastError = PasswordKeyError.NONE

    override val valid: Boolean get() = keyChars.isNotEmpty()
    override val scalarCount: Int get() = scalars
    override val error: PasswordKeyError get() = lastError

    override fun key(): SecretText = SecretText.adopt(keyChars.copyOf())

    override fun update(password: CharArray): PasswordKeyError {
        fun reject(error: PasswordKeyError, invalidate: Boolean): PasswordKeyError {
            lastError = error
            if (invalidate) {
                keyChars.fill('\u0000')
                keyChars = CharArray(0)
                scalars = 0
            }
            return error
        }
        // A valid value cannot exceed 510 UTF-16 units; checked before scanning.
        if (password.size > MAX_SCALARS * 2) return reject(PasswordKeyError.TOO_LONG, false)
        val work = ByteArray(MAX_SCALARS * 4)
        var digest: ByteArray? = null
        try {
            var count = 0
            var bytes = 0
            var i = 0
            while (i < password.size) {
                var code = password[i].code
                if (code in 0xd800..0xdbff) {
                    if (i + 1 == password.size || password[i + 1].code !in 0xdc00..0xdfff) return reject(PasswordKeyError.INVALID_UTF16, true)
                    code = 0x10000 + ((code - 0xd800) shl 10) + (password[++i].code - 0xdc00)
                } else if (code in 0xdc00..0xdfff) {
                    return reject(PasswordKeyError.INVALID_UTF16, true)
                }
                // Single-line policy: C0, DEL/C1 and the Unicode line and
                // paragraph separators are unsupported; nothing is replaced.
                if (code < 0x20 || code in 0x7f..0x9f || code == 0x2028 || code == 0x2029) {
                    return reject(PasswordKeyError.UNSUPPORTED_CONTROL, true)
                }
                if (++count > MAX_SCALARS) return reject(PasswordKeyError.TOO_LONG, false)
                when {
                    code < 0x80 -> work[bytes++] = code.toByte()
                    code < 0x800 -> {
                        work[bytes++] = (0xc0 or (code shr 6)).toByte()
                        work[bytes++] = (0x80 or (code and 0x3f)).toByte()
                    }
                    code < 0x10000 -> {
                        work[bytes++] = (0xe0 or (code shr 12)).toByte()
                        work[bytes++] = (0x80 or ((code shr 6) and 0x3f)).toByte()
                        work[bytes++] = (0x80 or (code and 0x3f)).toByte()
                    }
                    else -> {
                        work[bytes++] = (0xf0 or (code shr 18)).toByte()
                        work[bytes++] = (0x80 or ((code shr 12) and 0x3f)).toByte()
                        work[bytes++] = (0x80 or ((code shr 6) and 0x3f)).toByte()
                        work[bytes++] = (0x80 or (code and 0x3f)).toByte()
                    }
                }
                i++
            }
            digest = sha256.digest(work, bytes) ?: return reject(PasswordKeyError.CRYPTO_FAILED, true)
            val hex = CharArray(digest.size * 2)
            for (index in digest.indices) {
                val value = digest[index].toInt() and 0xff
                hex[2 * index] = HEX[value shr 4]
                hex[2 * index + 1] = HEX[value and 0xf]
            }
            keyChars.fill('\u0000')
            keyChars = hex
            scalars = count
            lastError = PasswordKeyError.NONE
            return lastError
        } finally {
            work.fill(0)
            digest?.fill(0)
        }
    }

    override fun clear() {
        keyChars.fill('\u0000')
        keyChars = CharArray(0)
        scalars = 0
        lastError = PasswordKeyError.NONE
    }

    override fun close() = clear()

    override fun toString(): String = "StandInPasswordKeyModel(valid=$valid, error=$lastError)"

    companion object {
        /** `PasswordKeyModel::kMaxScalars`. */
        const val MAX_SCALARS = 255
        private val HEX = "0123456789abcdef".toCharArray()
    }
}
