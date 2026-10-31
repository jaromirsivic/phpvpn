package phpvpn.android.ui.screens.password

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import phpvpn.android.bridge.controller.PasswordKeyError
import phpvpn.android.bridge.controller.PasswordKeyModelHandle
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.standin.RandomSource
import phpvpn.android.standin.randomPassword

/**
 * State of screen 21 (Password → SHA-256, Default, Random) over the native
 * `PasswordKeyModel` ([PasswordKeyModelHandle], a Kotlin stand-in until
 * WP04). The typed UTF-16 text goes to the model as a [CharArray] without any
 * conversion and is wiped after every update; the model never keeps the
 * password. Only the text field itself holds a String (Compose).
 */
class PasswordKeyViewModel(
    private val model: PasswordKeyModelHandle,
    private val random: RandomSource = RandomSource.SECURE,
) : ViewModel() {
    /** Content of the password field. */
    var password by mutableStateOf("")
        private set

    /** Lowercase hex preview of the derived key; empty while there is none. */
    var keyPreview by mutableStateOf("")
        private set

    var error by mutableStateOf(PasswordKeyError.NONE)
        private set

    var randomFailed by mutableStateOf(false)
        private set

    val valid: Boolean get() = keyPreview.isNotEmpty()

    init {
        // The cue text is never hashed: the empty input has the standard empty hash.
        hash(CharArray(0))
    }

    /** A typed change. Returns false when the model refused the whole edit (the field keeps its text). */
    fun update(text: String): Boolean {
        randomFailed = false
        val chars = text.toCharArray()
        return try {
            accept(text, hash(chars))
        } finally {
            chars.fill('\u0000')
        }
    }

    /** Default: the canonical `minIP_maxIP` pair of the captured target. */
    fun useDefault(contractId: String) {
        update(contractId)
    }

    fun useRandom() {
        val chars = randomPassword(random)
        if (chars == null) {
            randomFailed = true
            return
        }
        try {
            accept(String(chars), hash(chars))
        } finally {
            chars.fill('\u0000')
        }
        randomFailed = false
    }

    /** The derived key for the contract draft; the caller wipes it. */
    fun key(): SecretText = model.key()

    private fun hash(chars: CharArray): PasswordKeyError {
        val result = model.update(chars)
        error = result
        val key = model.key()
        keyPreview = try {
            key.reveal()
        } finally {
            key.wipe()
        }
        return result
    }

    /** The desktop input adapter refuses an over-long or control-character edit as a whole. */
    private fun accept(text: String, result: PasswordKeyError): Boolean {
        val accepted = result != PasswordKeyError.TOO_LONG && result != PasswordKeyError.UNSUPPORTED_CONTROL
        if (accepted) password = text
        return accepted
    }

    override fun onCleared() {
        password = ""
        keyPreview = ""
        model.close()
    }
}
