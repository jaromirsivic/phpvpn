package phpvpn.android.ui.navigation

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.rememberNavController

/**
 * The navigation graph of android-screen-map.md, composed from one file per
 * work package: MainGraph (WP08), WizardGraph (WP09), ContractsGraph and
 * EditorGraph (WP10). Start destination is Connection; WP08 switches to
 * [Welcome] while no configuration exists. [clientVersion] feeds About (34).
 */
@Composable
fun PhpvpnNavHost(
    clientVersion: () -> String,
    modifier: Modifier = Modifier,
    navController: NavHostController = rememberNavController(),
) {
    NavHost(navController, startDestination = Connection, modifier = modifier) {
        mainDestinations(navController)
        settingsExtras(navController, clientVersion)
        wizard(navController)
        diagnostics(navController)
        contracts(navController)
        editor(navController)
        saveOutcomes(navController)
    }
}
