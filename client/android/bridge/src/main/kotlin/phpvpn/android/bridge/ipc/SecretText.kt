package phpvpn.android.bridge.ipc

import kotlinx.serialization.KSerializer
import kotlinx.serialization.Serializable
import kotlinx.serialization.descriptors.PrimitiveKind
import kotlinx.serialization.descriptors.PrimitiveSerialDescriptor
import kotlinx.serialization.descriptors.SerialDescriptor
import kotlinx.serialization.encoding.Decoder
import kotlinx.serialization.encoding.Encoder

/**
 * Secret text (SERVER_SECRET, ACCESS_KEY, payload keys, whole configuration
 * documents) held in a [CharArray] that the owner wipes with [wipe] as soon as
 * it is no longer needed.
 *
 * [toString] never reveals the content, so a secret cannot reach a log, an
 * exception message or test output by accident. Wiping is best effort, exactly
 * like the native `secureWipe`: the JVM may have copied the characters before
 * (for example the immutable [String] created while a JSON reply was parsed),
 * and those copies are left to the garbage collector.
 */
@Serializable(with = SecretTextSerializer::class)
class SecretText private constructor(private var chars: CharArray) {
    private var wiped = false

    val length: Int get() = chars.size

    fun isEmpty(): Boolean = chars.isEmpty()

    /** True after [wipe]; the content is then empty. */
    val isWiped: Boolean get() = wiped

    /** A copy the caller owns and must wipe. */
    fun copyChars(): CharArray = chars.copyOf()

    /** Gives temporary access without another copy; the array must not escape [block]. */
    fun <T> read(block: (CharArray) -> T): T = block(chars)

    /** Exposes the content as a String for a JSON encoder or an equality check. */
    fun reveal(): String = String(chars)

    /** An independent copy with its own lifetime. */
    fun copy(): SecretText = SecretText(chars.copyOf())

    /** Zeroes the characters. Idempotent. */
    fun wipe() {
        chars.fill('\u0000')
        chars = CharArray(0)
        wiped = true
    }

    /** Constant-time comparison of the content. */
    fun contentEquals(other: SecretText): Boolean {
        val a = chars
        val b = other.chars
        var difference = a.size xor b.size
        for (i in 0 until maxOf(a.size, b.size)) {
            val x = if (i < a.size) a[i].code else 0
            val y = if (i < b.size) b[i].code else 0
            difference = difference or (x xor y)
        }
        return difference == 0
    }

    override fun equals(other: Any?): Boolean = other is SecretText && contentEquals(other)

    override fun hashCode(): Int = chars.size

    override fun toString(): String = "SecretText(<redacted>)"

    companion object {
        /** Takes ownership of [chars]; the caller must not keep a reference. */
        fun adopt(chars: CharArray): SecretText = SecretText(chars)

        fun of(text: String): SecretText = SecretText(text.toCharArray())

        fun empty(): SecretText = SecretText(CharArray(0))
    }
}

/** Wipes every non-null secret. */
fun wipeAll(vararg secrets: SecretText?) {
    for (secret in secrets) secret?.wipe()
}

object SecretTextSerializer : KSerializer<SecretText> {
    override val descriptor: SerialDescriptor = PrimitiveSerialDescriptor("phpvpn.SecretText", PrimitiveKind.STRING)

    override fun serialize(encoder: Encoder, value: SecretText) = encoder.encodeString(value.reveal())

    override fun deserialize(decoder: Decoder): SecretText = SecretText.of(decoder.decodeString())
}
