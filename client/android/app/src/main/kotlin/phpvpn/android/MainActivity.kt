package phpvpn.android

import android.graphics.Color
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import phpvpn.android.bridge.NativeBridge
import phpvpn.android.ui.navigation.PhpvpnNavHost
import phpvpn.android.ui.theme.PhpvpnTheme

/**
 * The single activity: the Compose navigation graph of the design. State
 * holders and the VPN service arrive with WP05, WP06 and WP08 to WP10.
 */
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        enableLightEdgeToEdge()
        super.onCreate(savedInstanceState)
        setContent {
            PhpvpnTheme {
                PhpvpnNavHost(clientVersion = NativeBridge::version)
            }
        }
    }
}

/**
 * Edge-to-edge with dark system bar icons in every system mode: v1 has only
 * the light theme (R7), so the bars must not follow a dark system setting.
 */
internal fun ComponentActivity.enableLightEdgeToEdge() {
    enableEdgeToEdge(
        statusBarStyle = SystemBarStyle.light(Color.TRANSPARENT, Color.TRANSPARENT),
        navigationBarStyle = SystemBarStyle.light(Color.TRANSPARENT, Color.TRANSPARENT),
    )
}
