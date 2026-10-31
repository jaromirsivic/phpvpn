package phpvpn.android.ui.navigation

import androidx.activity.ComponentActivity
import androidx.activity.compose.LocalActivity
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.window.DialogProperties
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavDestination.Companion.hasRoute
import androidx.navigation.NavGraphBuilder
import androidx.navigation.NavHostController
import androidx.navigation.compose.composable
import androidx.navigation.compose.dialog
import androidx.navigation.toRoute
import phpvpn.android.appContainer
import phpvpn.android.ui.components.MainDestination
import phpvpn.android.ui.screens.common.appViewModel
import phpvpn.android.ui.screens.connection.ConnectionChecksScreen
import phpvpn.android.ui.screens.connection.ConnectionNavigation
import phpvpn.android.ui.screens.connection.ConnectionScreen
import phpvpn.android.ui.screens.connection.PermissionDeniedScreen
import phpvpn.android.ui.screens.server.ServerScreen
import phpvpn.android.ui.screens.settings.AboutScreen
import phpvpn.android.ui.screens.settings.ConfigAttentionScreen
import phpvpn.android.ui.screens.settings.ConfigNavigation
import phpvpn.android.ui.screens.settings.ExitDialog
import phpvpn.android.ui.screens.settings.SettingsNavigation
import phpvpn.android.ui.screens.settings.SettingsScreen
import phpvpn.android.ui.screens.settings.WelcomeScreen
import phpvpn.android.ui.screens.traffic.TrafficScreen
import phpvpn.android.ui.state.ConfigStateViewModel
import phpvpn.android.ui.state.ConnectionViewModel
import phpvpn.android.ui.state.ExitViewModel
import phpvpn.android.ui.state.ServerViewModel
import phpvpn.android.ui.state.SettingsViewModel
import phpvpn.android.ui.state.StartDestinationViewModel
import phpvpn.android.ui.state.StartTarget
import phpvpn.android.ui.state.TrafficViewModel

/**
 * WP08: the four main destinations and Welcome (00, 01-08, 10, 26-29, 37, 38,
 * 43-45, 48). The screens live in ui/screens, their state holders in ui/state.
 */
internal fun NavGraphBuilder.mainDestinations(nav: NavHostController) {
    val selectMain: (MainDestination) -> Unit = { nav.navigateToMain(it) }

    composable<Connection>(deepLinks = link(MapPaths.CONNECTION)) { entry ->
        StartDestinationCheck(nav)
        ConnectionScreen(
            connectionViewModel(nav, entry),
            LocalContext.current.appContainer.vpnConsent,
            ConnectionNavigation(
                selectMain = selectMain,
                openChecks = { nav.navigate(ConnectionChecks) { launchSingleTop = true } },
                openPermissionDenied = { nav.navigate(ConnectionPermission) { launchSingleTop = true } },
                openConfigAttention = { nav.navigate(ConfigAttention) { launchSingleTop = true } },
                openHttpDetails = { nav.navigate(DiagnosticResponse(DiagnosticSource.Connect)) { launchSingleTop = true } },
                openWizardUrl = { nav.openWizardUrl() },
            ),
        )
    }
    composable<ConnectionChecks>(deepLinks = link(MapPaths.CONNECTION_CHECKS)) { entry ->
        ConnectionChecksScreen(connectionViewModel(nav, entry), onBack = { nav.popBackStack() })
    }
    composable<ConnectionPermission>(deepLinks = link(MapPaths.CONNECTION_PERMISSION)) { entry ->
        PermissionDeniedScreen(
            connectionViewModel(nav, entry),
            LocalContext.current.appContainer.vpnConsent,
            onDone = { nav.popBackStack() },
        )
    }
    composable<Traffic>(deepLinks = link(MapPaths.TRAFFIC)) {
        TrafficScreen(appViewModel { TrafficViewModel(it.statistics) }, selectMain)
    }
    composable<Server>(deepLinks = link(MapPaths.SERVER)) { entry ->
        ServerScreen(
            appViewModel { ServerViewModel(it.serverInfo, it.serverInfoModel, it.configSummary) },
            entry.toRoute<Server>().tab,
            selectMain,
        )
    }
    composable<Settings>(deepLinks = link(MapPaths.SETTINGS)) {
        SettingsScreen(
            appViewModel { SettingsViewModel(it.connection, it.alwaysOn) },
            SettingsNavigation(
                selectMain = selectMain,
                openWizardGate = { nav.openWizard() },
                openWizardUrl = { nav.openWizardUrl() },
                openContracts = { nav.openContracts() },
                openEditor = { nav.openEditor() },
                openAbout = { nav.navigate(About) { launchSingleTop = true } },
                openExit = { nav.navigate(Exit) { launchSingleTop = true } },
            ),
        )
    }
    composable<Welcome>(deepLinks = link(MapPaths.WELCOME)) {
        WelcomeScreen(appViewModel { ConfigStateViewModel(it.ipcClient, loadOnStart = false) }, configNavigation(nav))
    }
}

