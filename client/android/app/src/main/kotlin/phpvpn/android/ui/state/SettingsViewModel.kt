package phpvpn.android.ui.state

import androidx.lifecycle.ViewModel
import kotlinx.coroutines.flow.StateFlow
import phpvpn.android.bridge.repository.ConnectionRepository
import phpvpn.android.bridge.repository.ConnectionStatus

/** Where the Setup wizard entry of Settings leads. */
enum class WizardEntry {
    /** Confirmed disconnected: straight to the URL step (12). */
    URL,

    /** Connected, in transition or unknown: the disconnect gate first (11). */
    GATE,
}

/**
 * Settings hub (10). It reads the last status the Connection destination
 * polled instead of polling itself: the hub shows no live status, it only
 * routes the wizard through 11 unless the client is confirmed disconnected.
 */
class SettingsViewModel(
    private val connection: ConnectionRepository,
    alwaysOn: AlwaysOnStatus,
) : ViewModel() {
    val alwaysOn: StateFlow<AlwaysOnState?> = alwaysOn.state

    fun wizardEntry(): WizardEntry {
        val status = (connection.latest as? ConnectionStatus.Available)?.status
        return if (status != null && status.offline) WizardEntry.URL else WizardEntry.GATE
    }
}
