package phpvpn.android.ui.state

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.VpnService
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import phpvpn.android.bridge.repository.CommandResult
import phpvpn.android.bridge.repository.ConnectionRepository

// Seams between the screens of WP08 and the VPN service of WP05
// (plan-android-v001, "Životní cyklus připojení na Androidu"). The screens only
// talk to these interfaces; WP05 replaces the defaults in PhpvpnApplication.

/**
 * System VPN consent (03). [isGranted] is `VpnService.prepare() == null`;
 * [request] starts the system dialog through [launcher] and returns false when
 * no dialog is needed. The result arrives in the launcher callback:
 * granted → Connect (04), denied → 37 (never a server error, step 1).
 */
interface VpnConsent {
    fun isGranted(): Boolean

    fun request(launcher: ActivityResultLauncher<Intent>): Boolean
}

/** The real consent over `VpnService.prepare`. */
class SystemVpnConsent(private val context: Context) : VpnConsent {
    override fun isGranted(): Boolean = VpnService.prepare(context) == null

    override fun request(launcher: ActivityResultLauncher<Intent>): Boolean {
        val intent = VpnService.prepare(context) ?: return false
        launcher.launch(intent)
        return true
    }
}

/** Launcher for [VpnConsent.request]; [onResult] receives true when Android granted the VPN. */
@Composable
fun rememberVpnConsentLauncher(onResult: (Boolean) -> Unit): ActivityResultLauncher<Intent> {
    val callback by rememberUpdatedState(onResult)
    return rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) {
        callback(it.resultCode == Activity.RESULT_OK)
    }
}

/**
 * Connect and Disconnect of the UI. The default goes straight to the
 * [ConnectionRepository]; WP05 wraps it so that Connect first starts the
 * foreground service (`startForegroundService(ACTION_CONNECT)`, step 1) and a
 * confirmed Disconnect stops it (step 4). Results keep the repository meaning:
 * a timeout is `Unconfirmed`, never success.
 */
interface ConnectActions {
    suspend fun connect(): CommandResult

    suspend fun disconnect(): CommandResult
}

class RepositoryConnectActions(private val connection: ConnectionRepository) : ConnectActions {
    override suspend fun connect(): CommandResult = connection.connect()

    override suspend fun disconnect(): CommandResult = connection.disconnect()
}

/** `VpnService.isAlwaysOn()` and `isLockdownEnabled()` as last reported by the running service. */
data class AlwaysOnState(val alwaysOn: Boolean, val lockdown: Boolean)

/**
 * Always-on status for Settings (10) and About (34), plan step 6. Only a
 * running VpnService can ask Android, so WP05 publishes the values from the
 * service; null means "not reported" and is shown as such, never as Off.
 */
interface AlwaysOnStatus {
    val state: StateFlow<AlwaysOnState?>

    companion object {
        /** Nothing reported yet (no service in WP08). */
        val UNKNOWN: AlwaysOnStatus = object : AlwaysOnStatus {
            override val state: StateFlow<AlwaysOnState?> = MutableStateFlow(null)
        }
    }
}

/** Why the last connection ended without the user's Disconnect (02). */
enum class DisconnectReason {
    /** `onRevoke()`: another VPN app took the slot (step 5). */
    REVOKED,

    /** R9: the process was killed and the service did not reconnect (step 9). */
    PROCESS_RESTARTED,
}

/**
 * Local Android facts the IPC status cannot carry. WP05 reports them from the
 * service; the Disconnected screen (02) explains them until the next Connect.
 * The flag never replaces get-status: it is shown only while the status
 * confirms the disconnected state.
 */
class ConnectionUiEvents {
    private val _reason = MutableStateFlow<DisconnectReason?>(null)
    val reason: StateFlow<DisconnectReason?> = _reason.asStateFlow()

    fun report(reason: DisconnectReason) {
        _reason.value = reason
    }

    fun clear() {
        _reason.value = null
    }
}
