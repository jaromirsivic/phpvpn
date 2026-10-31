package phpvpn.android.debug

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.core.net.toUri
import androidx.navigation.compose.rememberNavController
import phpvpn.android.bridge.NativeBridge
import phpvpn.android.enableLightEdgeToEdge
import phpvpn.android.ui.components.MainDestination
import phpvpn.android.ui.navigation.MapPaths
import phpvpn.android.ui.navigation.PhpvpnNavHost
import phpvpn.android.ui.navigation.navigateToMain
import phpvpn.android.ui.theme.PhpvpnTheme

/**
 * Debug-only route switcher: opens the app graph and then the map path given
 * in the `route` extra, on top of Connection so that Back behaves as in the
 * app. Example (Git Bash needs MSYS_NO_PATHCONV=1):
 * `adb shell am start -n io.github.jaromirsivic.phpvpn/phpvpn.android.debug.DebugRouteActivity --es route contracts/10.80.0.2/password`
 */
class DebugRouteActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        enableLightEdgeToEdge()
        super.onCreate(savedInstanceState)
        val route = intent.getStringExtra("route").orEmpty().trim('/')
        setContent {
            PhpvpnTheme {
                val navController = rememberNavController()
                PhpvpnNavHost(clientVersion = NativeBridge::version, navController = navController)
                var opened by rememberSaveable { mutableStateOf(false) }
                LaunchedEffect(route) {
                    if (opened || route.isEmpty()) return@LaunchedEffect
                    opened = true
                    val main = MainDestination.entries.firstOrNull { route.substringBefore('?') == it.name.lowercase() }
                    if (main != null && !route.contains('?')) {
                        navController.navigateToMain(main)
                    } else {
                        navController.navigate("${MapPaths.BASE}/$route".toUri())
                    }
                }
            }
        }
    }
}
