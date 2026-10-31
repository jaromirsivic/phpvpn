package phpvpn.android.bridge.jni

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import phpvpn.android.bridge.NativeBridge
import phpvpn.android.bridge.ipc.IpcTransport

/** The blocking native exchange, `NativeBridge.nativeDispatch` in production. */
fun interface NativeDispatch {
    fun dispatch(request: ByteArray, timeoutMs: Int): ByteArray?
}

/**
 * [IpcTransport] over the in-process native server (plan-android-v001 point 8
 * and "Vlákna JNI z Kotlinu").
 *
 * `nativeDispatch` blocks until the reply or the caller's timeout, so every
 * call switches to [DISPATCHER], a `Dispatchers.IO` view limited to
 * [PARALLELISM] threads: never the main thread, and a 30 s Connect cannot
 * starve the 400 ms status polls. The caller's timeout is passed through
 * unchanged; the native side returns null when it passed. The request stays
 * owned (and wiped) by the caller, the reply belongs to the caller.
 */
class JniIpcTransport internal constructor(
    private val dispatcher: CoroutineDispatcher,
    private val native: NativeDispatch,
) : IpcTransport {
    constructor() : this(DISPATCHER, NativeDispatch { request, timeoutMs -> NativeBridge.nativeDispatch(request, timeoutMs) })

    override suspend fun dispatch(request: ByteArray, timeoutMs: Int): ByteArray? =
        withContext(dispatcher) { native.dispatch(request, timeoutMs) }

    companion object {
        /**
         * Concurrent blocking exchanges: enough for a long command, a Wizard
         * operation poll, a diagnostic request and the status polls at once;
         * the native executors bound the real work per class anyway.
         */
        const val PARALLELISM = 8

        /** The dedicated IO dispatcher of every native exchange. */
        val DISPATCHER: CoroutineDispatcher = Dispatchers.IO.limitedParallelism(PARALLELISM, "phpvpn-ipc")
    }
}
