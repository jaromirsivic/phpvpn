package phpvpn.android.bridge.model

import kotlinx.serialization.json.JsonObject
import phpvpn.android.bridge.ipc.CheckpointResult
import phpvpn.android.bridge.ipc.ConnectionPhase
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.HttpDiagnostic
import phpvpn.android.bridge.ipc.IPC_MAX_MESSAGE_BYTES
import phpvpn.android.bridge.ipc.IpcJson
import phpvpn.android.bridge.ipc.StatusEnvelope
import phpvpn.android.bridge.ipc.StatusSnapshot
import phpvpn.android.bridge.ipc.StrictJson
import phpvpn.android.bridge.ipc.Tristate
import phpvpn.android.bridge.ipc.WireFormats
import phpvpn.android.bridge.ipc.WireValidation

/** The nine service counters of get-status, in the order of `kCounterFields`. */
data class TrafficCounters(
    val dataPollsTotal: ULong = 0uL,
    val dataPollsEmpty: ULong = 0uL,
    val dataPollsWithPayload: ULong = 0uL,
    val serializedDataRequestBodyBytes: ULong = 0uL,
    val serializedDataResponseBodyBytes: ULong = 0uL,
    val ipv4PacketsSent: ULong = 0uL,
    val ipv4PacketsReceived: ULong = 0uL,
    val ipv4BytesSent: ULong = 0uL,
    val ipv4BytesReceived: ULong = 0uL,
) {
    /** Value by `kCounterFields` index. */
    operator fun get(index: Int): ULong = when (index) {
        0 -> dataPollsTotal
        1 -> dataPollsEmpty
        2 -> dataPollsWithPayload
        3 -> serializedDataRequestBodyBytes
        4 -> serializedDataResponseBodyBytes
        5 -> ipv4PacketsSent
        6 -> ipv4PacketsReceived
        7 -> ipv4BytesSent
        8 -> ipv4BytesReceived
        else -> throw IndexOutOfBoundsException(index)
    }

    companion object {
        /** The wire keys of `kCounterFields`, in order. */
        val KEYS: List<String> = WireValidation.counterNames
    }
}

/**
 * Kotlin port of `tray::TrayStatus` (apps/tray/status_model.h). The string
 * members of the C++ struct are typed enums here; an invalid or absent reply is
 * `TrayStatus()` with [valid] false and null enums.
 */
data class TrayStatus(
    val connectionEpoch: ULong = 0uL,
    val connectAttemptId: String? = null,
    val httpDiagnostic: HttpDiagnostic? = null,
    val state: ConnectionState? = null,
    val phase: ConnectionPhase? = null,
    val serverUrl: CheckpointResult = CheckpointResult.PENDING,
    val serverSecret: CheckpointResult = CheckpointResult.PENDING,
    val accessKey: CheckpointResult = CheckpointResult.PENDING,
    val communicationContracts: CheckpointResult = CheckpointResult.PENDING,
    val tunnelReady: Tristate? = null,
    val hasError: Boolean = false,
    val errorCode: String = "",
    val errorDescription: String = "",
    val errorRecoverable: Boolean = false,
    val counters: TrafficCounters = TrafficCounters(),
    val connectionStartedAtUtcMs: Long? = null,
    val valid: Boolean = false,
) {
    /** The "connected" of the UI: confirmed, running, tunnel ready, no error. */
    val healthyConnected: Boolean
        get() = state == ConnectionState.CONNECTED && phase == ConnectionPhase.RUNNING && tunnelReady == Tristate.YES && !hasError

    /** A transition that the UI polls every 500 ms (connecting, recovering, reconnecting, stopping). */
    val transitional: Boolean
        get() = state == ConnectionState.CONNECTING || state == ConnectionState.RECOVERING ||
            phase == ConnectionPhase.RECONNECTING || phase == ConnectionPhase.STOPPING

    /** Confirmed idle disconnection (`state == disconnected && phase == idle`). */
    val offline: Boolean get() = state == ConnectionState.DISCONNECTED && phase == ConnectionPhase.IDLE

    companion object {
        /** Maps an already validated wire snapshot. */
        fun from(snapshot: StatusSnapshot): TrayStatus = TrayStatus(
            connectionEpoch = snapshot.connectionEpoch,
            connectAttemptId = snapshot.connectAttemptId,
            httpDiagnostic = snapshot.httpDiagnostic,
            state = snapshot.state,
            phase = snapshot.phase,
            serverUrl = snapshot.serverUrlCheck,
            serverSecret = snapshot.serverSecretCheck,
            accessKey = snapshot.accessKeyCheck,
            communicationContracts = snapshot.communicationContractsCheck,
            tunnelReady = snapshot.tunnelReady,
            hasError = snapshot.lastError != null,
            errorCode = snapshot.lastError?.code ?: "",
            errorDescription = snapshot.lastError?.description ?: "",
            errorRecoverable = snapshot.lastError?.recoverable ?: false,
            counters = TrafficCounters(
                snapshot.dataPollsTotal, snapshot.dataPollsEmpty, snapshot.dataPollsWithPayload,
                snapshot.serializedDataRequestBodyBytes, snapshot.serializedDataResponseBodyBytes,
                snapshot.ipv4PacketsSent, snapshot.ipv4PacketsReceived, snapshot.ipv4BytesSent, snapshot.ipv4BytesReceived,
            ),
            connectionStartedAtUtcMs = snapshot.connectionStartedAt?.let { WireFormats.parseIso8601(it) },
            valid = true,
        )
    }
}

/** Port of `tray::TrayIconState`: the three state indicators (notification, Connection). */
enum class ConnectionIndicator { DISCONNECTED, CONNECTING, CONNECTED }

object StatusModel {
    /** Port of `parseStatusResponse`: a complete v1 snapshot or an invalid [TrayStatus]. */
    fun parseStatusResponse(json: String): TrayStatus {
        if (json.toByteArray(Charsets.UTF_8).size > IPC_MAX_MESSAGE_BYTES) return TrayStatus()
        val root = StrictJson.parse(json) ?: return TrayStatus()
        if (!WireValidation.status(root)) return TrayStatus()
        return try {
            TrayStatus.from(IpcJson.decodeFromJsonElement(StatusEnvelope.serializer(), root as JsonObject).status)
        } catch (_: IllegalArgumentException) {
            TrayStatus()
        }
    }

    /** Port of `selectTrayIcon`. */
    fun selectIndicator(status: TrayStatus, unavailable: Boolean, localOperation: Boolean): ConnectionIndicator {
        if (unavailable || !status.valid) return ConnectionIndicator.DISCONNECTED
        if (localOperation || status.phase == ConnectionPhase.STOPPING || status.state == ConnectionState.CONNECTING ||
            status.state == ConnectionState.RECOVERING ||
            (status.state == ConnectionState.CONNECTED && status.phase == ConnectionPhase.RECONNECTING)
        ) return ConnectionIndicator.CONNECTING
        if (status.healthyConnected) return ConnectionIndicator.CONNECTED
        return ConnectionIndicator.DISCONNECTED
    }

    /** Port of `applyStatusSnapshot`: invalid replies never replace the last authoritative snapshot. */
    fun applyStatusSnapshot(current: TrayStatus, next: TrayStatus): TrayStatus? = if (next.valid) next else null
}
