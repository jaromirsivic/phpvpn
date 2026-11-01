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
import phpvpn.android.bridge.NativeBridge
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
 * WP11 and the device tests add CONFIGURE_FILE (a whole document written by
 * the host, deleted after set-config), DIAGNOSTICS (public counters of
 * `get-diagnostics`) and STATUS_JSON (the get-status document).
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
                    ACTION_CONFIGURE_FILE -> configureFile(context, intent)
                    ACTION_DIAGNOSTICS -> diagnostics(context)
                    // WP11: the whole get-status document on one line (recoveryCount, lastError,
                    // counters); it holds no secret. Only while the runtime runs.
                    ACTION_STATUS_JSON -> log("status-json ${statusJson()}")
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

    /**
     * A whole configuration document (own virtual IP, a communication contract
     * with the peer) that a host script wrote with `adb shell run-as`. With the
     * extra `file` (WP11 peer tests) it is `filesDir/<file>`, without it
     * (device tests, client/tests/android/device-report-2026-11-01.md) it is
     * `cacheDir/device-test-config.json`. The file is overwritten with zeros
     * and deleted after reading; the document is never logged.
     */
    private suspend fun configureFile(context: Context, intent: Intent) {
        val name = intent.getStringExtra(EXTRA_FILE)
        val file = when {
            name == null -> java.io.File(context.cacheDir, CONFIG_FILE)
            name.matches(Regex("[A-Za-z0-9._-]+")) -> java.io.File(context.filesDir, name)
            else -> return log("configure-file failed: bad file name")
        }
        if (!file.isFile) return log("configure-file failed: no file")
        val bytes = file.readBytes()
        file.writeBytes(ByteArray(bytes.size))
        file.delete()
        val chars = String(bytes, Charsets.UTF_8).toCharArray()
        bytes.fill(0)
        val document = SecretText.adopt(chars)
        val outcome = context.appContainer.ipcClient.setConfig(document)
        document.wipe()
        log("configure-file ${if (outcome is IpcOutcome.Success) "ok" else "failed ${outcome.javaClass.simpleName} $outcome".take(400)} fileDeleted=${!file.exists()}")
    }

    /** Device test T-A20: the public recovery counters of `get-diagnostics` (no secret, no address). */
    private suspend fun diagnostics(context: Context) {
        when (val outcome = context.appContainer.ipcClient.getDiagnostics()) {
            is IpcOutcome.Success -> with(outcome.value) {
                log(
                    "diagnostics state=${state.name.lowercase()} phase=${phase.name.lowercase()} " +
                        "uptimeS=$serviceUptimeSeconds recoveryCount=$recoveryCount lastRecoveryAt=$lastRecoveryAt " +
                        "softRecoveryCount=$softRecoveryCount lastSoftRecoveryAt=$lastSoftRecoveryAt " +
                        "tunnel=${tunnelReady.name.lowercase()} polls=${counters.dataPollsTotal} " +
                        "pollsEmpty=${counters.dataPollsEmpty} pktSent=${counters.ipv4PacketsSent} " +
                        "pktRecv=${counters.ipv4PacketsReceived} dropped=${counters.packetsDropped} " +
                        "outExpired=${counters.outboundQueueExpiredPackets} inExpired=${counters.inboundQueueExpiredPackets} " +
                        "lastError=${lastError?.code ?: "none"}",
                )
            }
            else -> log("diagnostics failed ${outcome.javaClass.simpleName}")
        }
    }

    private fun statusJson(): String =
        NativeBridge.nativeStatus()?.decodeToString()?.lines()?.joinToString(" ") { it.trim() } ?: "unavailable"

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
        const val ACTION_CONFIGURE_FILE = "phpvpn.android.debug.CONFIGURE_FILE"
        const val EXTRA_FILE = "file"
        const val CONFIG_FILE = "device-test-config.json"
        const val ACTION_DIAGNOSTICS = "phpvpn.android.debug.DIAGNOSTICS"
        const val ACTION_STATUS_JSON = "phpvpn.android.debug.STATUS_JSON"
        const val EXTRA_URL = "url"
        const val EXTRA_AUTO_CONNECT = "autoConnect"
    }
}
