package phpvpn.android.bridge.controller

import phpvpn.android.bridge.ipc.SecretText

/** `PasswordKeyError` (core/presentation/password_key_model.h). */
enum class PasswordKeyError { NONE, TOO_LONG, INVALID_UTF16, UNSUPPORTED_CONTROL, CRYPTO_FAILED }

/**
 * `PasswordKeyModel` (core/presentation/password_key_model.h), the Password →
 * SHA-256 tool of screen 21. WP04 implements it over JNI with the native model
 * and crypto provider; the app runs a Kotlin stand-in until then.
 *
 * The password crosses as UTF-16 [CharArray] exactly as typed, without any
 * conversion or normalization: the model encodes it to UTF-8 itself. The
 * model never retains the password; the caller wipes its array after
 * [update]. Calls must be serialized (UI thread).
 */
interface PasswordKeyModelHandle : AutoCloseable {
    /**
     * Hashes [password]. [PasswordKeyError.TOO_LONG] keeps the previous output
     * and count, so the view must restore its previous input; every other
     * error clears the output. [valid], not the error, decides whether a key
     * can be used.
     */
    fun update(password: CharArray): PasswordKeyError

    val valid: Boolean

    /** Copy of the lowercase hex key the caller wipes; empty while not [valid]. */
    fun key(): SecretText

    val scalarCount: Int

    val error: PasswordKeyError

    fun clear()

    /** Clears the key and releases the native model. */
    override fun close()
}
