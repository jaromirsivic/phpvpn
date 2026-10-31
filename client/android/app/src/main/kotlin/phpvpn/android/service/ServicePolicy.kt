package phpvpn.android.service

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.booleanOrNull
import phpvpn.android.bridge.ipc.ConnectionPhase
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.IpcTimeouts
import phpvpn.android.bridge.ipc.StrictJson
import phpvpn.android.bridge.model.TrayStatus
import phpvpn.android.ui.state.DisconnectReason

// Pure decisions of PhpvpnVpnService (plan-android-v001, "Životní cyklus
// připojení na Androidu" steps 4 to 9 and "Stav pro UI"), kept free of the
// Android framework so the JVM tests cover them.

/**
 * Content of `noBackupFilesDir/phpvpn/state/intent.json` (R9, step 9).
 * [wantConnected] is the user's intent, never a replacement for get-status;
 * [restartsElapsedMs] are the sticky restarts (SystemClock.elapsedRealtime)
 * that reconnected without a confirmed `connected` since; [notice] is the
 * local reason shown on 02 until the next Connect.
 */
data class IntentRecord(
    val wantConnected: Boolean = false,
    val restartsElapsedMs: List<Long> = emptyList(),
    val notice: DisconnectReason? = null,
)

/** Strict codec of [IntentRecord]; anything unreadable is the safe default (no reconnect). */
object IntentRecordCodec {
    const val VERSION = 1

    /** A record never holds more restarts than the policy can count. */
    private const val MAX_RESTARTS = 16

    @Serializable
    private data class FileV1(
        val version: Int,
        val wantConnected: Boolean,
        val restartsElapsedMs: List<Long>,
        val notice: Notice?,
    )

    @Serializable
    private enum class Notice {
        @SerialName("revoked") REVOKED,
        @SerialName("process_restarted") PROCESS_RESTARTED,
    }

    private val json = Json { encodeDefaults = true }

    fun encode(record: IntentRecord): String = json.encodeToString(
        FileV1.serializer(),
        FileV1(
            VERSION,
            record.wantConnected,
            record.restartsElapsedMs.takeLast(MAX_RESTARTS),
            when (record.notice) {
                DisconnectReason.REVOKED -> Notice.REVOKED
                DisconnectReason.PROCESS_RESTARTED -> Notice.PROCESS_RESTARTED
                null -> null
            },
        ),
    )

    fun decode(text: String): IntentRecord {
        // StrictJson first: duplicate members and quoted booleans or numbers
        // ("true", "1"), which the serializer would coerce, are refused.
        val root = StrictJson.parse(text) as? JsonObject ?: return IntentRecord()
        val strict = root.values.all { value ->
            when (value) {
                is JsonPrimitive -> !value.isString || value === root["notice"]
                is JsonArray -> value.all { it is JsonPrimitive && !it.isString }
                else -> value is JsonNull
            }
        }
        if (!strict) return IntentRecord()
        val file = try {
            json.decodeFromJsonElement(FileV1.serializer(), root)
        } catch (_: SerializationException) {
            return IntentRecord()
        } catch (_: IllegalArgumentException) {
            return IntentRecord()
        }
        if (file.version != VERSION || file.restartsElapsedMs.size > MAX_RESTARTS) return IntentRecord()
        return IntentRecord(
            wantConnected = file.wantConnected,
            restartsElapsedMs = file.restartsElapsedMs,
            notice = when (file.notice) {
                Notice.REVOKED -> DisconnectReason.REVOKED
                Notice.PROCESS_RESTARTED -> DisconnectReason.PROCESS_RESTARTED
                null -> null
            },
        )
    }
}

/** Why a sticky restart does not reconnect. */
enum class GiveUpReason {
    /** No `wantConnected`: nothing was connected, nothing is said on 02. */
    NOT_WANTED,

    /** The restart limit: three restarts without a confirmed `connected` in 10 minutes. */
    RESTART_LIMIT,

    /** `VpnService.prepare() != null`. */
    NO_CONSENT,

