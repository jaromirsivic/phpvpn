package phpvpn.android.bridge

import android.content.Intent
import android.net.VpnService
import android.os.Binder
import android.os.IBinder

/**
 * Test-only VpnService implementing [TunnelHost] (plan-android-v001 T-A01).
 * The production service (PhpvpnVpnService, WP05) will do the same through
 * [VpnInterfaces]. The test binds it with [ACTION_TEST_BIND]; the system binds
 * it with `android.net.VpnService` while the interface exists.
 */
class TestVpnService : VpnService(), TunnelHost {
    inner class LocalBinder : Binder() {
        val service: TestVpnService
            get() = this@TestVpnService
    }

    @Volatile
    private var lastError = TunnelHost.ESTABLISH_OK

    @Volatile
    var lastSpecJson: String? = null
        private set

    @Volatile
    var establishCalls = 0
        private set

    override fun onBind(intent: Intent?): IBinder? =
        if (intent?.action == ACTION_TEST_BIND) LocalBinder() else super.onBind(intent)

    override fun establish(specJson: ByteArray): Int {
        establishCalls++
        lastSpecJson = specJson.toString(Charsets.UTF_8)
        val result = VpnInterfaces.establish(this, specJson)
        lastError = result.error
        return result.descriptor
    }

    override fun lastEstablishError(): Int = lastError

    override fun waitVpnNetworkGone(timeoutMs: Int): Boolean = VpnInterfaces.waitVpnNetworkGone(this, timeoutMs)

    companion object {
        const val ACTION_TEST_BIND = "phpvpn.android.bridge.TEST_BIND"
    }
}
