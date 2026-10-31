package phpvpn.android.standin

/**
 * Random of the Password SHA tool (`PasswordShaWindow::randomPassword`,
 * apps/tray/password_sha_window.cpp): 64 characters from a 17-letter alphabet
 * without look-alike letters, rejection sampling without modulo bias. Returns
 * null when the random source fails; the caller keeps its current password.
 * The result belongs to the caller, who wipes it.
 */
fun randomPassword(random: RandomSource = RandomSource.SECURE): CharArray? {
    val alphabet = "ghjkmnpqrstuvwxyz"
    val limit = 256 - (256 % alphabet.length)
    val bytes = ByteArray(64)
    val password = CharArray(64)
    var used = 0
    try {
        while (used < password.size) {
            if (!random.fill(bytes)) {
                password.fill('\u0000')
                return null
            }
            for (byte in bytes) {
                val value = byte.toInt() and 0xff
                // 255 is rejected, so each of the 17 letters has exactly 15 byte values.
                if (value >= limit) continue
                password[used++] = alphabet[value % alphabet.length]
                if (used == password.size) break
            }
        }
        return password
    } finally {
        bytes.fill(0)
    }
}
