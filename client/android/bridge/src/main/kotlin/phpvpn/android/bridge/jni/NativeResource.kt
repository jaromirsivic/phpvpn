package phpvpn.android.bridge.jni

import java.lang.ref.Cleaner
import phpvpn.android.bridge.errors.ErrorCode

/**
 * Owner of one native handle. Calls run under the handle's lock, so the single
 * owner models of the C++ side are never entered twice at once, and [close]
 * frees the handle exactly once. A handle the owner forgot is freed by the
 * cleaner after the wrapper became unreachable (the native destructors wipe
 * any secret); explicit [close] stays the rule.
 */
internal class NativeResource(handle: Long, release: (Long) -> Unit) : AutoCloseable {
    private val state = State(handle, release)
    private val cleanable = CLEANER.register(this, state)

    val isClosed: Boolean get() = synchronized(state) { state.handle == 0L }

    /** Runs [block] with the live handle; throws [IllegalStateException] after [close]. */
    fun <T> withHandle(block: (Long) -> T): T = synchronized(state) {
        val handle = state.handle
        check(handle != 0L) { "the native handle is closed" }
        block(handle)
    }

    override fun close() = cleanable.clean()

    private class State(var handle: Long, val release: (Long) -> Unit) : Runnable {
        override fun run() = synchronized(this) {
            if (handle != 0L) {
                release(handle)
                handle = 0L
            }
        }
    }

    private companion object {
        val CLEANER: Cleaner = Cleaner.create()
    }
}

/** Raised by a factory whose native model refused the input; [code] says why. */
class NativeModelException(val code: ErrorCode) : Exception("native model refused the input: ${code.wireName}")

/** The native error name (null = success) as an [ErrorCode]; an unknown name is internal. */
internal fun nativeError(name: ByteArray?): ErrorCode? = name?.let { ErrorCode.fromWire(it.decodeToString()) ?: ErrorCode.INTERNAL }

internal fun utf8(text: String): ByteArray = text.encodeToByteArray()

/** IPv4 text with exactly four decimal parts without leading zeros, as the C++ `Ipv4Address::parse`. */
internal object Ipv4Text {
    fun toInt(text: String): Int? {
        val parts = text.split('.')
        if (parts.size != 4) return null
        var value = 0L
        for (part in parts) {
            if (part.isEmpty() || part.length > 3 || !part.all { it in '0'..'9' }) return null
            if (part.length > 1 && part[0] == '0') return null
            val number = part.toInt()
            if (number > 255) return null
            value = (value shl 8) or number.toLong()
        }
        return value.toInt()
    }

    fun toText(value: Int): String {
        val bits = value.toLong() and 0xffffffffL
        return "${bits shr 24 and 0xff}.${bits shr 16 and 0xff}.${bits shr 8 and 0xff}.${bits and 0xff}"
    }
}
