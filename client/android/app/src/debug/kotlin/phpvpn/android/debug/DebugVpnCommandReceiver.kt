package phpvpn.android.debug

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.serialization.json.JsonPrimitive
import phpvpn.android.appContainer
import phpvpn.android.bridge.ipc.IpcOutcome
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.repository.CommandResult
import phpvpn.android.bridge.repository.ConnectionStatus

/**
 * Debug-only hook of the host-driven service tests (T-A10, T-A12, T-A16,
 * client/tests/android/service-lifecycle-tests.sh): `adb shell am broadcast
 * -n <package>/phpvpn.android.debug.DebugVpnCommandReceiver -a <action>`.
 * Connect and Disconnect take the production path (ConnectActions → VPN
 * service); results go to logcat with the tag `phpvpn-debug`, never a secret.
 * Release builds do not contain it.
 */
class DebugVpnCommandReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val pending = goAsync()
        val container = context.appContainer
        CoroutineScope(Dispatchers.Default).launch {
            try {
                when (intent.action) {
                    ACTION_CONNECT -> log("connect ${describe(container.connectActions.connect())}")
                    ACTION_DISCONNECT -> log("disconnect ${describe(container.connectActions.disconnect())}")
                    ACTION_STATUS -> log("status ${describe(container.connection.readOnce())}")
                    ACTION_CONFIGURE -> configure(context, intent)
                    else -> log("unknown action")
                }
            } finally {
                pending.finish()
            }
        }
    }

    /** The default document with `serverUrl` and `autoConnectOnStartup` replaced (the relay of the test). */
    private suspend fun configure(context: Context, intent: Intent) {
        val url = intent.getStringExtra(EXTRA_URL)
        val autoConnect = intent.getBooleanExtra(EXTRA_AUTO_CONNECT, false)
        val client = context.appContainer.ipcClient
        val default = (client.getDefaultConfig() as? IpcOutcome.Success)?.value?.document
        if (default == null || url == null) return log("configure failed: no default document or no url")
        val text = default.read { String(it) }
            .replace(Regex("\"serverUrl\"\\s*:\\s*\"[^\"]*\""), "\"serverUrl\": ${JsonPrimitive(url)}")
            .replace(Regex("\"autoConnectOnStartup\"\\s*:\\s*(true|false)"), "\"autoConnectOnStartup\": $autoConnect")
        default.wipe()
        val document = SecretText.adopt(text.toCharArray())
        val outcome = client.setConfig(document)
        document.wipe()
        log("configure ${if (outcome is IpcOutcome.Success) "ok" else "failed ${outcome.javaClass.simpleName}"} autoConnect=$autoConnect")
    }

    private fun describe(result: CommandResult): String = when (result) {
        is CommandResult.Accepted -> "accepted state=${result.status.state?.name?.lowercase()}"
        is CommandResult.Failed -> "failed ${result.error.code}"
        CommandResult.Unconfirmed -> "unconfirmed"
    }

    private fun describe(status: ConnectionStatus): String = when (status) {
        is ConnectionStatus.Available -> with(status.status) {
            "state=${state?.name?.lowercase()} phase=${phase?.name?.lowercase()} tunnel=${tunnelReady?.name?.lowercase()} error=${errorCode.ifEmpty { "none" }}"
        }
        else -> "unavailable"
    }

    private fun log(message: String) {
        Log.i(TAG, message)
    }

    private companion object {
        const val TAG = "phpvpn-debug"
        const val ACTION_CONNECT = "phpvpn.android.debug.CONNECT"
        const val ACTION_DISCONNECT = "phpvpn.android.debug.DISCONNECT"
        const val ACTION_STATUS = "phpvpn.android.debug.STATUS"
        const val ACTION_CONFIGURE = "phpvpn.android.debug.CONFIGURE"
        const val EXTRA_URL = "url"
        const val EXTRA_AUTO_CONNECT = "autoConnect"
    }
}
