package phpvpn.android.ui.navigation

import androidx.navigation.NavGraphBuilder
import androidx.navigation.NavHostController
import androidx.navigation.compose.composable
import androidx.navigation.compose.navigation
import androidx.navigation.toRoute
import phpvpn.android.ui.screens.diagnostic.DiagnosticRoute
import phpvpn.android.ui.screens.wizard.WizardErrorRoute
import phpvpn.android.ui.screens.wizard.WizardExpiredRoute
import phpvpn.android.ui.screens.wizard.WizardFinishRoute
import phpvpn.android.ui.screens.wizard.WizardGateRoute
import phpvpn.android.ui.screens.wizard.WizardHeadersRoute
import phpvpn.android.ui.screens.wizard.WizardIdentityRoute
import phpvpn.android.ui.screens.wizard.WizardSavedRoute
import phpvpn.android.ui.screens.wizard.WizardSecretRoute
import phpvpn.android.ui.screens.wizard.WizardUrlRoute

/**
 * WP09: the Wizard (11-18, 40, 47) and the HTTP diagnostic views (30, 31).
 * One Wizard session (WizardViewModel) is scoped to the [WizardGraph] entry;
 * its state decides which step is shown (phpvpn.android.ui.screens.wizard).
 * 17 and 47 replace the graph and have their own state.
 */
internal fun NavGraphBuilder.wizard(nav: NavHostController) {
    navigation<WizardGraph>(startDestination = WizardUrl) {
        composable<WizardGate>(deepLinks = link(MapPaths.WIZARD_GATE)) { entry -> WizardGateRoute(nav, entry) }
        composable<WizardUrl>(deepLinks = link(MapPaths.WIZARD_URL)) { entry -> WizardUrlRoute(nav, entry) }
        composable<WizardHeaders>(deepLinks = link(MapPaths.WIZARD_HEADERS)) { entry -> WizardHeadersRoute(nav, entry) }
        composable<WizardSecret>(deepLinks = link(MapPaths.WIZARD_SECRET)) { entry -> WizardSecretRoute(nav, entry) }
        composable<WizardIdentity>(deepLinks = link(MapPaths.WIZARD_IDENTITY)) { entry -> WizardIdentityRoute(nav, entry) }
        composable<WizardFinish>(deepLinks = link(MapPaths.WIZARD_FINISH)) { entry -> WizardFinishRoute(nav, entry) }
        composable<WizardError>(deepLinks = link(MapPaths.WIZARD_ERROR)) { entry -> WizardErrorRoute(nav, entry) }
    }
    composable<WizardSaved>(deepLinks = link(MapPaths.WIZARD_SAVED)) { WizardSavedRoute(nav) }
    composable<WizardExpired>(deepLinks = link(MapPaths.WIZARD_EXPIRED)) { WizardExpiredRoute(nav) }
}

/** WP09: HTTP response (30) and request (31) of a failed Connect or Wizard probe. */
internal fun NavGraphBuilder.diagnostics(nav: NavHostController) {
    composable<DiagnosticResponse>(deepLinks = link(MapPaths.DIAGNOSTIC_RESPONSE)) { entry ->
        DiagnosticRoute(nav, entry, entry.toRoute<DiagnosticResponse>().source, showRequest = false)
    }
    composable<DiagnosticRequest>(deepLinks = link(MapPaths.DIAGNOSTIC_REQUEST)) { entry ->
        DiagnosticRoute(nav, entry, entry.toRoute<DiagnosticRequest>().source, showRequest = true)
    }
}
