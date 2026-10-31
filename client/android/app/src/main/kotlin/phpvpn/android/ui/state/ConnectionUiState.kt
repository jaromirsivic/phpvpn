package phpvpn.android.ui.state

import phpvpn.android.bridge.errors.ConnectionErrorCode
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.errors.ErrorTextKey
import phpvpn.android.bridge.errors.ErrorTexts
import phpvpn.android.bridge.errors.ProgressErrorText
import phpvpn.android.bridge.ipc.CheckpointResult
import phpvpn.android.bridge.ipc.ConnectionPhase
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.model.ProgressStage
import phpvpn.android.bridge.model.TrafficCounters
import phpvpn.android.bridge.model.TrayStatus
import phpvpn.android.bridge.repository.ConnectionStatus
import phpvpn.android.bridge.repository.OperationProgress
import phpvpn.android.ui.components.CheckState

/** The last confirmed status before the service stopped answering (08), and when it was seen. */
data class LastKnown(val status: TrayStatus, val seenAtUtcMs: Long)

/**
 * The variant the Connection destination renders. The states 01 to 08, 38 and
 * the permission error are one route (`connection`), chosen from the status
 * snapshot and the own Connect/Disconnect progress (android-screen-map.md).
 */
sealed interface ConnectionUiState {
    /** Before the first read nothing is claimed, not even "disconnected". */
    data object Loading : ConnectionUiState

    /** 01: confirmed, running, tunnel ready, no error. */
    data class Connected(val startedAtUtcMs: Long?, val counters: TrafficCounters) : ConnectionUiState

    /** 02, with the local reason when Android ended the connection (onRevoke, R9). */
    data class Disconnected(val reason: DisconnectReason?) : ConnectionUiState

    /** 04: the four checks; [statusUnavailable] when the last read during the attempt failed. */
    data class Connecting(val checks: List<CheckState>, val statusUnavailable: Boolean) : ConnectionUiState

    /** 05: connected + reconnecting or recovering; never shown as connected. */
    data class Reconnecting(val lastError: ErrorTextKey?) : ConnectionUiState

    /**
     * 06. [error] comes only from the allowlist of progressErrorText, never
     * from a received description. [inputError] separates rejected input
     * (secret, access key, contracts, version) from transport failures.
     * [diagnostic] is true only when the client really captured HTTP data.
     */
    data class Failed(
        val error: ProgressErrorText,
        val inputError: Boolean,
        val checks: List<CheckState>,
        val diagnostic: Boolean,
    ) : ConnectionUiState

    /** 07: Disconnect accepted or stopping, waiting for confirmation. */
    data object Disconnecting : ConnectionUiState

    /** 08: get-status did not answer; neither state is confirmed. */
    data class Unavailable(val lastKnown: LastKnown?) : ConnectionUiState

    /** 38: the own Disconnect was not confirmed. */
    data object DisconnectUnconfirmed : ConnectionUiState

    /** `tunnel_permission_missing`: the engine needed establish() without consent; shown as 37. */
    data object PermissionMissing : ConnectionUiState
}

/** Pure mapping of repository state to [ConnectionUiState] (JVM tested). */
object ConnectionStateMapper {
    /** Errors that mean the configured input was rejected, not that the transport failed (06). */
    val INPUT_ERRORS: Set<String> = setOf(
        ConnectionErrorCode.SERVER_VERIFICATION_FAILED,
        ConnectionErrorCode.ACCESS_KEY_REJECTED,
        ConnectionErrorCode.COMMUNICATION_CONTRACTS_VERIFICATION_FAILED,
        ConnectionErrorCode.SERVER_VERSION_INCOMPATIBLE,
    ).map { it.wireName }.toSet()

    private val PERMISSION_MISSING = ErrorCode.TUNNEL_PERMISSION_MISSING.wireName

