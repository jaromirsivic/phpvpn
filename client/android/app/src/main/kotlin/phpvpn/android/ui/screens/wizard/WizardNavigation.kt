package phpvpn.android.ui.screens.wizard

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.lifecycle.ViewModelStoreOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavController
import phpvpn.android.ui.navigation.SaveOrigin
import phpvpn.android.ui.navigation.WizardError
import phpvpn.android.ui.navigation.WizardFinish
import phpvpn.android.ui.navigation.WizardGate
import phpvpn.android.ui.navigation.WizardGraph
import phpvpn.android.ui.navigation.WizardIdentity
import phpvpn.android.ui.navigation.WizardSecret
import phpvpn.android.ui.navigation.WizardUrl
import phpvpn.android.ui.navigation.expireWizard
import phpvpn.android.ui.navigation.finishWizard
import phpvpn.android.ui.navigation.showSaveUnknown

/**
 * The [WizardViewModel] of the Wizard graph that [entry] belongs to: one
 * session per graph, shared by every step screen and dropped (with its
 * best-effort wizard-close) when the graph leaves the back stack.
 */
@Composable
fun wizardViewModel(nav: NavController, entry: NavBackStackEntry): WizardViewModel {
    val owner: ViewModelStoreOwner = remember(entry) { nav.getBackStackEntry<WizardGraph>() }
    return wizardViewModel(owner)
}

/** The Wizard session of [owner] (the Wizard graph entry), created on first use. */
@Composable
fun wizardViewModel(owner: ViewModelStoreOwner): WizardViewModel {
    val dependencies = rememberWizardDependencies()
    return viewModel(owner, factory = viewModelFactory { initializer { WizardViewModel(dependencies) } })
}

/**
 * The live Wizard session for a screen outside the Wizard graph (30, 31 with
 * the Wizard source), or null when no Wizard graph is on the back stack.
 */
@Composable
fun existingWizardViewModel(nav: NavController, entry: NavBackStackEntry): WizardViewModel? {
    val owner = remember(entry) { runCatching { nav.getBackStackEntry<WizardGraph>() }.getOrNull() } ?: return null
    return wizardViewModel(owner)
}

/**
 * Follows the navigation requests of [vm] while this screen is shown. A
 * request is consumed once, so a transition that composes two screens never
 * navigates twice; requests made while no Wizard screen is shown wait for the
 * next one.
 */
@Composable
fun WizardNavigationEffect(vm: WizardViewModel, nav: NavController) {
    val event by vm.navigation.collectAsStateWithLifecycle()
    LaunchedEffect(event) {
        val pending = event ?: return@LaunchedEffect
        if (vm.consumeNavigation(pending)) nav.goToWizard(pending.target)
    }
}

/**
 * Back stack rules of the Wizard steps: a step that is already on the stack
 * is returned to (dropping everything above it, e.g. 18 or the gate after an
 * interruption), a new step is placed directly above its predecessor, 18 is
 * drawn over the failed step, and 17, 47 replace the whole Wizard.
 */
fun NavController.goToWizard(target: WizardNavTarget) {
    when (target) {
        WizardNavTarget.GATE -> if (!returnTo<WizardGate>()) navigate(WizardGate) { launchSingleTop = true }
        WizardNavTarget.URL -> if (!returnTo<WizardUrl>()) {
            navigate(WizardUrl) {
                popUpTo<WizardGate> { inclusive = true }
                launchSingleTop = true
            }
        }
        WizardNavTarget.SECRET -> if (!returnTo<WizardSecret>()) {
            navigate(WizardSecret) {
                popUpTo<WizardUrl>()
                launchSingleTop = true
            }
        }
        WizardNavTarget.IDENTITY -> if (!returnTo<WizardIdentity>()) {
            navigate(WizardIdentity) {
                popUpTo<WizardSecret>()
                launchSingleTop = true
            }
        }
        WizardNavTarget.FINISH -> if (!returnTo<WizardFinish>()) {
            navigate(WizardFinish) {
                popUpTo<WizardIdentity>()
                launchSingleTop = true
            }
        }
        WizardNavTarget.ERROR -> navigate(WizardError) { launchSingleTop = true }
        WizardNavTarget.EXPIRED -> expireWizard()
        WizardNavTarget.SAVE_UNKNOWN -> showSaveUnknown(SaveOrigin.Wizard)
        WizardNavTarget.SAVED -> finishWizard()
    }
}

/** Pops back to [T] when it is on the back stack; false when it is not. */
private inline fun <reified T : Any> NavController.returnTo(): Boolean {
    if (runCatching { getBackStackEntry<T>() }.isFailure) return false
    popBackStack<T>(inclusive = false)
    return true
}
