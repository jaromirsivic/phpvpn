package phpvpn.android.bridge.runtime

import android.system.Os
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlinx.serialization.json.JsonObject
import org.junit.AfterClass
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import phpvpn.android.bridge.NativeBridge
import phpvpn.android.bridge.NativeRuntime
import phpvpn.android.bridge.ipc.IdSource
import phpvpn.android.bridge.ipc.JsonChecks
import phpvpn.android.bridge.runtime.RuntimeHarness.fixture
import phpvpn.android.bridge.runtime.RuntimeHarness.json
import phpvpn.android.bridge.runtime.RuntimeHarness.plain
import phpvpn.android.bridge.runtime.RuntimeHarness.raw

/**
 * Lifecycle, storage and logging of the native runtime in the application
 * sandbox (WP04, plan points 8, 12 and 13): the private 0700 layout below
 * noBackupFilesDir with the anchored trust walk, the log file and logcat, the
 * status snapshot for the service notification, stop and start in one
 * process and the startup Connect that Android never arms.
 */
@RunWith(AndroidJUnit4::class)
class NativeRuntimeTest {
    @Test
    fun storageLayoutIsPrivateAndAnchoredAtNoBackupFilesDir() {
        val anchor = RuntimeHarness.context.noBackupFilesDir
        RuntimeHarness.restart { File(it, "run").deleteRecursively() }
        val anchorMode = Os.stat(anchor.path).st_mode and 0xfff
        Log.i(TAG, "noBackupFilesDir ${anchor.path} mode ${Integer.toOctalString(anchorMode)} uid ${Os.getuid()}")
        for (name in listOf("", "config", "state", "run", "logs")) {
            val directory = if (name.isEmpty()) RuntimeHarness.root else File(RuntimeHarness.root, name)
            assertTrue("$directory", directory.isDirectory)
            val stat = Os.lstat(directory.path)
            assertEquals("mode of phpvpn/$name", "700", Integer.toOctalString(stat.st_mode and 0xfff))
            assertEquals("owner of phpvpn/$name", Os.getuid(), stat.st_uid)
        }
        val config = Os.stat(RuntimeHarness.configFile.path)
        assertEquals("600", Integer.toOctalString(config.st_mode and 0xfff))
        assertEquals(Os.getuid(), config.st_uid)
        // The anchor itself is trusted as given and never changed.
        assertEquals(anchorMode, Os.stat(anchor.path).st_mode and 0xfff)
        assertTrue(File(RuntimeHarness.root, "state").list()!!.all { it == "ownership.json" })
    }

    @Test
    fun wrongModesAreRepairedAndALinkedDirectoryIsRefusedWithoutStoppingTheRuntime() {
        RuntimeHarness.restart { root ->
            File(root, "config").mkdirs()
            Os.chmod(File(root, "config").path, "755".toInt(8))
        }
        assertEquals("700", Integer.toOctalString(Os.stat(File(RuntimeHarness.root, "config").path).st_mode and 0xfff))

        val elsewhere = File(RuntimeHarness.context.noBackupFilesDir, "wp04-elsewhere")
        try {
            RuntimeHarness.restart { root ->
                elsewhere.mkdirs()
                Os.symlink(elsewhere.path, File(root, "config").path)
            }
            // The storage error is reported through IPC; the runtime keeps answering.
            assertNotNull(raw(plain("get-status"), 400))
            val state = raw(plain("get-config-state"))!!
            Log.i(TAG, "linked config directory: $state")
            assertTrue(state, state.contains("config_directory_security_invalid"))
            assertTrue(Os.lstat(File(RuntimeHarness.root, "config").path).st_mode and 0xf000 == 0xa000)
        } finally {
            NativeRuntime.stop()
            File(RuntimeHarness.root, "config").delete()
            elsewhere.deleteRecursively()
        }
    }