/** WP08: About (34), Configuration needs attention (36) and the Exit dialog (35). */
internal fun NavGraphBuilder.settingsExtras(nav: NavHostController, clientVersion: () -> String) {
    composable<About>(deepLinks = link(MapPaths.ABOUT)) {
        val alwaysOn by LocalContext.current.appContainer.alwaysOn.state.collectAsStateWithLifecycle()
        AboutScreen(clientVersion, alwaysOn, onBack = { nav.popBackStack() })
    }
    composable<ConfigAttention>(deepLinks = link(MapPaths.CONFIG_ATTENTION)) {
        ConfigAttentionScreen(
            appViewModel { ConfigStateViewModel(it.ipcClient, loadOnStart = true) },
            configNavigation(nav),
            onBack = { nav.popBackStack() },
        )
    }
    dialog<Exit>(
        deepLinks = link(MapPaths.EXIT),
        dialogProperties = DialogProperties(usePlatformDefaultWidth = false),
    ) {
        ExitDialog(
            appViewModel { ExitViewModel(it.connection, it.connectActions) },
            onShowConnection = {
                nav.popBackStack()
                nav.navigateToMain(MainDestination.Connection)
            },
        )
    }
}

private fun configNavigation(nav: NavHostController) = ConfigNavigation(
    openWizardUrl = { nav.openWizardUrl() },
    openEditor = { nav.openEditor() },
    openCreateDefault = { path -> nav.navigate(CreateDefaultConfig(path)) { launchSingleTop = true } },
    openAttention = { nav.navigate(ConfigAttention) { launchSingleTop = true } },
)

/**
 * 37 and 48 sit on top of Connection and share its state holder, so they see
 * the same status and a consent granted on 37 starts the same Connect.
 */
@Composable
private fun connectionViewModel(nav: NavHostController, entry: NavBackStackEntry): ConnectionViewModel {
    val owner = remember(entry) { runCatching { nav.getBackStackEntry<Connection>() }.getOrDefault(entry) }
    return appViewModel(owner) {
        ConnectionViewModel(it.connection, it.connectActions, it.vpnConsent, it.connectionEvents, it.configSummary)
    }
}

/**
 * Start destination (WP07 left `showWelcome` to WP08): one get-config-state
 * per activity. Missing configuration makes Welcome (00) the root; a stored
 * but unusable document opens 36 over Connection. A late answer never moves
 * the user away from another screen.
 */
@Composable
private fun StartDestinationCheck(nav: NavHostController) {
    val activity = LocalActivity.current as? ComponentActivity ?: return
    val decider = appViewModel(activity) { StartDestinationViewModel(it.ipcClient) }
    LaunchedEffect(decider) {
        val target = decider.decide() ?: return@LaunchedEffect
        if (nav.currentDestination?.hasRoute<Connection>() != true) return@LaunchedEffect
        when (target) {
            StartTarget.WELCOME -> nav.showWelcome()
            StartTarget.ATTENTION -> nav.navigate(ConfigAttention) { launchSingleTop = true }
        }
    }
}
