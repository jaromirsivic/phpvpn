package phpvpn.android.ui.navigation

import androidx.activity.compose.BackHandler
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavGraphBuilder
import androidx.navigation.NavHostController
import androidx.navigation.compose.composable
import androidx.navigation.compose.navigation
import androidx.navigation.toRoute
import phpvpn.android.bridge.controller.ContractsStage
import phpvpn.android.ui.screens.ConfigFlowDependencies
import phpvpn.android.ui.screens.configFlowDependencies
import phpvpn.android.ui.screens.contracts.ContractDetailScreen
import phpvpn.android.ui.screens.contracts.ContractsListActions
import phpvpn.android.ui.screens.contracts.ContractsListScreen
import phpvpn.android.ui.screens.contracts.ContractsNavigation
import phpvpn.android.ui.screens.contracts.ContractsViewModel
import phpvpn.android.ui.screens.password.PasswordKeyScreen
import phpvpn.android.ui.screens.password.PasswordKeyViewModel

/**
 * WP10: Communication contracts (19, 41), contract detail (20, 22) and the
 * key from a password (21). One [ContractsViewModel] per visit of the graph
 * holds the draft for every target IP, so page switches (which replace the
 * list entry) and the detail screens never lose it.
 */
internal fun NavGraphBuilder.contracts(nav: NavHostController) {
    navigation<ContractsGraph>(startDestination = Contracts()) {
        composable<Contracts>(deepLinks = link(MapPaths.CONTRACTS)) { entry ->
            val page = entry.toRoute<Contracts>().page.coerceAtLeast(1)
            val vm = contractsViewModel(nav, entry)
            val ui by vm.state.collectAsState()
            ContractsNavigationEffect(nav, vm)
            // A dirty draft or an unknown save result asks 32 before the flow is left.
            val pending = (vm.dirtyCount() > 0 && ContractsViewModel.editing(ui.stage)) || ui.stage == ContractsStage.SAVE_UNKNOWN
            val leave = rememberUnsavedChangesGuard(pending) { nav.leaveContracts() }
            // Back on a later page returns to the first page; the draft stays in the model.
            BackHandler(enabled = page > 1) { nav.openContractsPage(1) }
            // While the save is in flight the flow cannot be closed (client.md, editor rules).
            BackHandler(enabled = ui.stage == ContractsStage.SAVING) {}
            ContractsListScreen(
                vm,
                ui,
                page,
                ContractsListActions(
                    onBack = if (page > 1) ({ nav.openContractsPage(1) }) else leave,
                    onDiscard = leave,
                    onOpenPage = nav::openContractsPage,
                    onOpenContract = { nav.navigate(ContractDetail(it)) },
                    onVerifySave = { nav.showSaveUnknown(SaveOrigin.Contracts) },
                    onOpenWizard = {
                        nav.leaveContracts()
                        nav.openWizard()
                    },
                    onOpenEditor = {
                        nav.leaveContracts()
                        nav.openEditor()
                    },
                ),
            )
        }
        composable<ContractDetail>(deepLinks = link(MapPaths.CONTRACT_DETAIL)) { entry ->
            val ip = entry.toRoute<ContractDetail>().ip
            val vm = contractsViewModel(nav, entry)
            val ui by vm.state.collectAsState()
            // 22 when the contract blocks saving at the moment the detail opens.
            val invalidMode = remember(ip, ui.stage != ContractsStage.LOADING) {
                vm.openDetail(ip)
                vm.detail(ip)?.let { it.enabled && !it.keyValid } ?: false
            }
            val detail = remember(ui.revision, ip) { vm.detail(ip) }
            // Discarding a detail restores this row and returns to the list; other changes stay.
            val leave = rememberUnsavedChangesGuard(detail?.changedSinceOpen == true) {
                vm.discardDetail(ip)
                nav.popBackStack()
            }
            ContractDetailScreen(
                vm,
                ui,
                ip,
                invalidMode,
                onBack = leave,
                onApply = {
                    vm.closeDetail()
                    nav.popBackStack()
                },
                onPassword = { nav.navigate(ContractPassword(ip)) },
            )
        }
        composable<ContractPassword>(deepLinks = link(MapPaths.CONTRACT_PASSWORD)) { entry ->
            val ip = entry.toRoute<ContractPassword>().ip
            val contracts = contractsViewModel(nav, entry)
            val ui by contracts.state.collectAsState()
            val deps = configFlowDependencies()
            val vm: PasswordKeyViewModel = viewModel(factory = viewModelFactory { initializer { PasswordKeyViewModel(deps.passwordKeyModel()) } })
            // The target is captured once; a later save makes it stale (ContractTarget generation).
            val target = remember(ip, ui.stage != ContractsStage.LOADING) { contracts.target(ip) }
            var stale by remember(ip) { mutableStateOf(false) }
            val targetEnabled = remember(ui.revision, ip) { contracts.targetEnabled(ip) }
            val leave = rememberUnsavedChangesGuard(vm.password.isNotEmpty()) { nav.popBackStack() }
            PasswordKeyScreen(
                vm,
                ip,
                target?.contractId,
                targetEnabled,
                stale = stale,
                onBack = leave,
                onUse = {
                    val key = vm.key()
                    val error = try {
                        target?.let { contracts.applyPasswordKey(it, key) }
                    } finally {
                        key.wipe()
                    }
                    if (target != null && error == null) nav.popBackStack() else stale = true
                },
            )
        }
    }
}

/** The contracts state holder of the graph entry, shared by 19, 20, 21, 22, 41 and the contracts side of 33 and 46. */
@Composable
internal fun contractsViewModel(nav: NavHostController, entry: NavBackStackEntry): ContractsViewModel {
    val owner = remember(entry) { nav.getBackStackEntry<ContractsGraph>() }
    val deps = configFlowDependencies()
    return viewModel(owner, factory = contractsFactory(deps))
}

internal fun contractsFactory(deps: ConfigFlowDependencies) = viewModelFactory { initializer { ContractsViewModel(deps) } }

/** Follows the one-shot navigation requests of the contracts flow (25, 33, 22). */
@Composable
internal fun ContractsNavigationEffect(nav: NavHostController, vm: ContractsViewModel) {
    val request by vm.navigation.collectAsState()
    LaunchedEffect(request) {
        when (val next = request ?: return@LaunchedEffect) {
            ContractsNavigation.Saved -> nav.showSaved(SaveOrigin.Contracts)
            ContractsNavigation.Unknown -> nav.showSaveUnknown(SaveOrigin.Contracts)
            is ContractsNavigation.Blocked -> nav.navigate(ContractDetail(next.remoteIp))
        }
        vm.consumeNavigation()
    }
}
