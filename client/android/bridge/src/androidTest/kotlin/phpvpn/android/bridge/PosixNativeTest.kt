package phpvpn.android.bridge

import android.system.Os
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * libphpvpn-android-tests.so, built only for the debug variant
 * (plan-android-v001 T-N03). Strings cross as UTF-8 byte arrays, like
 * [NativeBridge].
 */
object NativeTestLibrary {
    init {
        System.loadLibrary("phpvpn-android-tests")
    }

    /**
     * Runs the native cases whose "suite.name" contains [filter] with TMPDIR
     * set to [directory]; returns the harness output ending in "exit <status>".
     */
    @JvmStatic
    external fun nativeRun(directory: ByteArray, filter: ByteArray): ByteArray
}

/**
 * T-N03: the POSIX configuration files (publication, recovery, the linkat
 * backup, renameat2, directory fsync), the anchored trusted walk and the
 * descriptor tunnel, run inside the application sandbox: application uid,
 * SELinux untrusted_app and the encrypted noBackupFilesDir. The adb shell user
 * has other SELinux rules and cannot stand in for the application.
 */
@RunWith(AndroidJUnit4::class)
class PosixNativeTest {
    @Test
    fun posixLayerPassesInsideTheApplicationSandbox() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val label = File("/proc/self/attr/current").readText().trim('\u0000', '\n', ' ')
        Log.i(TAG, "uid ${Os.getuid()}, SELinux $label, noBackupFilesDir ${context.noBackupFilesDir}")
        assertTrue("not an application uid: ${Os.getuid()}", Os.getuid() >= 10000)
        assertTrue("not in the application sandbox: $label", label.contains(":untrusted_app"))

        val directory = File(context.noBackupFilesDir, "native-tests")
        directory.deleteRecursively()
        assertTrue("cannot create $directory", directory.mkdirs())
        try {
            val output = NativeTestLibrary.nativeRun(
                directory.absolutePath.toByteArray(Charsets.UTF_8),
                ByteArray(0),
            ).toString(Charsets.UTF_8)
            // logcat truncates long entries, so one entry per line.
            output.lineSequence().forEach { Log.i(TAG, it) }
            val summary = SUMMARY.find(output)
            assertNotNull("no harness summary:\n$output", summary)
            val (executed, failed) = summary!!.destructured
            assertTrue("no native test ran:\n$output", executed.toInt() > 0)
            assertEquals("native failures:\n$output", 0, failed.toInt())
            assertTrue("harness exit status:\n$output", output.trimEnd().endsWith("exit 0"))
        } finally {
            directory.deleteRecursively()
        }
    }

    private companion object {
        const val TAG = "phpvpn-native-tests"
        val SUMMARY = Regex("""(\d+) test\(s\) run, (\d+) failed\.""")
    }
}
