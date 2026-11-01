package phpvpn.android.service

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.net.VpnService
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import phpvpn.android.appContainer
import phpvpn.android.bridge.NativeBridge
import phpvpn.android.bridge.NativeRuntime
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.IpcOutcome

/**
 * `autoConnectOnStartup` on Android means "after the device started" (plan
 * step 7). The receiver connects only when the stored configuration is valid
 * and asks for it, and only when the VPN consent persists
 * (`VpnService.prepare() == null`); otherwise it only logs. This is never
 * Always-on, which Android itself starts through the service.
 */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_BOOT_COMPLETED) return
        val pending = goAsync()
        val application = context.applicationContext
        CoroutineScope(Dispatchers.IO).launch {
            try {
                onBoot(application)
            } finally {
                pending.finish()
            }
        }
    }

    private suspend fun onBoot(context: Context) {
        if (NativeRuntime.ensureStarted(context) != NativeBridge.START_OK) {
            Log.w(TAG, "boot: the native runtime did not start; no Connect")
            return
        }
        if (!autoConnectRequested(context)) {
            Log.i(TAG, "boot: autoConnectOnStartup is off or the configuration is not valid; no Connect")
            return
        }
        // prepare() also takes the slot from another VPN app; it is called only here and at Connect.
        if (VpnService.prepare(context) != null) {
            Log.i(TAG, "boot: autoConnectOnStartup is on, but the VPN consent is missing; no Connect")
            return
        }
        Log.i(TAG, "boot: autoConnectOnStartup is on, connecting")
        try {
            context.startForegroundService(PhpvpnVpnService.connectIntent(context))
        } catch (error: IllegalStateException) {
            Log.w(TAG, "boot: the VPN service could not be started: ${error.javaClass.simpleName}")
        }
    }

    /** `masterSetup.autoConnectOnStartup` of a valid document; the document is wiped at once. */
    private suspend fun autoConnectRequested(context: Context): Boolean {
        val outcome = context.appContainer.ipcClient.getConfigDocument()
        val result = (outcome as? IpcOutcome.Success)?.value ?: return false
        try {
            if (result.availability != ConfigAvailability.VALID) return false
            return result.document?.read { chars -> AutoConnectSetting.of(String(chars)) } == true
        } finally {
            result.wipe()
        }
    }

    private companion object {
        const val TAG = "phpvpn"
    }
}
