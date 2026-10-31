package phpvpn.android.ui.navigation

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import androidx.navigation.NavGraphBuilder
import androidx.navigation.NavHostController
import androidx.navigation.compose.composable
import androidx.navigation.compose.navigation
import androidx.navigation.toRoute
import phpvpn.android.R
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.MainDestination
import phpvpn.android.ui.components.Tabs

/**
 * WP09: the Wizard (11-18, 40, 47) and the HTTP diagnostic views (30, 31).
 * Everything here is still the WP07 placeholder.
 */
internal fun NavGraphBuilder.wizard(nav: NavHostController) {
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

/** WP09: HTTP response (30) and request (31) of a failed Connect or Wizard probe. */
internal fun NavGraphBuilder.diagnostics(nav: NavHostController) {
    composable<DiagnosticResponse>(deepLinks = link(MapPaths.DIAGNOSTIC_RESPONSE)) { entry ->
        val source = entry.toRoute<DiagnosticResponse>().source
        DiagnosticPlaceholder(nav, source, showRequest = false)
    }
    composable<DiagnosticRequest>(deepLinks = link(MapPaths.DIAGNOSTIC_REQUEST)) { entry ->
        val source = entry.toRoute<DiagnosticRequest>().source
        DiagnosticPlaceholder(nav, source, showRequest = true)
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
