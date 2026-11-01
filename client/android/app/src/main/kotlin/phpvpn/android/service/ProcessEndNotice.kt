package phpvpn.android.service

import android.util.Log
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import phpvpn.android.AppContainer
import phpvpn.android.bridge.repository.ConnectionStatus
import phpvpn.android.ui.state.DisconnectReason

/**
 * Runs [ProcessEndCheck] once per process start (plan step 9, WP05 finding):
 * the explanation on 02 when the previous process ended while connected and
 * Android did not restart the service.
 */
object ProcessEndNotice {
    fun schedule(container: AppContainer, intents: IntentStore) {
        if (!intents.read().wantConnected) return
        container.scope.launch {
            delay(ProcessEndCheck.GRACE_MS)
            if (VpnServiceCommands.running.value || !intents.read().wantConnected) return@launch
            val status = (container.connection.readOnce() as? ConnectionStatus.Available)?.status
            if (!ProcessEndCheck.endedWithTheProcess(intents.read(), VpnServiceCommands.running.value, status)) return@launch
            Log.i(TAG, "the previous process ended while connected and the service was not restarted")
            intents.update { IntentRecord(notice = DisconnectReason.PROCESS_RESTARTED) }
            container.connectionEvents.report(DisconnectReason.PROCESS_RESTARTED)
        }
    }

    private const val TAG = "phpvpn"
}