    fun map(
        status: ConnectionStatus,
        progress: OperationProgress?,
        reason: DisconnectReason?,
        lastKnownAtUtcMs: Long?,
    ): ConnectionUiState {
        val current = (status as? ConnectionStatus.Available)?.status
        // The permission error is local to Android; the progress allowlist
        // would turn it into communication_failed, so it is checked first.
        if (current != null && current.errorCode == PERMISSION_MISSING &&
            (current.hasError || current.state == ConnectionState.ERROR)
        ) return ConnectionUiState.PermissionMissing

        val open = progress?.takeUnless { it.view.closeRequested }
        if (open != null) {
            val view = open.view
            if (open.disconnect) {
                // Desktop rule (progress_window.cpp): failure, an unavailable
                // read or a disconnect error is "Disconnect not confirmed".
                if (view.stage == ProgressStage.FAILED || view.statusUnavailable || view.errorCode.isNotEmpty()) {
                    return ConnectionUiState.DisconnectUnconfirmed
                }
                if (view.stage == ProgressStage.CONNECTING) return ConnectionUiState.Disconnecting
            } else {
                val results = listOf(view.serverUrl, view.serverSecret, view.accessKey, view.communicationContracts)
                when (view.stage) {
                    ProgressStage.CONNECTING -> return ConnectionUiState.Connecting(checks(results, settled = false), view.statusUnavailable)
                    ProgressStage.FAILED -> {
                        val text = view.errorText ?: ErrorTexts.progressErrorText(view.errorCode)
                        return ConnectionUiState.Failed(
                            text,
                            text.code in INPUT_ERRORS,
                            checks(results, settled = true),
                            diagnostic = open.httpDiagnostic != null,
                        )
                    }
                    ProgressStage.SUCCEEDED -> Unit // the confirmed status below is the success (01)
                }
            }
        }
        return fromStatus(status, reason, lastKnownAtUtcMs)
    }

    private fun fromStatus(status: ConnectionStatus, reason: DisconnectReason?, lastKnownAtUtcMs: Long?): ConnectionUiState =
        when (status) {
            ConnectionStatus.Loading -> ConnectionUiState.Loading
            is ConnectionStatus.Unavailable -> ConnectionUiState.Unavailable(
                status.lastKnown?.let { LastKnown(it, lastKnownAtUtcMs ?: it.connectionStartedAtUtcMs ?: 0L) },
            )
            is ConnectionStatus.Available -> fromSnapshot(status.status, reason)
        }

    private fun fromSnapshot(s: TrayStatus, reason: DisconnectReason?): ConnectionUiState {
        val results = listOf(s.serverUrl, s.serverSecret, s.accessKey, s.communicationContracts)
        return when {
            s.phase == ConnectionPhase.STOPPING -> ConnectionUiState.Disconnecting
            s.healthyConnected -> ConnectionUiState.Connected(s.connectionStartedAtUtcMs, s.counters)
            s.state == ConnectionState.CONNECTING -> ConnectionUiState.Connecting(checks(results, settled = false), false)
            // Connected without a ready tunnel, reconnecting or recovering is
            // not a confirmed connection (05, status_snapshot.h).
            s.state == ConnectionState.RECOVERING || s.state == ConnectionState.CONNECTED ->
                ConnectionUiState.Reconnecting(if (s.hasError) ErrorTexts.messageKey(s.errorCode) else null)
            s.state == ConnectionState.ERROR -> {
                val text = ErrorTexts.progressErrorText(s.errorCode)
                ConnectionUiState.Failed(
                    text,
                    text.code in INPUT_ERRORS,
                    checks(results, settled = true),
                    // Same condition as ProgressModel: raw data only for a
                    // final, non-recoverable error of this snapshot.
                    diagnostic = s.httpDiagnostic != null && s.phase == ConnectionPhase.IDLE && s.hasError && !s.errorRecoverable,
                )
            }
            else -> ConnectionUiState.Disconnected(reason)
        }
    }

    /**
     * The four checks as the design shows them: verified, failed, the first
     * open check "verifying" while the attempt runs, the rest "waiting". A
     * pending check is never shown as passed.
     */
    fun checks(results: List<CheckpointResult>, settled: Boolean): List<CheckState> {
        val anyFailed = results.any { it == CheckpointResult.FAILED }
        var activeShown = settled || anyFailed
        return results.map { result ->
            when (result) {
                CheckpointResult.SUCCESS -> CheckState.Done
                CheckpointResult.FAILED -> CheckState.Fail
                CheckpointResult.PENDING -> if (!activeShown) {
                    activeShown = true
                    CheckState.Pending
                } else {
                    CheckState.Off
                }
            }
        }
    }

    /** 48: the checks of the last connection from the status, or null without any status. */
    fun lastChecks(status: ConnectionStatus): List<CheckState>? {
        val s = when (status) {
            is ConnectionStatus.Available -> status.status
            is ConnectionStatus.Unavailable -> status.lastKnown
            ConnectionStatus.Loading -> null
        } ?: return null
        val results = listOf(s.serverUrl, s.serverSecret, s.accessKey, s.communicationContracts)
        return checks(results, settled = s.state != ConnectionState.CONNECTING)
    }
}