    /** get-config-state is not `valid` (or did not answer). */
    CONFIGURATION_NOT_VALID,
}

sealed interface RestartDecision {
    /** The record to store before anything else happens. */
    val record: IntentRecord

    data class Reconnect(override val record: IntentRecord) : RestartDecision

    data class GiveUp(val reason: GiveUpReason, override val record: IntentRecord) : RestartDecision
}

/**
 * R9: a sticky restart (empty intent after the process was killed) reconnects
 * only with the flag, the consent and a valid configuration, at most three
 * times in 10 minutes without a confirmed `connected`. The checks run in this
 * order and stop at the first failure, because `prepare()` takes the VPN slot
 * from another app and get-config-state costs an IPC exchange.
 */
object StickyRestartPolicy {
    const val RESTART_LIMIT = 3
    const val WINDOW_MS = 10 * 60 * 1000L

    suspend fun decide(
        record: IntentRecord,
        nowElapsedMs: Long,
        consentGranted: () -> Boolean,
        configurationValid: suspend () -> Boolean,
    ): RestartDecision {
        if (!record.wantConnected) return RestartDecision.GiveUp(GiveUpReason.NOT_WANTED, record)
        // Restarts after a reboot (elapsed time went back) or older than the window do not count.
        val recent = record.restartsElapsedMs.filter { it in (nowElapsedMs - WINDOW_MS)..nowElapsedMs }
        val reason = when {
            recent.size >= RESTART_LIMIT -> GiveUpReason.RESTART_LIMIT
            !consentGranted() -> GiveUpReason.NO_CONSENT
            !configurationValid() -> GiveUpReason.CONFIGURATION_NOT_VALID
            else -> return RestartDecision.Reconnect(record.copy(restartsElapsedMs = recent + nowElapsedMs, notice = null))
        }
        return RestartDecision.GiveUp(reason, IntentRecord(notice = DisconnectReason.PROCESS_RESTARTED))
    }
}

/** A command of the service itself, before its result is known. */
enum class LocalOperation { CONNECT, DISCONNECT }

/** What the service does after a status read. */
enum class ServiceAction {
    /** Keep the foreground service and its notification. */
    KEEP,

    /** Confirmed idle state (`disconnected` or a final `error`): remove the notification and stop. */
    STOP,
}

/**
 * Step 4 and point 7: the service stays in the foreground while the engine
 * works (connecting, connected, recovering, stopping). Only a confirmed idle
 * status stops it: `disconnected/idle`, or `error/idle`, where the engine has
 * given up and runs nothing (WP05 report, deviation). An unanswered or invalid
 * read and a command still in flight never stop it.
 */
object ServiceLifecycle {
    fun action(status: TrayStatus?, operation: LocalOperation?): ServiceAction {
        if (operation != null || status == null || !status.valid) return ServiceAction.KEEP
        val idle = status.phase == ConnectionPhase.IDLE
        return when {
            idle && status.state == ConnectionState.DISCONNECTED -> ServiceAction.STOP
            idle && status.state == ConnectionState.ERROR -> ServiceAction.STOP
            else -> ServiceAction.KEEP
        }
    }
}

/** The notification title (09); the text is the server and the virtual IP. */
enum class NotificationKind { CONNECTING, CONNECTED, RECONNECTING, DISCONNECTING, FAILED, UNAVAILABLE }

/** The three state icons of the notification (monochrome forms of assets/phpvpn-*.svg). */
enum class NotificationIcon { CONNECTED, CONNECTING, DISCONNECTED }

data class NotificationModel(val kind: NotificationKind) {
    /** Disconnect only for connected, connecting and reconnecting (09). */
    val disconnectAction: Boolean
        get() = kind == NotificationKind.CONNECTED || kind == NotificationKind.CONNECTING || kind == NotificationKind.RECONNECTING

    val icon: NotificationIcon
        get() = when (kind) {
            NotificationKind.CONNECTED -> NotificationIcon.CONNECTED
            NotificationKind.CONNECTING, NotificationKind.RECONNECTING, NotificationKind.DISCONNECTING -> NotificationIcon.CONNECTING
            NotificationKind.FAILED, NotificationKind.UNAVAILABLE -> NotificationIcon.DISCONNECTED
        }

