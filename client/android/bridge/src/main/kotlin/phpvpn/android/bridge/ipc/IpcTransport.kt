package phpvpn.android.bridge.ipc

/**
 * Budgets and polling intervals in milliseconds, the values of
 * client/apps/tray/ipc_timeouts.h so Android keeps the desktop timing.
 */
object IpcTimeouts {
    /** Read-only get-status of the UI, the progress and the Wizard gate. */
    const val STATUS_REQUEST_MS = 400

    /** Ordinary commands (connect, disconnect, get-info, ...) and set-config. */
    const val COMMAND_MS = 30_000

    /** One Wizard or configuration IPC request. */
    const val WIZARD_REQUEST_MS = 5_000

    /** Best effort wizard-close while the Wizard ends. */
    const val WIZARD_CLOSE_MS = 1_000

    /** Editor reset (get-default-config). */
    const val EDITOR_RESET_MS = 5_000

    /** Absolute deadlines of an editor and a contracts save. */
    const val EDITOR_SAVE_DEADLINE_MS = 30_000
    const val CONTRACTS_SAVE_DEADLINE_MS = 30_000

    /** Base status polling and the fast polling of a transition. */
    const val STATUS_INTERVAL_MS = 5_000L
    const val PROGRESS_INTERVAL_MS = 500L

    /** Server information automatic refresh period. */
    const val SERVER_INFO_AUTO_REFRESH_MS = 5_000L

    /** Wizard lease, keepalive cadence and operation polling (wizard_types.h, tray). */
    const val WIZARD_LEASE_MS = 60_000L
    const val WIZARD_KEEPALIVE_MS = 15_000L
    const val WIZARD_OPERATION_POLL_MS = 500L

    /** Finish receipt budget of the tray (kCommitBudgetMs). */
    const val WIZARD_COMMIT_BUDGET_MS = 15_000L
}

/**
 * The single byte-level channel to the in-process IPC v1 dispatcher.
 *
 * WP04 implements it over JNI with `NativeBridge.nativeDispatch(request, timeoutMs)`,
 * which hands the request to `IpcExecutors` and blocks until the reply or the
 * deadline. That call blocks the calling thread, so the real implementation
 * MUST switch to its own limited `Dispatchers.IO` dispatcher (`withContext`) and
 * MUST NEVER run on the main thread; the debug StrictMode policy of the app
 * makes a violation fail immediately.
 *
 * Contract:
 * - [request] is the complete UTF-8 JSON request. The caller wipes it after the
 *   call returns; the transport must not keep a reference.
 * - [timeoutMs] is the caller's whole budget for this exchange.
 * - The result is the exact UTF-8 reply bytes, owned by the caller, which wipes
 *   them after parsing. `null` means the deadline passed, the runtime is
 *   shutting down or the request could not be delivered: the outcome is
 *   unknown, never a success ("Status unavailable", screen 08).
 */
fun interface IpcTransport {
    suspend fun dispatch(request: ByteArray, timeoutMs: Int): ByteArray?
}

/** Source of fresh request, attempt and commit identifiers (UUIDv4, lowercase). */
fun interface IdSource {
    fun next(): String

    companion object {
        /** java.util.UUID.randomUUID uses SecureRandom and yields lowercase v4. */
        val RANDOM: IdSource = IdSource { java.util.UUID.randomUUID().toString() }
    }
}

/** Monotonic milliseconds, injectable for virtual-time tests. */
fun interface MonotonicClock {
    fun nowMs(): Long

    companion object {
        val SYSTEM: MonotonicClock = MonotonicClock { System.nanoTime() / 1_000_000 }
    }
}
