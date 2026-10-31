package phpvpn.android.ui.navigation

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.window.DialogProperties
import androidx.navigation.NavGraphBuilder
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.dialog
import androidx.navigation.compose.navigation
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navDeepLink
import androidx.navigation.toRoute
import phpvpn.android.R
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.InfoRow
import phpvpn.android.ui.components.MainDestination
import phpvpn.android.ui.components.Pager
import phpvpn.android.ui.components.Tabs

/**
 * WP07 placeholders report a pending draft on every screen of the 32 pattern
 * (12-16, 19-22, 23) so that Back and Cancel exercise the dialog. WP09 and
 * WP10 replace this with the real dirty flag of their state holders.
 */
private const val PLACEHOLDER_HAS_DRAFT = true

/** First sample target of the design (19); real targets come from ContractsModel (WP10). */
private const val PLACEHOLDER_CONTRACT_IP = "10.80.0.2"

/** Sample page count for the placeholder pager (252 targets, 4 per page in the design). */
private const val PLACEHOLDER_PAGE_SIZE = 4
private const val PLACEHOLDER_TARGETS = 252

private const val WP08 = 8
private const val WP09 = 9
private const val WP10 = 10

private fun link(uri: String) = listOf(navDeepLink { uriPattern = uri })

@Composable
private fun navAction(@StringRes label: Int, style: ActionStyle = ActionStyle.Secondary, onClick: () -> Unit) =
    PlaceholderAction(stringResource(label), onClick, style)

/**
 * The navigation graph of android-screen-map.md. Start destination is
 * Connection; WP08 switches to [Welcome] with [showWelcome] while no
 * configuration exists. [clientVersion] feeds About (34).
 */
@Composable
fun PhpvpnNavHost(
    clientVersion: () -> String,
    modifier: Modifier = Modifier,
    navController: NavHostController = rememberNavController(),
) {
    NavHost(navController, startDestination = Connection, modifier = modifier) {
        mainDestinations(navController)
        wizard(navController)
        contracts(navController)
        editor(navController)
        recovery(navController, clientVersion)
    }
}