    @Test
    fun logFileAndLogcatCarryTheRuntimeRecordsWithoutSecrets() {
        RuntimeHarness.restart()
        val setId = IdSource.RANDOM.next()
        assertEquals(fixture("ack.json", setId), raw(RuntimeHarness.setConfig(RuntimeHarness.document(), setId), 30_000))
        val log = File(RuntimeHarness.root, "logs/phpvpn-service.log")
        assertTrue("$log", log.isFile)
        val text = log.readText()
        assertTrue(text.contains("[INFO] service: the service control IPC is ready"))
        assertTrue(text.contains("android: the native runtime is running"))
        val logcat = ProcessBuilder("logcat", "-d", "-s", "phpvpn:V").redirectErrorStream(true).start()
        val lines = logcat.inputStream.bufferedReader().readText()
        logcat.waitFor(10, TimeUnit.SECONDS)
        assertTrue("logcat lines of the runtime", lines.contains("service: the service control IPC is ready"))
        // Neither sink ever carries a secret of the configuration.
        for (secret in listOf("CHANGE_THIS_SERVER_SECRET", "0202020202020202020202020202020202020202020202020202020202020202")) {
            assertFalse(text.contains(secret))
            assertFalse(lines.contains(secret))
        }
    }

    @Test
    fun stopAndStartAgainInOneProcess() {
        RuntimeHarness.restart()
        assertNotNull(raw(plain("get-status"), 400))
        assertEquals(
            NativeBridge.START_ALREADY_STARTED,
            NativeBridge.nativeStart(RuntimeHarness.context.noBackupFilesDir.absolutePath.encodeToByteArray(), RuntimeHarness.context.packageName.encodeToByteArray()),
        )

        // A caller waiting for a slow diagnostic request wakes when the runtime stops.
        val setId = IdSource.RANDOM.next()
        assertEquals(fixture("ack.json", setId), raw(RuntimeHarness.setConfig(RuntimeHarness.document(RuntimeHarness.BLACKHOLE_URL), setId), 30_000))
        val pool = Executors.newSingleThreadExecutor()
        try {
            val pending = pool.submit<String?> { raw(plain("get-server-info"), 60_000) }
            Thread.sleep(300)
            val stopping = System.nanoTime()
            NativeRuntime.stop()
            Log.i(TAG, "stop with a pending request took ${(System.nanoTime() - stopping) / 1_000_000} ms")
            pending.get(30, TimeUnit.SECONDS)
        } finally {
            pool.shutdownNow()
        }
        assertNull(raw(plain("get-status"), 400))
        assertNull(NativeBridge.nativeStatus())
        NativeRuntime.stop()

        RuntimeHarness.restart(clean = false)
        val id = IdSource.RANDOM.next()
        assertEquals(id, JsonChecks.string(json(raw(plain("get-status", id), 400)!!)["requestId"]))
        val snapshot = json(NativeBridge.nativeStatus()!!.decodeToString())
        assertEquals(NativeBridge.STATUS_REQUEST_ID, JsonChecks.string(snapshot["requestId"]))
        RuntimeHarness.assertSameMembers(json(fixture("status-disconnected.json", NativeBridge.STATUS_REQUEST_ID)), snapshot, ordered = true)
    }

    @Test
    fun theStartupConnectIsNeverArmed() {
        RuntimeHarness.restart()
        val setId = IdSource.RANDOM.next()
        assertEquals(fixture("ack.json", setId), raw(RuntimeHarness.setConfig(RuntimeHarness.document(autoConnect = true), setId), 30_000))
        RuntimeHarness.restart(clean = false)
        // StartupConnect would post its Connect on the first tick (250 ms).
        Thread.sleep(2_000)
        val status = json(raw(plain("get-status"), 400)!!)["status"] as JsonObject
        assertEquals("disconnected", JsonChecks.string(status["state"]))
        assertEquals("idle", JsonChecks.string(status["phase"]))
        assertEquals(null, JsonChecks.string(status["connectAttemptId"]))
    }

    @Test
    fun invalidStartArgumentsAreRefused() {
        val packageName = RuntimeHarness.context.packageName.encodeToByteArray()
        for (directory in listOf("relative/path", "/data/../data", "/trailing/", "")) {
            assertEquals(directory, NativeBridge.START_INVALID_ARGUMENT, NativeBridge.nativeStart(directory.encodeToByteArray(), packageName))
        }
        val directory = RuntimeHarness.context.noBackupFilesDir.absolutePath.encodeToByteArray()
        for (name in listOf("", "a b", "a/b", "x".repeat(256))) {
            assertEquals(name, NativeBridge.START_INVALID_ARGUMENT, NativeBridge.nativeStart(directory, name.encodeToByteArray()))
        }
    }

    companion object {
        private const val TAG = "phpvpn-wp04-runtime"

        @JvmStatic
        @AfterClass
        fun stopRuntime() = NativeRuntime.stop()
    }
}
