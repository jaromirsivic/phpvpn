package phpvpn.android.bridge

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeFalse
import org.junit.Test
import org.junit.runner.RunWith

/**
 * T-N02 and T-N04 inside the application sandbox (plan-android-v001 WP03):
 * the native Android platform cases (suites Android*, the same as on the build
 * host plus the BOOTTIME clock, FdTunnel, logcat and the CA store) and one
 * HTTPS exchange of the production curl transport with the system CA store,
 * as untrusted_app.
 */
@RunWith(AndroidJUnit4::class)
class AndroidPlatformNativeTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    @Test
    fun platformCasesPassInsideTheApplicationSandbox() {
        val directory = File(context.noBackupFilesDir, "platform-tests")
        directory.deleteRecursively()
        assertTrue(directory.mkdirs())
        try {
            val output = NativeTestLibrary.nativeRun(
                directory.absolutePath.toByteArray(Charsets.UTF_8),
                "Android".toByteArray(Charsets.UTF_8),
            ).toString(Charsets.UTF_8)
            output.lineSequence().forEach { Log.i(TAG, it) }
            val summary = SUMMARY.find(output)
            assertNotNull("no harness summary:\n$output", summary)
            val (executed, failed) = summary!!.destructured
            assertTrue("no native test ran:\n$output", executed.toInt() > 0)
            assertEquals("native failures:\n$output", 0, failed.toInt())
        } finally {
            directory.deleteRecursively()
        }
    }

    @Test
    fun systemCaStoreVerifiesHttpsInsideTheApplicationSandbox() {
        val store = VpnTestLibrary.text(VpnTestLibrary.tlsStoreInfo())
        Log.i(TAG, "CA store: $store")
        assumeFalse("built without PHPVPN_ANDROID_DEPS", store.startsWith("skipped"))
        assertTrue(store, store.startsWith("ok certificates="))
        assertTrue(store, store.contains("directoryIndex=0"))

        val cases = listOf(
            Triple("https://www.google.com/", false, true),
            Triple("https://www.google.com/", true, true),
            Triple("https://expired.badssl.com/", false, false),
            Triple("https://expired.badssl.com/", true, true),
            Triple("https://wrong.host.badssl.com/", false, false),
        )
        for ((url, trustAny, expectOk) in cases) {
            val result = VpnTestLibrary.text(VpnTestLibrary.tlsRequest(url.toByteArray(), trustAny))
            Log.i(TAG, "POST $url trustAny=$trustAny: $result")
            if (expectOk) {
                assertTrue("$url trustAny=$trustAny: $result", result.startsWith("ok status="))
            } else {
                assertTrue("$url trustAny=$trustAny: $result", result.startsWith("error transport_tls_failed"))
            }
        }
    }

    private companion object {
        const val TAG = "phpvpn-platform-tests"
        val SUMMARY = Regex("""(\d+) test\(s\) run, (\d+) failed\.""")
    }
}
