package phpvpn.android.service

import android.app.Activity
import android.content.Intent
import android.net.VpnService
import android.os.Bundle
import android.os.ParcelFileDescriptor
import android.util.Log

/**
 * "Another VPN app" of T-A09: these two components live in the test APK
 * (package `<applicationId>.test`), so they run in their own process with
 * their own uid and VPN consent. The test pre-consents that package with
 * `appops set <test package> ACTIVATE_VPN allow`; [VpnService.prepare] then
 * takes the slot from phpvpn, which receives onRevoke.
 */
class RivalVpnActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val stop = intent.getBooleanExtra(EXTRA_STOP, false)
        if (!stop && VpnService.prepare(this) != null) {
            Log.w(TAG, "rival: no consent")
        } else {
            startService(Intent(this, RivalVpnService::class.java).putExtra(EXTRA_STOP, stop))
        }
        finish()
    }

    companion object {
        const val EXTRA_STOP = "stop"
    }
}

class RivalVpnService : VpnService() {
    private var tunnel: ParcelFileDescriptor? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.getBooleanExtra(RivalVpnActivity.EXTRA_STOP, false) == true) {
            close()
            stopSelf()
            return START_NOT_STICKY
        }
        if (tunnel == null) {
            tunnel = Builder().setSession("rival").addAddress("10.111.0.2", 32).addRoute("10.111.0.0", 24).establish()
            Log.i(TAG, "rival: established=${tunnel != null}")
        }
        return START_NOT_STICKY
    }

    override fun onRevoke() {
        close()
        stopSelf()
    }

    override fun onDestroy() {
        close()
        super.onDestroy()
    }

    private fun close() {
        tunnel?.close()
        tunnel = null
    }
}

private const val TAG = "phpvpn-test"
