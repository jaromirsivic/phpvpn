package phpvpn.android.bridge

import android.app.Activity
import android.content.Intent
import android.net.VpnService
import android.os.Bundle

/**
 * Test-only activity that asks for the VPN consent the way the app will
 * (WP05, screen 03): the system ConfirmDialog only records a consent for the
 * package that started it with startActivityForResult, so the instrumentation
 * cannot start the prepare() intent directly.
 */
class ConsentActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val request = VpnService.prepare(this)
        if (request == null) {
            finish()
            return
        }
        @Suppress("DEPRECATION") // the plain Activity API, no androidx result registry in the test APK
        startActivityForResult(request, REQUEST_CONSENT)
    }

    @Deprecated("Activity result API of the platform")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        finish()
    }

    private companion object {
        const val REQUEST_CONSENT = 1
    }
}
