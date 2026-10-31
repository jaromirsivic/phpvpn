package phpvpn.android.bridge.runtime

import android.content.Context
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.net.InetSocketAddress
import java.net.Socket
import java.net.URI
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import phpvpn.android.bridge.NativeBridge
import phpvpn.android.bridge.NativeRuntime
import phpvpn.android.bridge.ipc.IdSource
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.StrictJson
import phpvpn.android.bridge.jni.JniIpcTransport
import phpvpn.android.bridge.jni.NativeConfigDocumentCheck

/**
 * The native runtime of the test APK (WP04): storage under the test
 * package's noBackupFilesDir, raw `nativeDispatch` and the WP06 client over
 * the real JNI transport. Fixtures are the WP06 files copied to
 * src/androidTest/resources/ipc.
 */
object RuntimeHarness {
    val context: Context get() = InstrumentationRegistry.getInstrumentation().targetContext
    val root: File get() = File(context.noBackupFilesDir, "phpvpn")
    val configFile: File get() = File(root, "config/config.json")

    /**
     * Relay of the Connect and Wizard scenarios: a wizard-default deployment
     * (tools/deploy-test.ps1 -Profile wizard-default) served by `php -S` on the
     * host behind a TCP proxy on port 18096 that half-closes 200 ms after the
     * reply (php -S closes at once, and through the emulator network a reply
     * then lost its final byte now and then). The emulator reaches the host as
     * 10.0.2.2. Override with the instrumentation argument `phpvpnServerUrl`.
     */
    val serverUrl: String
        get() = InstrumentationRegistry.getArguments().getString("phpvpnServerUrl") ?: "http://10.0.2.2:18096/phpvpn.php"

    /** A routable address that never answers: transport requests stay pending until their deadline. */
    const val BLACKHOLE_URL = "http://10.255.255.1/phpvpn.php"

    /**
     * Stops the runtime, removes the configuration and the journal (the log
     * directory stays: the logger keeps its file for the whole process) and
     * starts it again.
     */
    fun restart(clean: Boolean = true, prepare: (File) -> Unit = {}) {
        NativeRuntime.stop()
        if (clean) {
            File(root, "config").deleteRecursively()
            File(root, "state").deleteRecursively()
        }
        prepare(root)
        assertEquals(NativeBridge.START_OK, NativeRuntime.ensureStarted(context))
    }

    fun raw(json: String, timeoutMs: Int = 5_000): String? =
        NativeBridge.nativeDispatch(json.encodeToByteArray(), timeoutMs)?.decodeToString()

    fun plain(command: String, id: String = IdSource.RANDOM.next()) = """{"ipcVersion":1,"requestId":"$id","command":"$command"}"""

    fun client(): IpcClient = IpcClient(JniIpcTransport(), IdSource.RANDOM, NativeConfigDocumentCheck)

    fun fixture(name: String): String {
        val stream = RuntimeHarness::class.java.getResourceAsStream("/ipc/$name") ?: error("missing fixture $name")
        return stream.use { it.readBytes().toString(Charsets.UTF_8) }
    }

    fun fixture(name: String, requestId: String): String = fixture(name).replace("\$ID", requestId)

    fun json(text: String): JsonObject = StrictJson.parse(text) as JsonObject

    /** The default configuration document (config.h) pointing at [url]. */
    fun document(url: String = serverUrl, autoConnect: Boolean = false): String = fixture("default-config-document.json")
        .replace("\"http://127.0.0.1/phpvpn.php\"", "\"$url\"")
        .replace("\"autoConnectOnStartup\": false", "\"autoConnectOnStartup\": $autoConnect")

    /** A set-config request carrying [document] as its JSON string. */
    fun setConfig(document: String, id: String = IdSource.RANDOM.next()): String =
        """{"ipcVersion":1,"requestId":"$id","command":"set-config","config":${JsonPrimitive(document)}}"""

    fun relayReachable(): Boolean {
        val uri = URI(serverUrl)
        return try {
            Socket().use { it.connect(InetSocketAddress(uri.host, if (uri.port > 0) uri.port else 80), 2_000) }
            true
        } catch (_: Exception) {
            false
        }
    }

    /**
     * The member names of [actual] equal those of the desktop fixture
     * [expected], recursively wherever both sides hold an object; values may
     * differ (times, identifiers, counters). [ordered] also compares the order,
     * for fixtures taken from the C++ builders (get-status); the hand-written
     * Wizard family fixtures of WP06 are compared as sets.
     */
    fun assertSameMembers(expected: JsonElement?, actual: JsonElement?, path: String = "$", ordered: Boolean = false) {
        if (expected !is JsonObject || actual !is JsonObject) return
        if (ordered) assertEquals("members of $path", expected.keys.toList(), actual.keys.toList())
        else assertEquals("members of $path", expected.keys, actual.keys)
        for (key in expected.keys) assertSameMembers(expected[key], actual[key], "$path.$key", ordered)
    }

    fun waitUntil(timeoutMs: Long, message: String, condition: () -> Boolean) {
        val deadline = System.nanoTime() + timeoutMs * 1_000_000
        while (!condition()) {
            assertTrue("timed out: $message", System.nanoTime() < deadline)
            Thread.sleep(100)
        }
    }
}
