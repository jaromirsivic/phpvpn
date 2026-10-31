package phpvpn.android.ui.state

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.errors.ErrorTextKey
import phpvpn.android.bridge.errors.ErrorTexts
import phpvpn.android.bridge.model.ProgressStage
import phpvpn.android.bridge.repository.CommandResult
import phpvpn.android.bridge.repository.ConnectionRepository
import phpvpn.android.bridge.repository.ConnectionStatus
import phpvpn.android.ui.components.CheckState

/** Everything the Connection destination draws. */
data class ConnectionScreenModel(
    val state: ConnectionUiState = ConnectionUiState.Loading,
    /** Public part of the configuration for the server card, null while unknown. */
    val config: ConfigSummary? = null,
    /** A Connect the service refused before any attempt started (02); text by code. */
    val refusedConnect: ErrorTextKey? = null,
    /** A Connect or Disconnect command is in flight; the buttons wait for it. */
    val commandPending: Boolean = false,
    /** 48: the four checks of the last status (live or last known), null before any status. */
    val lastChecks: List<CheckState>? = null,
    /** 48: the checks are from the last known status, the current one is unavailable. */
    val checksStale: Boolean = false,
)

/** One-shot instructions for the screen; navigation and the system dialog are UI concerns. */
sealed interface ConnectionEffect {
    /** Show the system consent dialog (03). */
    data object RequestConsent : ConnectionEffect

    /** 37: consent denied or missing. */
    data object PermissionDenied : ConnectionEffect

    /** 36: Connect was refused because of the stored configuration. */
    data object ConfigAttention : ConnectionEffect
}

/**
 * State holder of the Connection destination (01-08, 37, 38, 48).
 *
 * [screen] collects get-status only while the UI collects it (the screen
 * uses collectAsStateWithLifecycle, so polling stops below STARTED, plan
 * "Stav pro UI"). The 500 ms / 5 s schedule is the repository's.
 */
class ConnectionViewModel(
    private val connection: ConnectionRepository,
    private val actions: ConnectActions,
    private val consent: VpnConsent,
    private val events: ConnectionUiEvents,
    private val config: ConfigSummaryRepository,
    private val wallClock: () -> Long = System::currentTimeMillis,
) : ViewModel() {
    private val refused = MutableStateFlow<ErrorTextKey?>(null)
    private val pending = MutableStateFlow(false)
    private val effectChannel = Channel<ConnectionEffect>(Channel.BUFFERED)
    private var lastAvailable: ConnectionStatus.Available? = null
    private var lastAvailableAt: Long? = null

    val effects: Flow<ConnectionEffect> = effectChannel.receiveAsFlow()

    val screen: StateFlow<ConnectionScreenModel> = combine(
        connection.status,
        connection.progress,
        events.reason,
        config.state,
        combine(refused, pending) { r, p -> r to p },
    ) { status, progress, reason, summary, (refusedKey, busy) ->
        if (status is ConnectionStatus.Available && status != lastAvailable) {
            lastAvailable = status
            lastAvailableAt = wallClock()
        }
        val state = ConnectionStateMapper.map(status, progress, reason, lastAvailableAt)
        ConnectionScreenModel(
            state = state,
            config = (summary as? ConfigSummaryState.Loaded)?.summary,
            refusedConnect = refusedKey.takeIf { state is ConnectionUiState.Disconnected },
            commandPending = busy,
            lastChecks = ConnectionStateMapper.lastChecks(status),
            checksStale = status is ConnectionStatus.Unavailable,
        )
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(), ConnectionScreenModel())

    init {
        // A visible success is the confirmed status itself (01 or 02), so the
        // finished progress is closed at once and polling returns to 5 s.
        viewModelScope.launch {
            connection.progress.collect { progress ->
                if (progress != null && !progress.view.closeRequested && progress.view.stage == ProgressStage.SUCCEEDED) {
                    connection.closeProgress()
                }
            }
        }
        refreshConfig()
    }

    fun refreshConfig() {
        viewModelScope.launch { config.refresh() }
    }

    /** Connect VPN (02), Try again (06): consent first, never a server request without it. */
    fun connect() {
        if (pending.value) return
        refused.value = null
        events.clear()
        if (consent.isGranted()) startConnect() else effectChannel.trySend(ConnectionEffect.RequestConsent)
    }

    /** Result of the system dialog (03) or of [VpnConsent.request] when no dialog was needed. */
    fun onConsentResult(granted: Boolean) {
        if (granted) startConnect() else effectChannel.trySend(ConnectionEffect.PermissionDenied)
    }

    /** Disconnect (01, 05), Cancel connection (04), Try disconnecting again (38). */
    fun disconnect() {
        if (pending.value) return
        viewModelScope.launch {
            pending.value = true
            try {
                actions.disconnect()
            } finally {
                pending.value = false
            }
        }
    }

    /** Check status again (08). Any confirmed state may follow; nothing is started. */
    fun checkStatus() = connection.refresh()

    /** Check status (38): leaves the unconfirmed progress and shows what the service reports. */
    fun leaveUnconfirmed() {
        viewModelScope.launch {
            connection.closeProgress()
            connection.refresh()
        }
    }

    private fun startConnect() {
        if (pending.value) return
        viewModelScope.launch {
            pending.value = true
            val result = try {
                actions.connect()
            } finally {
                pending.value = false
            }
            if (result is CommandResult.Failed) handleRefusal(result.error.code)
            config.refresh()
        }
    }

    private fun handleRefusal(code: String) {
        val known = ErrorCode.fromWire(code)
        when {
            known == ErrorCode.TUNNEL_PERMISSION_MISSING -> effectChannel.trySend(ConnectionEffect.PermissionDenied)
            code.startsWith(CONFIG_PREFIX) || known == ErrorCode.CONTRACT_MISSING ->
                effectChannel.trySend(ConnectionEffect.ConfigAttention)
            else -> refused.value = ErrorTexts.messageKey(code)
        }
    }

    private companion object {
        const val CONFIG_PREFIX = "config_"
    }
}
