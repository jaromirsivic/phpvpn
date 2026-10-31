package phpvpn.android.bridge.jni

import phpvpn.android.bridge.controller.ContractTargetHandle
import phpvpn.android.bridge.ipc.SecretText

/** `PasswordKeyError` (core/presentation/password_key_model.h), same order. */
enum class PasswordKeyError { NONE, TOO_LONG, INVALID_UTF16, UNSUPPORTED_CONTROL, CRYPTO_FAILED }

/**
 * The "Password SHA" dialog model (screen 21) over the native
 * `PasswordKeyModel`: the UTF-16 units of the input go to the model without
 * any conversion on the Kotlin side, the model hashes their exact UTF-8 with
 * SHA-256 and keeps the lowercase hex key. `TOO_LONG` keeps the previous key,
 * so the UI restores the previous input; [valid], not `error == NONE`, says
 * whether a usable key exists. Single owner; [close] wipes and frees it.
 */
class NativePasswordKeyModel : AutoCloseable {
    private val resource = NativeResource(ModelNatives.passwordCreate()) { ModelNatives.passwordDestroy(it) }

    /** Borrows [password] only for the call; the caller keeps and wipes it. */
    fun update(password: CharArray): PasswordKeyError =
        PasswordKeyError.entries[resource.withHandle { ModelNatives.passwordUpdate(it, password) }]

    val valid: Boolean get() = state()[0] != 0
    val scalarCount: Int get() = state()[1]
    val error: PasswordKeyError get() = PasswordKeyError.entries[state()[2]]

    private fun state(): IntArray = resource.withHandle { ModelNatives.passwordState(it) } ?: intArrayOf(0, 0, PasswordKeyError.CRYPTO_FAILED.ordinal)

    /** The 64 character hex key as a copy the caller wipes, or null while not [valid]. */
    fun key(): SecretText? = resource.withHandle { ModelNatives.passwordKey(it) }?.let { SecretText.adopt(it) }

    fun clear() = resource.withHandle { ModelNatives.passwordClear(it) }

    override fun close() = resource.close()

    companion object {
        /** `PasswordKeyModel::kMaxScalars`. */
        const val MAX_SCALARS = 255

        /**
         * The "Random" action (apps/tray/password_sha_window.cpp): 64 characters
         * of `ghjkmnpqrstuvwxyz` from the native crypto provider without modulo
         * bias; null when no random bytes are available. The caller wipes it.
         */
        fun randomPassword(): SecretText? = ModelNatives.passwordRandom()?.let { SecretText.adopt(it) }

        /** The "Default" action: the canonical contract id of the captured target. */
        fun defaultPassword(target: ContractTargetHandle): SecretText = SecretText.of(target.contractId)
    }
}
