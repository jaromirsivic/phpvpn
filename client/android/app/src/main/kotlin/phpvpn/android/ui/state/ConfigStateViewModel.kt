package phpvpn.android.ui.state

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.errors.ErrorTextKey
import phpvpn.android.bridge.errors.ErrorTexts
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.ConfigStateResult
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.IpcOutcome

/** The classification of screen 36, from `get-config-state` (client.md, table of availability). */
sealed interface AttentionUiState {
    data object Loading : AttentionUiState

    /** No file: the wizard is the primary action, "create default and edit" goes through 36a. */
    data class Missing(val configPath: String) : AttentionUiState

    /** unreadable, encoding_error, too_large: no repair offered, only the wizard (reset). */
    data class Unreadable(val configPath: String, val message: ErrorTextKey) : AttentionUiState

    /** invalid or repairable UTF-8 text: repair in the editor or start from defaults. */
    data class Invalid(val configPath: String, val reason: ErrorTextKey?) : AttentionUiState

    data class Valid(val configPath: String) : AttentionUiState

    /** The service did not answer; the text comes from the error code. */
    data class Unknown(val message: ErrorTextKey) : AttentionUiState

    companion object {
        fun from(outcome: IpcOutcome<ConfigStateResult>): AttentionUiState = when (outcome) {
            is IpcOutcome.Success -> from(outcome.value)
            is IpcOutcome.ServiceError -> Unknown(ErrorTexts.messageKey(outcome.code))
            IpcOutcome.TimedOut -> Unknown(ErrorCode.TIMEOUT.messageKey)
            is IpcOutcome.Rejected -> Unknown(outcome.code.messageKey)
        }

        fun from(result: ConfigStateResult): AttentionUiState = when (result.availability) {
            ConfigAvailability.MISSING -> Missing(result.configPath)
            ConfigAvailability.UNREADABLE -> Unreadable(result.configPath, ErrorCode.CONFIG_UNREADABLE.messageKey)
            ConfigAvailability.ENCODING_ERROR -> Unreadable(result.configPath, ErrorCode.CONFIG_ENCODING_ERROR.messageKey)
            ConfigAvailability.TOO_LARGE -> Unreadable(result.configPath, ErrorCode.CONFIG_TOO_LARGE.messageKey)
            ConfigAvailability.INVALID, ConfigAvailability.REPAIRABLE ->
                Invalid(result.configPath, result.reasonCode?.let { ErrorTexts.messageKey(it) })
            ConfigAvailability.VALID -> Valid(result.configPath)
        }
    }
}

/** Where "Open configuration editor" (00) and "Repair" lead after one get-config-state. */
sealed interface EditorEntry {
    /** 36a with the real path from the service (WP10 owns the dialog). */
    data class CreateDefault(val configPath: String) : EditorEntry

    /** A readable document: the editor itself. */
    data object Editor : EditorEntry

    /** Anything else: explain it on 36. */
    data object Attention : EditorEntry
}

/** State holder of 00 (editor entry) and 36 (classification). */
class ConfigStateViewModel(private val client: IpcClient, loadOnStart: Boolean) : ViewModel() {
    private val _state = MutableStateFlow<AttentionUiState>(AttentionUiState.Loading)
    val state: StateFlow<AttentionUiState> = _state.asStateFlow()

    init {
        if (loadOnStart) reload()
    }

    fun reload() {
        _state.value = AttentionUiState.Loading
        viewModelScope.launch { _state.value = AttentionUiState.from(client.getConfigState()) }
    }

    /** One fresh read decides the editor entry; a missing file never opens an empty editor. */
    suspend fun editorEntry(): EditorEntry {
        val next = AttentionUiState.from(client.getConfigState())
        _state.value = next
        return when (next) {
            is AttentionUiState.Missing -> EditorEntry.CreateDefault(next.configPath)
            is AttentionUiState.Invalid, is AttentionUiState.Valid -> EditorEntry.Editor
            else -> EditorEntry.Attention
        }
    }
}

/** Start destination decided once per activity (WP07 left `showWelcome` to WP08). */
enum class StartTarget { WELCOME, ATTENTION }

/**
 * Reads `get-config-state` once when the activity starts. Missing
 * configuration (no identity) shows Welcome (00) as the root; a stored but
 * unusable document opens 36 over Connection; anything else, including a
 * timeout, keeps Connection as the home.
 */
class StartDestinationViewModel(private val client: IpcClient) : ViewModel() {
    private var decided = false

    suspend fun decide(): StartTarget? {
        if (decided) return null
        decided = true
        val result = (client.getConfigState() as? IpcOutcome.Success)?.value ?: return null
        return when (result.availability) {
            ConfigAvailability.MISSING -> StartTarget.WELCOME
            ConfigAvailability.VALID -> null
            else -> StartTarget.ATTENTION
        }
    }
}
