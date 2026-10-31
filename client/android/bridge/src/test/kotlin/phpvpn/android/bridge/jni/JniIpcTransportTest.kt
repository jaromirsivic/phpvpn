package phpvpn.android.bridge.jni

import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Threading policy of [JniIpcTransport] (WP04, plan "Vlákna JNI z Kotlinu")
 * with the native call stubbed: the blocking call never runs on the caller's
 * thread, always on the limited IO dispatcher, at most [JniIpcTransport.PARALLELISM]
 * at once, and the caller's bytes and timeout reach it unchanged.
 */
class JniIpcTransportTest {
    @Test
    fun requestBytesAndTimeoutPassThroughUnchanged() = runBlocking {
        val request = """{"ipcVersion":1}""".encodeToByteArray()
        val reply = "reply".encodeToByteArray()
        var seenRequest: ByteArray? = null
        var seenTimeout = -1
        val transport = JniIpcTransport(JniIpcTransport.DISPATCHER) { bytes, timeoutMs ->
            seenRequest = bytes
            seenTimeout = timeoutMs
            reply
        }
        for (timeout in listOf(400, 5_000, 30_000)) {
            assertSame(reply, transport.dispatch(request, timeout))
            assertSame(request, seenRequest)
            assertEquals(timeout, seenTimeout)
        }
    }

    @Test
    fun noReplyStaysNoReply() = runBlocking {
        val transport = JniIpcTransport(JniIpcTransport.DISPATCHER) { _, _ -> null }
        assertNull(transport.dispatch(ByteArray(1), 400))
    }

    @Test
    fun theNativeCallNeverRunsOnTheCallingThread() {
        val threads = Collections.synchronizedList(mutableListOf<Thread>())
        val transport = JniIpcTransport(JniIpcTransport.DISPATCHER) { _, _ ->
            threads += Thread.currentThread()
            ByteArray(0)
        }
        // A single-threaded caller, like the main thread of the app.
        val caller = Thread.currentThread()
        runBlocking { transport.dispatch(ByteArray(1), 400) }
        runBlocking(Dispatchers.Default) { transport.dispatch(ByteArray(1), 400) }
        assertEquals(2, threads.size)
        for (thread in threads) {
            assertNotEquals(caller, thread)
            // Dispatchers.IO and its limited views run on the shared scheduler workers.
            assertTrue(thread.name, thread.name.startsWith("DefaultDispatcher-worker"))
        }
    }

    @Test
    fun blockingCallsAreLimitedToTheDedicatedParallelism() = runBlocking {
        val running = AtomicInteger()
        val peak = AtomicInteger()
        val release = CountDownLatch(1)
        val transport = JniIpcTransport(JniIpcTransport.DISPATCHER) { _, _ ->
            val now = running.incrementAndGet()
            peak.accumulateAndGet(now) { a, b -> maxOf(a, b) }
            release.await(5, TimeUnit.SECONDS)
            running.decrementAndGet()
            ByteArray(0)
        }
        val calls = List(JniIpcTransport.PARALLELISM * 3) { async(Dispatchers.Default) { transport.dispatch(ByteArray(1), 30_000) } }
        // Wait until the limit is reached, then release everything.
        withContext(Dispatchers.IO) {
            val deadline = System.nanoTime() + 5_000_000_000L
            while (running.get() < JniIpcTransport.PARALLELISM && System.nanoTime() < deadline) Thread.sleep(10)
            Thread.sleep(200)
        }
        assertEquals(JniIpcTransport.PARALLELISM, running.get())
        release.countDown()
        calls.awaitAll()
        assertEquals(JniIpcTransport.PARALLELISM, peak.get())
    }
}