private fun NavGraphBuilder.mainDestinations(nav: NavHostController) {
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

private fun NavGraphBuilder.wizard(nav: NavHostController) {
    navigation<WizardGraph>(startDestination = WizardUrl) {
        composable<WizardGate>(deepLinks = link(MapPaths.WIZARD_GATE)) {
            PlaceholderScreen(
                R.string.gate_title,
                WP09,
                onBack = { nav.popBackStack() },
                footer = listOf(
                    navAction(R.string.gate_disconnect_continue, ActionStyle.Primary) { nav.passWizardGate() },
                    navAction(R.string.gate_keep_connected, ActionStyle.Ghost) { nav.leaveWizard() },
                ),
            )
        }
        composable<WizardUrl>(deepLinks = link(MapPaths.WIZARD_URL)) {
            val leave = rememberUnsavedChangesGuard(PLACEHOLDER_HAS_DRAFT) { nav.leaveWizard() }
            PlaceholderScreen(
                R.string.wizard_url_title,
                WP09,
                onBack = leave,
                links = listOf(
                    navAction(R.string.wizard_url_headers) { nav.navigate(WizardHeaders) },
                    navAction(R.string.wizard_error_title) { nav.showWizardError() },
                    navAction(R.string.attention_title) { nav.navigate(ConfigAttention) },
                ),
                footer = listOf(
                    navAction(R.string.wizard_verify_continue, ActionStyle.Primary) { nav.navigate(WizardSecret) },
                    navAction(R.string.wizard_cancel_setup, ActionStyle.Ghost, leave),
                ),
            )
        }
        composable<WizardHeaders>(deepLinks = link(MapPaths.WIZARD_HEADERS)) {
            // Cancel here drops only the header edits and returns to step 1.
            val leave = rememberUnsavedChangesGuard(PLACEHOLDER_HAS_DRAFT) { nav.popBackStack() }
            PlaceholderScreen(
                R.string.headers_title,
                WP09,
                onBack = leave,
                footer = listOf(
                    navAction(R.string.headers_use, ActionStyle.Primary) { nav.popBackStack() },
                    navAction(R.string.headers_cancel, ActionStyle.Ghost, leave),
                ),
            )
        }
        composable<WizardSecret>(deepLinks = link(MapPaths.WIZARD_SECRET)) {
            val leave = rememberUnsavedChangesGuard(PLACEHOLDER_HAS_DRAFT) { nav.leaveWizard() }
            PlaceholderScreen(
                R.string.wizard_secret_title,
                WP09,
                onBack = leave,
                links = listOf(navAction(R.string.wizard_error_title) { nav.showWizardError() }),
                footer = listOf(
                    navAction(R.string.wizard_verify_continue, ActionStyle.Primary) { nav.navigate(WizardIdentity) },
                    navAction(R.string.wizard_cancel_setup, ActionStyle.Ghost, leave),
                ),
            )
        }
        composable<WizardIdentity>(deepLinks = link(MapPaths.WIZARD_IDENTITY)) {
            val leave = rememberUnsavedChangesGuard(PLACEHOLDER_HAS_DRAFT) { nav.leaveWizard() }
            PlaceholderScreen(
                R.string.wizard_identity_title,
                WP09,
                onBack = leave,
                footer = listOf(
                    navAction(R.string.wizard_verify_continue, ActionStyle.Primary) { nav.navigate(WizardFinish) },
                ),
            )
        }
        composable<WizardFinish>(deepLinks = link(MapPaths.WIZARD_FINISH)) {
            val leave = rememberUnsavedChangesGuard(PLACEHOLDER_HAS_DRAFT) { nav.leaveWizard() }
            PlaceholderScreen(
                R.string.wizard_finish_title,
                WP09,
                onBack = leave,
                links = listOf(navAction(R.string.save_unknown_title) { nav.showSaveUnknown(SaveOrigin.Wizard) }),
                footer = listOf(navAction(R.string.wizard_finish_save, ActionStyle.Primary) { nav.finishWizard() }),
            )
        }
        composable<WizardError>(deepLinks = link(MapPaths.WIZARD_ERROR)) {
            PlaceholderScreen(
                R.string.wizard_error_title,
                WP09,
                onBack = { nav.popBackStack() },
                links = listOf(
                    navAction(R.string.entry_http_error_details) { nav.navigate(DiagnosticResponse(DiagnosticSource.Wizard)) },
                ),
                footer = listOf(navAction(R.string.wizard_error_correct, ActionStyle.Primary) { nav.correctWizardSecret() }),
            )
        }
    }
    composable<WizardSaved>(deepLinks = link(MapPaths.WIZARD_SAVED)) {
        PlaceholderScreen(
            R.string.wizard_saved_title,
            WP09,
            onBack = { nav.popBackStack() },
            links = listOf(navAction(R.string.wizard_saved_review) { nav.openContracts() }),
            footer = listOf(
                navAction(R.string.action_connect_vpn, ActionStyle.Primary) { nav.navigateToMain(MainDestination.Connection) },
                navAction(R.string.wizard_saved_later, ActionStyle.Ghost) { nav.navigateToMain(MainDestination.Connection) },
            ),
        )
    }
    composable<WizardExpired>(deepLinks = link(MapPaths.WIZARD_EXPIRED)) {
        PlaceholderScreen(
            R.string.session_expired_title,
            WP09,
            onBack = { nav.popBackStack() },
            footer = listOf(
                navAction(R.string.session_expired_reopen, ActionStyle.Primary) { nav.openWizardUrl() },
                navAction(R.string.action_inspect_configuration, ActionStyle.Ghost) { nav.openEditor() },
            ),
        )
    }
}

private fun NavGraphBuilder.contracts(nav: NavHostController) {
    navigation<ContractsGraph>(startDestination = Contracts()) {
        composable<Contracts>(deepLinks = link(MapPaths.CONTRACTS)) { entry ->
            val page = entry.toRoute<Contracts>().page.coerceAtLeast(1)
            val leave = rememberUnsavedChangesGuard(PLACEHOLDER_HAS_DRAFT) { nav.leaveContracts() }
            val pages = (PLACEHOLDER_TARGETS + PLACEHOLDER_PAGE_SIZE - 1) / PLACEHOLDER_PAGE_SIZE
            val first = (page - 1) * PLACEHOLDER_PAGE_SIZE + 1
            val last = minOf(page * PLACEHOLDER_PAGE_SIZE, PLACEHOLDER_TARGETS)
            PlaceholderScreen(
                if (page == 1) R.string.contracts_title else R.string.contracts_more_title,
                WP10,
                onBack = leave,
                argument = "page=$page",
                extra = {
                    Pager(
                        pluralStringResource(R.plurals.contracts_page_range, PLACEHOLDER_TARGETS, first, last, PLACEHOLDER_TARGETS),
                        previousLabel = stringResource(R.string.contracts_page_previous),
                        onPrevious = if (page > 1) ({ nav.openContractsPage(page - 1) }) else null,
                        nextLabel = stringResource(R.string.contracts_page_next),
                        onNext = if (page < pages) ({ nav.openContractsPage(page + 1) }) else null,
                    )
                },
                links = listOf(
                    navAction(R.string.contract_detail_title) { nav.navigate(ContractDetail(PLACEHOLDER_CONTRACT_IP)) },
                    navAction(R.string.save_unknown_title) { nav.showSaveUnknown(SaveOrigin.Contracts) },
                ),
                footer = listOf(
                    PlaceholderAction(
                        pluralStringResource(R.plurals.contracts_save_changes, 2, 2),
                        { nav.showSaved(SaveOrigin.Contracts) },
                        ActionStyle.Primary,
                    ),
                    navAction(R.string.action_discard_changes, ActionStyle.Ghost, leave),
                ),
            )
        }
        composable<ContractDetail>(deepLinks = link(MapPaths.CONTRACT_DETAIL)) { entry ->
            val ip = entry.toRoute<ContractDetail>().ip
            // Discarding a detail returns to the list; the list keeps its other draft changes.
            val leave = rememberUnsavedChangesGuard(PLACEHOLDER_HAS_DRAFT) { nav.popBackStack() }
            PlaceholderScreen(
                R.string.contract_detail_title,
                WP10,
                onBack = leave,
                argument = "ip=$ip",
                links = listOf(navAction(R.string.contract_detail_from_password) { nav.navigate(ContractPassword(ip)) }),
                footer = listOf(navAction(R.string.contract_detail_apply, ActionStyle.Primary) { nav.popBackStack() }),
            )
        }
        composable<ContractPassword>(deepLinks = link(MapPaths.CONTRACT_PASSWORD)) { entry ->
            val ip = entry.toRoute<ContractPassword>().ip
            val leave = rememberUnsavedChangesGuard(PLACEHOLDER_HAS_DRAFT) { nav.popBackStack() }
            PlaceholderScreen(
                R.string.password_key_title,
                WP10,
                onBack = leave,
                argument = "ip=$ip",
                footer = listOf(navAction(R.string.password_key_use, ActionStyle.Primary) { nav.popBackStack() }),
            )
        }
    }
}

private fun NavGraphBuilder.editor(nav: NavHostController) {
    navigation<EditorGraph>(startDestination = Editor) {
        composable<Editor>(deepLinks = link(MapPaths.EDITOR)) {
            val leave = rememberUnsavedChangesGuard(PLACEHOLDER_HAS_DRAFT) { nav.leaveEditor() }
            PlaceholderScreen(
                R.string.editor_title,
                WP10,
                onBack = leave,
                links = listOf(
                    navAction(R.string.editor_reset) { nav.navigate(EditorReset) },
                    navAction(R.string.save_unknown_title) { nav.showSaveUnknown(SaveOrigin.Editor) },
                ),
                footer = listOf(
                    navAction(R.string.editor_save, ActionStyle.Primary) { nav.showSaved(SaveOrigin.Editor) },
                    navAction(R.string.action_discard_changes, ActionStyle.Ghost, leave),
                ),
            )
        }
        composable<EditorReset>(deepLinks = link(MapPaths.EDITOR_RESET)) {
            PlaceholderScreen(
                R.string.reset_title,
                WP10,
                onBack = { nav.popBackStack() },
                footer = listOf(
                    navAction(R.string.reset_confirm, ActionStyle.Danger) { nav.popBackStack() },
                    navAction(R.string.reset_keep) { nav.popBackStack() },
                ),
            )
        }
    }
}

private fun NavGraphBuilder.recovery(nav: NavHostController, clientVersion: () -> String) {
    composable<Saved>(deepLinks = link(MapPaths.SAVED)) {
        PlaceholderScreen(
            R.string.saved_title,
            WP10,
            onBack = { nav.popBackStack() },
            footer = listOf(
                navAction(R.string.saved_reconnect, ActionStyle.Primary) { nav.navigateToMain(MainDestination.Connection) },
                navAction(R.string.saved_next_connection, ActionStyle.Ghost) { nav.navigateToMain(MainDestination.Connection) },
            ),
        )
    }
    composable<SaveUnknown>(deepLinks = link(MapPaths.SAVE_UNKNOWN)) { entry ->
        val origin = entry.toRoute<SaveUnknown>().origin
        PlaceholderScreen(
            R.string.save_unknown_title,
            if (origin == SaveOrigin.Wizard) WP09 else WP10,
            onBack = { nav.popBackStack() },
            argument = "origin=${origin.name.lowercase()}",
            links = listOf(navAction(R.string.save_unknown_inspect) { nav.openEditor() }),
            footer = listOf(navAction(R.string.save_unknown_verify, ActionStyle.Primary) { nav.showSaveChecked(origin) }),
        )
    }
    composable<SaveChecked>(deepLinks = link(MapPaths.SAVE_CHECKED)) { entry ->
        val origin = entry.toRoute<SaveChecked>().origin
        PlaceholderScreen(
            R.string.save_checked_title,
            WP10,
            onBack = { nav.popBackStack() },
            argument = "origin=${origin.name.lowercase()}",
            footer = listOf(
                // Back to the preserved draft, which re-submits it.
                navAction(R.string.save_checked_again, ActionStyle.Primary) { nav.popBackStack() },
                navAction(R.string.action_inspect_configuration, ActionStyle.Ghost) { nav.openEditor() },
            ),
        )
    }
    composable<DiagnosticResponse>(deepLinks = link(MapPaths.DIAGNOSTIC_RESPONSE)) { entry ->
        val source = entry.toRoute<DiagnosticResponse>().source
        DiagnosticPlaceholder(nav, source, showRequest = false)
    }
    composable<DiagnosticRequest>(deepLinks = link(MapPaths.DIAGNOSTIC_REQUEST)) { entry ->
        val source = entry.toRoute<DiagnosticRequest>().source
        DiagnosticPlaceholder(nav, source, showRequest = true)
    }
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
    dialog<CreateDefaultConfig>(
        deepLinks = link(MapPaths.CREATE_DEFAULT_CONFIG),
        dialogProperties = DialogProperties(usePlatformDefaultWidth = false),
    ) { entry ->
        val path = entry.toRoute<CreateDefaultConfig>().path.ifEmpty { stringResource(R.string.value_missing) }
        PlaceholderDialog(
            title = stringResource(R.string.create_default_title),
            paragraphs = listOf(
                stringResource(R.string.create_default_text, path),
                stringResource(R.string.placeholder_notice_text, WP10),
            ),
            actions = listOf(
                navAction(R.string.common_yes, ActionStyle.Primary) { nav.openEditorAfterCreate() },
                navAction(R.string.common_no) { nav.popBackStack() },
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

@Composable
private fun DiagnosticPlaceholder(nav: NavHostController, source: DiagnosticSource, showRequest: Boolean) {
    PlaceholderScreen(
        if (showRequest) R.string.http_request_title else R.string.http_response_title,
        WP09,
        onBack = { nav.popBackStack() },
        argument = "source=${source.name.lowercase()}",
        extra = {
            Tabs(
                listOf(stringResource(R.string.http_tab_request), stringResource(R.string.http_tab_response)),
                if (showRequest) 0 else 1,
                { index -> if ((index == 0) != showRequest) nav.switchDiagnosticTab(source, showRequest = index == 0) },
            )
        },
    )
}
