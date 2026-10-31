package phpvpn.android.bridge.model

import phpvpn.android.bridge.errors.ErrorTexts
import phpvpn.android.bridge.errors.ProgressErrorText
import phpvpn.android.bridge.ipc.CheckpointResult
import phpvpn.android.bridge.ipc.ConnectionPhase
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.HttpDiagnostic
import phpvpn.android.bridge.ipc.Tristate

enum class ProgressStage { CONNECTING, SUCCEEDED, FAILED }

/**
 * Port of `tray::ProgressView`. [errorText] holds the resource keys selected
 * by [ErrorTexts.progressErrorText]; [errorCode] is the allowlisted canonical
 * code (the C++ `errorDescription` literal is replaced by [errorText]).
 */
data class ProgressView(
    val stage: ProgressStage = ProgressStage.CONNECTING,
    val serverUrl: CheckpointResult = CheckpointResult.PENDING,
    val serverSecret: CheckpointResult = CheckpointResult.PENDING,
    val accessKey: CheckpointResult = CheckpointResult.PENDING,
    val communicationContracts: CheckpointResult = CheckpointResult.PENDING,
    val errorText: ProgressErrorText? = null,
    val errorCode: String = "",
    val statusUnavailable: Boolean = false,
    val closingSeconds: Int = 3,
    val closeRequested: Boolean = false,
)

/**
 * Kotlin port of `tray::ProgressModel` (apps/tray/progress_model.cpp): the
 * Connect progress (screen 04, success 01, failure 06) and, with
 * [disconnect] true, the Disconnect progress (07, unconfirmed 38). Pure state,
 * single owner, the same inputs give the same outputs as the C++ model.
 */
class ProgressModel(private val disconnect: Boolean = false) {
    var view: ProgressView = ProgressView()
        private set
    var httpDiagnostic: HttpDiagnostic? = null
        private set
    var connectionEpoch: ULong = 0uL
        private set
    var connectAttemptId: String? = null
        private set
    var successDeadlineMs: Long? = null
        private set
    private var diagnosticRetired = false

    val isDisconnect: Boolean get() = disconnect

    fun onSnapshot(status: TrayStatus, @Suppress("UNUSED_PARAMETER") nowMs: Long) {
        if (status.valid && connectionEpoch != 0uL && status.connectionEpoch == connectionEpoch &&
            connectAttemptId != status.connectAttemptId
        ) {
            httpDiagnostic = null
            view = view.copy(closeRequested = true)
            return
        }
        if (status.valid && connectionEpoch != 0uL && status.connectionEpoch < connectionEpoch) {
            // A restarted service may have a lower epoch. A foreign UUID can never
            // keep raw data alive, even when the frozen public error is retained.
            if (status.connectAttemptId != connectAttemptId) {
                httpDiagnostic = null
                diagnosticRetired = true
            }
            return
        }
        if (!disconnect) {
            if (!status.valid || view.closeRequested) {
                diagnosticRetired = diagnosticRetired || httpDiagnostic != null
                httpDiagnostic = null
            } else {
                if (status.connectionEpoch != connectionEpoch) diagnosticRetired = false
                if (connectionEpoch != 0uL && status.connectionEpoch != connectionEpoch && view.stage != ProgressStage.CONNECTING) {
                    view = view.copy(closeRequested = true)
                }
                connectionEpoch = status.connectionEpoch
                connectAttemptId = status.connectAttemptId
                if (httpDiagnostic != null && status.httpDiagnostic == null) diagnosticRetired = true
                httpDiagnostic = if (!diagnosticRetired && !view.closeRequested && status.state == ConnectionState.ERROR &&
                    status.phase == ConnectionPhase.IDLE && status.hasError && !status.errorRecoverable
                ) status.httpDiagnostic else null
            }
        }
        if (view.stage != ProgressStage.CONNECTING || view.closeRequested) return
        view = view.copy(statusUnavailable = !status.valid)
        if (!status.valid) return // Delivery failure is not a bootstrap result.
        if (disconnect) {
            // The reply can still carry the previous connection's error while the
            // accepted Disconnect waits for teardown. Keep observing the service.
            val failed = status.phase != ConnectionPhase.STOPPING &&
                (status.state == ConnectionState.ERROR || (status.state == ConnectionState.DISCONNECTED && status.hasError))
            view = view.copy(
                errorCode = if (failed) DISCONNECT_UNCONFIRMED else "",
                errorText = if (failed) ErrorTexts.disconnectUnconfirmed else null,
            )
            if (status.state == ConnectionState.DISCONNECTED && status.phase == ConnectionPhase.IDLE &&
                status.tunnelReady == Tristate.NO && !status.hasError
            ) {
                view = view.copy(stage = ProgressStage.SUCCEEDED)
            }
            return
        }
        view = view.copy(
            serverUrl = status.serverUrl,
            serverSecret = status.serverSecret,
            accessKey = status.accessKey,
            communicationContracts = status.communicationContracts,
        )
        if (status.healthyConnected) {
            // Checkpoints remain the exact four checkpoints from this same status snapshot.
            view = view.copy(stage = ProgressStage.SUCCEEDED)
        } else if (status.hasError || status.state == ConnectionState.ERROR) {
            val text = ErrorTexts.progressErrorText(status.errorCode)
            view = view.copy(stage = ProgressStage.FAILED, errorCode = text.code, errorText = text)
        } else if (status.state == ConnectionState.DISCONNECTED && status.phase == ConnectionPhase.IDLE) {
            view = view.copy(closeRequested = true)
        }
    }

    fun onCommandFailure() {
        if (!disconnect || view.stage != ProgressStage.CONNECTING || view.closeRequested) return
        view = view.copy(stage = ProgressStage.FAILED, errorCode = DISCONNECT_UNCONFIRMED, errorText = ErrorTexts.disconnectUnconfirmed)
    }

    /** Called once after the visible success heading has actually been drawn. */
    fun onSuccessPresented(nowMs: Long) {
        if (view.stage != ProgressStage.SUCCEEDED || view.closeRequested || successDeadlineMs != null || nowMs < 0) return
        successDeadlineMs = if (nowMs > Long.MAX_VALUE - SUCCESS_DISPLAY_MS) Long.MAX_VALUE else nowMs + SUCCESS_DISPLAY_MS
    }

    fun onTick(nowMs: Long) {
        val deadline = successDeadlineMs ?: return
        if (view.closeRequested) return
        if (nowMs >= deadline) {
            view = view.copy(closeRequested = true)
            return
        }
        val remaining = if (nowMs < deadline - SUCCESS_DISPLAY_MS) SUCCESS_DISPLAY_MS else deadline - nowMs
        view = view.copy(closingSeconds = ((remaining + 999) / 1000).toInt())
    }

    /** Closing is a local presentation action, with no command callback. */
    fun onClose() {
        view = view.copy(closeRequested = true)
        httpDiagnostic = null
    }

    companion object {
        const val SUCCESS_DISPLAY_MS = 3_000L
        const val DISCONNECT_UNCONFIRMED = "disconnect_unconfirmed"
    }
}
