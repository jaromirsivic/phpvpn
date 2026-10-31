package phpvpn.android.ui.navigation

import androidx.navigation.NavController
import phpvpn.android.ui.components.MainDestination

// Back stack rules of android-screen-map.md in one place, so the screens of
// WP08 to WP10 navigate the same way the placeholders do.

/** Route object of a bottom-navigation destination. */
fun MainDestination.route(): Any = when (this) {
    MainDestination.Connection -> Connection
    MainDestination.Traffic -> Traffic
    MainDestination.Server -> Server()
    MainDestination.Settings -> Settings
}

private fun NavController.hasConnectionRoot(): Boolean =
    runCatching { getBackStackEntry<Connection>() }.isSuccess

/**
 * Bottom navigation: one entry per destination with saved state, Connection
 * as the root, Back from another destination returns to Connection. Also
 * used to leave flows that end on a main destination (17 Connect, 25).
 */
fun NavController.navigateToMain(destination: MainDestination) {
    if (!hasConnectionRoot()) {
        // Coming from Welcome or another root: Connection becomes the root.
        navigate(Connection) {
            popUpTo(graph.id) { inclusive = true }
            launchSingleTop = true
        }
        if (destination == MainDestination.Connection) return
    }
    navigate(destination.route()) {
        popUpTo<Connection> { saveState = true }
        launchSingleTop = true
        restoreState = true
    }
}

/** 00 replaces the whole stack while no configuration exists. */
fun NavController.showWelcome() {
    navigate(Welcome) {
        popUpTo(graph.id) { inclusive = true }
        launchSingleTop = true
    }
}

/** Opens the wizard at the disconnect gate (11), which skips itself when disconnected. */
fun NavController.openWizard() {
    navigate(WizardGate) { launchSingleTop = true }
}

/** Opens the wizard directly at the first step (00, 06 "Check server URL", 36, 42, 47). */
fun NavController.openWizardUrl() {
    navigate(WizardUrl) { launchSingleTop = true }
}

/** 11 → 12 after the confirmed disconnect: the gate is not kept on the stack. */
fun NavController.passWizardGate() {
    navigate(WizardUrl) {
        popUpTo<WizardGate> { inclusive = true }
        launchSingleTop = true
    }
}

/** Discard (32) or Keep connected (11): leave every wizard step at once. */
fun NavController.leaveWizard() {
    popBackStack<WizardGraph>(inclusive = true)
}

/** 18: shown over the step that failed; Back returns to the URL step like the design. */
fun NavController.showWizardError() {
    navigate(WizardError) {
        popUpTo<WizardUrl>()
        launchSingleTop = true
    }
}

/** 18 → the secret step again. */
fun NavController.correctWizardSecret() {
    navigate(WizardSecret) {
        popUpTo<WizardError> { inclusive = true }
        launchSingleTop = true
    }
}

/** 16 → 17: the saved configuration replaces the wizard. */
fun NavController.finishWizard() {
    navigate(WizardSaved) {
        popUpTo<WizardGraph> { inclusive = true }
        launchSingleTop = true
    }
}

/** 47: an expired session replaces the wizard; its result must not be reused. */
fun NavController.expireWizard() {
    navigate(WizardExpired) {
        popUpTo<WizardGraph> { inclusive = true }
        launchSingleTop = true
    }
}

/** 10 or 17 → 19. */
fun NavController.openContracts() {
    navigate(Contracts()) { launchSingleTop = true }
}

/** 19 ↔ 41: replaces the list entry; the draft lives in the contracts state holder. */
fun NavController.openContractsPage(page: Int) {
    navigate(Contracts(page)) {
        popUpTo<Contracts> { inclusive = true }
        launchSingleTop = true
    }
}

/** Discard (32) on the list: leave the contracts flow. */
fun NavController.leaveContracts() {
    popBackStack<ContractsGraph>(inclusive = true)
}

/** Discard (32) on the editor: leave the editor flow. */
fun NavController.leaveEditor() {
    popBackStack<EditorGraph>(inclusive = true)
}

/** 19/23 → 25 after a confirmed save: the edited flow is closed. */
fun NavController.showSaved(origin: SaveOrigin) {
    navigate(Saved) {
        when (origin) {
            SaveOrigin.Contracts -> popUpTo<ContractsGraph> { inclusive = true }
            SaveOrigin.Editor -> popUpTo<EditorGraph> { inclusive = true }
            SaveOrigin.Wizard -> popUpTo<WizardGraph> { inclusive = true }
        }
        launchSingleTop = true
    }
}

/** 33 over the origin, which keeps its draft underneath. */
fun NavController.showSaveUnknown(origin: SaveOrigin) {
    navigate(SaveUnknown(origin)) { launchSingleTop = true }
}

/** 33 → 46: the check result replaces the unknown state. */
fun NavController.showSaveChecked(origin: SaveOrigin) {
    navigate(SaveChecked(origin)) {
        popUpTo<SaveUnknown> { inclusive = true }
        launchSingleTop = true
    }
}

/** Opens the editor (23). WP10 first checks for a missing file and shows 36a instead. */
fun NavController.openEditor() {
    navigate(Editor) { launchSingleTop = true }
}

/** 36a Yes: the editor opens on the created (or concurrently created) document. */
fun NavController.openEditorAfterCreate() {
    navigate(Editor) {
        popUpTo<CreateDefaultConfig> { inclusive = true }
        launchSingleTop = true
    }
}

/** 30 ↔ 31: the tabs replace each other instead of stacking. */
fun NavController.switchDiagnosticTab(source: DiagnosticSource, showRequest: Boolean) {
    if (showRequest) {
        navigate(DiagnosticRequest(source)) {
            popUpTo<DiagnosticResponse> { inclusive = true }
            launchSingleTop = true
        }
    } else {
        navigate(DiagnosticResponse(source)) {
            popUpTo<DiagnosticRequest> { inclusive = true }
            launchSingleTop = true
        }
    }
}
