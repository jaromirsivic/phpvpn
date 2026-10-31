package phpvpn.android.ui.navigation

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.window.DialogProperties
import androidx.navigation.NavGraphBuilder
import androidx.navigation.NavHostController
import androidx.navigation.compose.composable
import androidx.navigation.compose.dialog
import androidx.navigation.toRoute
import phpvpn.android.R
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.InfoRow
import phpvpn.android.ui.components.MainDestination
import phpvpn.android.ui.components.Tabs

/**
 * WP08: the four main destinations and Welcome (00, 01-08, 10, 26-29, 37, 38,
 * 43-45, 48), plus About (34), Configuration needs attention (36) and the Exit
 * dialog (35). Everything here is still the WP07 placeholder.
 */
internal fun NavGraphBuilder.mainDestinations(nav: NavHostController) {
    val selectMain: (MainDestination) -> Unit = { nav.navigateToMain(it) }

    composable<Connection>(deepLinks = link(MapPaths.CONNECTION)) {
        PlaceholderScreen(
            R.string.connection_title,
            WP08,
            main = MainDestination.Connection,
            onSelectMain = selectMain,
            links = listOf(
                navAction(R.string.connected_checks_entry) { nav.navigate(ConnectionChecks) },
                navAction(R.string.permission_denied_title) { nav.navigate(ConnectionPermission) },
                navAction(R.string.entry_http_error_details) { nav.navigate(DiagnosticResponse(DiagnosticSource.Connect)) },
                navAction(R.string.connection_error_check_url) { nav.openWizardUrl() },
                navAction(R.string.unavailable_last_statistics) { nav.navigateToMain(MainDestination.Traffic) },
                navAction(R.string.disconnected_edit_configuration) { nav.navigateToMain(MainDestination.Settings) },
            ),
        )
    }
    composable<ConnectionChecks>(deepLinks = link(MapPaths.CONNECTION_CHECKS)) {
        PlaceholderScreen(R.string.checks_title, WP08, onBack = { nav.popBackStack() })
    }
    composable<ConnectionPermission>(deepLinks = link(MapPaths.CONNECTION_PERMISSION)) {
        PlaceholderScreen(
            R.string.permission_denied_title,
            WP08,
            onBack = { nav.popBackStack() },
            footer = listOf(
                navAction(R.string.permission_denied_retry, ActionStyle.Primary) { nav.popBackStack() },
                navAction(R.string.permission_denied_stay, ActionStyle.Ghost) { nav.popBackStack() },
            ),
        )
    }
    composable<Traffic>(deepLinks = link(MapPaths.TRAFFIC)) {
        PlaceholderScreen(R.string.traffic_title, WP08, main = MainDestination.Traffic, onSelectMain = selectMain)
    }
    composable<Server>(deepLinks = link(MapPaths.SERVER)) { entry ->
        val route = entry.toRoute<Server>()
        // The tab is screen state seeded from the route (server?tab=raw opens 29).
        var tab by rememberSaveable { mutableStateOf(route.tab) }
        PlaceholderScreen(
            if (tab == ServerTab.Online) R.string.server_online_title else R.string.server_raw_title,
            WP08,
            main = MainDestination.Server,
            onSelectMain = selectMain,
            argument = "tab=${tab.name.lowercase()}",
            extra = {
                Tabs(
                    listOf(stringResource(R.string.server_tab_online), stringResource(R.string.server_tab_raw)),
                    tab.ordinal,
                    { tab = ServerTab.entries[it] },
                )
            },
        )
    }
    composable<Settings>(deepLinks = link(MapPaths.SETTINGS)) {
        PlaceholderScreen(
            R.string.settings_title,
            WP08,
            main = MainDestination.Settings,
            onSelectMain = selectMain,
            links = listOf(
                navAction(R.string.settings_wizard) { nav.openWizard() },
                navAction(R.string.entry_communication_contracts) { nav.openContracts() },
                navAction(R.string.entry_configuration_editor) { nav.openEditor() },
                navAction(R.string.settings_about) { nav.navigate(About) },
                navAction(R.string.settings_exit) { nav.navigate(Exit) },
            ),
        )
    }
    composable<Welcome>(deepLinks = link(MapPaths.WELCOME)) {
        PlaceholderScreen(
            R.string.welcome_title,
            WP08,
            footer = listOf(
                navAction(R.string.welcome_set_up, ActionStyle.Primary) { nav.openWizardUrl() },
                navAction(R.string.welcome_open_editor, ActionStyle.Ghost) { nav.navigate(CreateDefaultConfig("")) },
            ),
        )
    }
}

/** WP08: About (34), Configuration needs attention (36) and the Exit dialog (35). */
internal fun NavGraphBuilder.settingsExtras(nav: NavHostController, clientVersion: () -> String) {
    composable<About>(deepLinks = link(MapPaths.ABOUT)) {
        PlaceholderScreen(
            R.string.about_title,
            WP08,
            onBack = { nav.popBackStack() },
            extra = {
                Card {
                    InfoRow(stringResource(R.string.about_version), clientVersion())
                    InfoRow(stringResource(R.string.about_font), stringResource(R.string.about_font_licence), divider = true)
                }
            },
        )
    }
    composable<ConfigAttention>(deepLinks = link(MapPaths.CONFIG_ATTENTION)) {
        PlaceholderScreen(
            R.string.attention_title,
            WP08,
            onBack = { nav.popBackStack() },
            links = listOf(
                navAction(R.string.attention_repair) { nav.openEditor() },
                navAction(R.string.attention_defaults) { nav.openWizardUrl() },
                navAction(R.string.attention_create_default) { nav.navigate(CreateDefaultConfig("")) },
            ),
        )
    }
    dialog<Exit>(
        deepLinks = link(MapPaths.EXIT),
        dialogProperties = DialogProperties(usePlatformDefaultWidth = false),
    ) {
        PlaceholderDialog(
            title = stringResource(R.string.exit_heading),
            eyebrow = stringResource(R.string.exit_eyebrow),
            paragraphs = listOf(
                stringResource(R.string.exit_intro),
                stringResource(R.string.placeholder_notice_text, WP08),
            ),
            actions = listOf(
                navAction(R.string.exit_keep_title, ActionStyle.Primary) { nav.popBackStack() },
                navAction(R.string.action_disconnect_vpn, ActionStyle.Danger) {
                    nav.popBackStack()
                    nav.navigateToMain(MainDestination.Connection)
                },
            ),
        )
    }
}