    companion object {
        /**
         * The notification of [status] (null or invalid: the read failed).
         * While an own command runs, an idle status still shows that command
         * (the reply has not changed the state yet). Null: no notification
         * (confirmed `disconnected`).
         */
        fun of(status: TrayStatus?, operation: LocalOperation?): NotificationModel? {
            val kind = kindOf(status)
            if (kind == null || kind == NotificationKind.FAILED) {
                when (operation) {
                    LocalOperation.CONNECT -> return NotificationModel(NotificationKind.CONNECTING)
                    LocalOperation.DISCONNECT -> return NotificationModel(NotificationKind.DISCONNECTING)
                    null -> Unit
                }
            }
            return kind?.let(::NotificationModel)
        }

        private fun kindOf(status: TrayStatus?): NotificationKind? {
            if (status == null || !status.valid) return NotificationKind.UNAVAILABLE
            return when {
                status.phase == ConnectionPhase.STOPPING -> NotificationKind.DISCONNECTING
                status.healthyConnected -> NotificationKind.CONNECTED
                status.state == ConnectionState.CONNECTING -> NotificationKind.CONNECTING
                // Connected without a ready tunnel is not shown as connected (05).
                status.state == ConnectionState.RECOVERING || status.state == ConnectionState.CONNECTED -> NotificationKind.RECONNECTING
                status.state == ConnectionState.ERROR -> NotificationKind.FAILED
                status.phase == ConnectionPhase.IDLE -> null
                else -> NotificationKind.DISCONNECTING
            }
        }
    }
}

/**
 * The notification polling of the tray (poll_schedule.h, ipc_timeouts.h):
 * 500 ms while an own command runs, after it until the outcome is settled
 * (like the tray's progress window), and during transitions; otherwise 5 s.
 * No 1 s timer.
 */
object NotificationSchedule {
    /** The progress of an own command is followed fast for at most the command budget. */
    const val OWN_COMMAND_MS = IpcTimeouts.COMMAND_MS.toLong()

    fun delayMs(status: TrayStatus?, operation: LocalOperation?, followed: LocalOperation?): Long {
        val transitional = status?.valid == true && status.transitional
        return if (operation != null || followed != null || transitional) IpcTimeouts.PROGRESS_INTERVAL_MS else IpcTimeouts.STATUS_INTERVAL_MS
    }

    /**
     * The own command whose outcome is still followed fast, or null. A Connect
     * settles with a healthy connection or an idle state, a Disconnect only with
     * an idle state (its reply usually still says "connected"); both end at
     * [deadlineMs] at the latest. [operation] is the command still in flight.
     */
    fun follow(followed: LocalOperation?, status: TrayStatus?, operation: LocalOperation?, nowMs: Long, deadlineMs: Long): LocalOperation? {
        if (operation != null) return operation
        if (followed == null || nowMs >= deadlineMs) return null
        if (status == null || !status.valid) return followed
        val idle = status.phase == ConnectionPhase.IDLE &&
            (status.state == ConnectionState.DISCONNECTED || status.state == ConnectionState.ERROR)
        val settled = idle || (followed == LocalOperation.CONNECT && status.healthyConnected)
        return if (settled) null else followed
    }

    /** Rewrite the notification only when this key changes (state, phase, text). */
    data class Key(val model: NotificationModel?, val state: ConnectionState?, val phase: ConnectionPhase?, val text: String?)
}

/** `masterSetup.autoConnectOnStartup` for the boot receiver (step 7): the strict boolean of client.md, missing is false. */
object AutoConnectSetting {
    fun of(document: String): Boolean {
        val root = StrictJson.parse(document) as? JsonObject ?: return false
        val setup = root["masterSetup"] as? JsonObject ?: return false
        val value = setup["autoConnectOnStartup"] as? JsonPrimitive ?: return false
        return !value.isString && value.booleanOrNull == true
    }
}
