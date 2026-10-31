package phpvpn.android.service

import android.content.Context
import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants
import android.util.Log
import java.io.File
import java.io.FileDescriptor
import java.util.concurrent.Executors

/**
 * The R9 state file `noBackupFilesDir/phpvpn/state/intent.json` (plan step 9),
 * one per process. The memory copy is authoritative for this process; every
 * change is written in order on one background thread, atomically like
 * durable_file_posix: a 0600 temporary file in the same directory, fsync,
 * rename over the target and fsync of the directory. A crash leaves either the
 * old or the new file, never a mix. The directory belongs to the native
 * runtime (0700, WP04); it is created here only if the runtime never started.
 */
class IntentStore private constructor(private val directory: File) {
    private val file = File(directory, FILE_NAME)
    private val writer = Executors.newSingleThreadExecutor { runnable -> Thread(runnable, "phpvpn-intent").apply { isDaemon = true } }

    @Volatile
    private var current: IntentRecord = load()

    fun read(): IntentRecord = current

    /** Applies [change] to the memory copy and schedules the write when anything changed. */
    fun update(change: (IntentRecord) -> IntentRecord): IntentRecord {
        val next: IntentRecord
        synchronized(this) {
            val previous = current
            next = change(previous)
            if (next == previous) return next
            current = next
        }
        writer.execute { persist() }
        return next
    }

    private fun load(): IntentRecord = try {
        if (file.isFile) IntentRecordCodec.decode(file.readText(Charsets.UTF_8)) else IntentRecord()
    } catch (error: java.io.IOException) {
        Log.w(TAG, "the intent file could not be read; nothing is reconnected")
        IntentRecord()
    }

    private fun persist() {
        val bytes = IntentRecordCodec.encode(current).encodeToByteArray()
        val temporary = File(directory, "$FILE_NAME.tmp")
        try {
            if (!directory.isDirectory && !directory.mkdirs()) throw ErrnoException("mkdirs", OsConstants.ENOENT)
            val descriptor = Os.open(
                temporary.path,
                OsConstants.O_WRONLY or OsConstants.O_CREAT or OsConstants.O_TRUNC or OsConstants.O_CLOEXEC,
                PRIVATE_FILE_MODE,
            )
            try {
                var offset = 0
                while (offset < bytes.size) offset += Os.write(descriptor, bytes, offset, bytes.size - offset)
                Os.fsync(descriptor)
            } finally {
                Os.close(descriptor)
            }
            Os.rename(temporary.path, file.path)
            syncDirectory()
        } catch (error: ErrnoException) {
            // Only the errno name: the path holds nothing secret, but logs stay minimal.
            Log.w(TAG, "the intent file could not be written: ${OsConstants.errnoName(error.errno)}")
        }
    }

    private fun syncDirectory() {
        val descriptor: FileDescriptor = Os.open(directory.path, OsConstants.O_RDONLY or OsConstants.O_CLOEXEC, 0)
        try {
            Os.fsync(descriptor)
        } finally {
            Os.close(descriptor)
        }
    }

    companion object {
        private const val TAG = "phpvpn"
        private const val FILE_NAME = "intent.json"
        private const val PRIVATE_FILE_MODE = 0x180 // 0600

        @Volatile
        private var instance: IntentStore? = null

        fun get(context: Context): IntentStore = instance ?: synchronized(this) {
            instance ?: IntentStore(File(context.applicationContext.noBackupFilesDir, "phpvpn/state")).also { instance = it }
        }
    }
}
