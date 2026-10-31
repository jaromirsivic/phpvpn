package phpvpn.android.bridge.model

/**
 * Port of `tray::ReadRequestQueue` (apps/tray/poll_schedule.h): explicit extra
 * reads queue behind a periodic read still in flight instead of being dropped.
 */
class ReadRequestQueue {
    private var pending = 0
    private var running = false

    /** True when the caller should start a read now. */
    fun request(): Boolean {
        if (running) {
            pending++
            return false
        }
        running = true
        return true
    }

    /** True when another queued read must run now. */
    fun finish(): Boolean {
        if (pending != 0) {
            pending--
            return true
        }
        running = false
        return false
    }

    fun stop() {
        pending = 0
        running = false
    }
}

/**
 * Port of `tray::PollSchedule`: fixed deadlines that extra reads never move.
 */
class PollSchedule(private val intervalMs: Long) {
    var nextMs: Long = 0
        private set
    var active: Boolean = false
        private set

    fun start(nowMs: Long) {
        active = true
        nextMs = nowMs + intervalMs
    }

    fun stop() {
        active = false
    }

    fun onTimer(nowMs: Long): Boolean {
        if (!active || nowMs < nextMs) return false
        nextMs += ((nowMs - nextMs) / intervalMs + 1) * intervalMs
        return true
    }
}
