package phpvpn.android.ui.navigation

import androidx.activity.compose.BackHandler
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.window.DialogProperties
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import androidx.navigation.NavGraphBuilder
import androidx.navigation.NavHostController
import androidx.navigation.compose.composable
import androidx.navigation.compose.dialog
import androidx.navigation.compose.navigation
import androidx.navigation.toRoute
import phpvpn.android.bridge.controller.ContractsStage
import phpvpn.android.bridge.controller.EditorState
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.MainDestination
import phpvpn.android.ui.screens.configFlowDependencies
import phpvpn.android.ui.screens.contracts.ContractsViewModel
import phpvpn.android.ui.screens.editor.CreateDefaultDialogContent
import phpvpn.android.ui.screens.editor.EditorActions
import phpvpn.android.ui.screens.editor.EditorNavigation
import phpvpn.android.ui.screens.editor.EditorResetScreen
import phpvpn.android.ui.screens.editor.EditorScreen
import phpvpn.android.ui.screens.editor.EditorViewModel
import phpvpn.android.ui.screens.save.SaveCheckedOrigin
import phpvpn.android.ui.screens.save.SaveCheckedScreen
import phpvpn.android.ui.screens.save.SaveUnknownScreen
import phpvpn.android.ui.screens.save.SavedScreen
import phpvpn.android.ui.screens.save.SavedViewModel
import phpvpn.android.R

/**
 * WP10: the configuration editor (23, 39), Reset (24, 42) and the dialog 36a
 * that creates the default configuration. Opening the editor from 00, 10 or
 * 36 reads get-config-document first; `missing` replaces the editor with 36a
 * over the screen it came from.
 */
internal fun NavGraphBuilder.editor(nav: NavHostController) {
    navigation<EditorGraph>(startDestination = Editor) {
        composable<Editor>(deepLinks = link(MapPaths.EDITOR)) {
            val vm = editorViewModel(nav)
            val state by vm.state.collectAsState()
            LaunchedEffect(Unit) {
                if (state is EditorState.Closed || state is EditorState.Saved || state is EditorState.Failed ||
                    state is EditorState.Unavailable || state is EditorState.ConfirmCreate
                ) vm.open()
            }
            EditorNavigationEffect(nav, vm)
            val unknown = state is EditorState.SaveUnknown || state is EditorState.Checking || state is EditorState.SaveChecked
            val leave = rememberUnsavedChangesGuard(vm.dirty || unknown) {
                vm.close()
                nav.leaveEditor()
            }
            BackHandler(enabled = state is EditorState.Saving || state is EditorState.Resetting) {}
            EditorScreen(
                vm,
                state,
                EditorActions(
                    onLeave = leave,
                    onReset = { nav.navigate(EditorReset) },
                    onVerifySave = { nav.showSaveUnknown(SaveOrigin.Editor) },
                ),
            )
        }
        composable<EditorReset>(deepLinks = link(MapPaths.EDITOR_RESET)) {
            val vm = editorViewModel(nav)
            val state by vm.state.collectAsState()
            LaunchedEffect(vm.resetLoaded) {
                // A failed reset returns to the editor, which shows why; the draft is unchanged.
                if (vm.resetLoaded == false) {
                    vm.consumeReset()
                    nav.popBackStack()
                }
            }
            val back = {
                vm.consumeReset()
                nav.popBackStack()
                Unit
            }
            BackHandler(enabled = state is EditorState.Resetting) {}
            EditorResetScreen(
                vm,
                state,
                onKeep = back,
                onContinue = back,
                onWizard = {
                    // The wizard does not take over the draft: leaving the editor discards it (32).
                    vm.consumeReset()
                    nav.popBackStack()
                },
            )
        }
    }
    dialog<CreateDefaultConfig>(
        deepLinks = link(MapPaths.CREATE_DEFAULT_CONFIG),
        dialogProperties = DialogProperties(usePlatformDefaultWidth = false),
    ) {
        val vm = editorViewModel(nav)
        val state by vm.state.collectAsState()
        LaunchedEffect(Unit) {
            // Reached directly (00 or 36 placeholders, deep link): read the actual state first.
            if (state !is EditorState.ConfirmCreate && state !is EditorState.Creating) vm.open()
        }
        EditorNavigationEffect(nav, vm, inDialog = true)
        // Yes opened the actual result, or the file already existed: show it in the editor.
        val opened = state is EditorState.Editing
        LaunchedEffect(opened) { if (opened) nav.openEditorAfterCreate() }
        val no = {
            vm.answerCreate(false)
            nav.popBackStack()
            Unit
        }
        CreateDefaultDialogContent(state, onYes = { vm.answerCreate(true) }, onNo = no)
    }
}

/**
 * The editor state holder, scoped to the navigation host because 36a is a
 * dialog outside the editor graph; [EditorViewModel.close] wipes the session.
 */
@Composable
internal fun editorViewModel(nav: NavHostController): EditorViewModel {
    val owner = remember(nav) { nav.getBackStackEntry(nav.graph.id) }
    val deps = configFlowDependencies()
    return viewModel(owner, factory = viewModelFactory { initializer { EditorViewModel(deps) } })
}

/** Follows the one-shot navigation requests of the editor flow (36a, 25, 33). */
@Composable
private fun EditorNavigationEffect(nav: NavHostController, vm: EditorViewModel, inDialog: Boolean = false) {
    val request by vm.navigation.collectAsState()
    LaunchedEffect(request) {
        when (val next = request ?: return@LaunchedEffect) {
            // The editor is replaced by 36a over the screen it was opened from.
            is EditorNavigation.Missing -> if (!inDialog) {
                nav.navigate(CreateDefaultConfig(next.configPath)) {
                    popUpTo<EditorGraph> { inclusive = true }
                    launchSingleTop = true
                }
            }
            EditorNavigation.Saved -> {
                vm.close()
                nav.showSaved(SaveOrigin.Editor)
            }
            EditorNavigation.Unknown -> nav.showSaveUnknown(SaveOrigin.Editor)
        }
        vm.consumeNavigation()
    }
}

/**
 * Shared save outcomes: Changes saved (25), Save result unknown (33) and Save
 * checked (46). The contracts and the editor verify with their own
 * [phpvpn.android.ui.screens.save.SaveOutcomeSource]; the Wizard side of 33
 * and 46 (receipt check with wizard-get-commit) belongs to WP09.
 */
internal fun NavGraphBuilder.saveOutcomes(nav: NavHostController) {
    composable<Saved>(deepLinks = link(MapPaths.SAVED)) {
        val deps = configFlowDependencies()
        val vm: SavedViewModel = viewModel(factory = viewModelFactory { initializer { SavedViewModel(deps.connection, deps.clock) } })
        val done = { nav.navigateToMain(MainDestination.Connection) }
        LaunchedEffect(vm.finished) { if (vm.finished) done() }
        SavedScreen(vm, onDone = done)
    }
    composable<SaveUnknown>(deepLinks = link(MapPaths.SAVE_UNKNOWN)) { entry ->
        when (val origin = entry.toRoute<SaveUnknown>().origin) {
            SaveOrigin.Wizard -> WizardSaveOutcomePlaceholder(nav, R.string.save_unknown_title)
            SaveOrigin.Contracts -> {
                val vm = existingContractsViewModel(nav)
                SaveUnknownScreen(
                    source = vm?.takeIf { it.state.collectAsState().value.stage == ContractsStage.SAVE_UNKNOWN },
                    draftChanges = vm?.dirtyCount(),
                    onBack = { nav.popBackStack() },
                    onInspect = { nav.openEditor() },
                    onDecided = { nav.showSaveChecked(origin) },
                )
            }
            SaveOrigin.Editor -> {
                val vm = editorViewModel(nav)
                val state by vm.state.collectAsState()
                val pending = state is EditorState.SaveUnknown || state is EditorState.Checking
                SaveUnknownScreen(
                    source = vm.takeIf { pending },
                    draftChanges = null,
                    onBack = { nav.popBackStack() },
                    onInspect = null,
                    onDecided = { nav.showSaveChecked(origin) },
                )
            }
        }
    }
    composable<SaveChecked>(deepLinks = link(MapPaths.SAVE_CHECKED)) { entry ->
        when (val origin = entry.toRoute<SaveChecked>().origin) {
            SaveOrigin.Wizard -> WizardSaveOutcomePlaceholder(nav, R.string.save_checked_title)
            SaveOrigin.Contracts -> {
                val vm = existingContractsViewModel(nav)
                val ui = vm?.state?.collectAsState()?.value
                if (vm != null) ContractsNavigationEffect(nav, vm)
                SaveCheckedScreen(
                    origin = SaveCheckedOrigin.Contracts,
                    verification = vm?.verification,
                    draftChanges = vm?.dirtyCount(),
                    saving = ui?.stage == ContractsStage.SAVING,
                    canSaveAgain = ui?.canRepeat == true,
                    onBack = { nav.popBackStack() },
                    onContinue = { nav.showSaved(origin) },
                    onSaveAgain = { vm?.saveAgain() },
                    onInspect = { nav.openEditor() },
                )
            }
            SaveOrigin.Editor -> {
                val vm = editorViewModel(nav)
                val state by vm.state.collectAsState()
                EditorNavigationEffect(nav, vm)
                SaveCheckedScreen(
                    origin = SaveCheckedOrigin.Editor,
                    verification = vm.verification,
                    draftChanges = null,
                    saving = state is EditorState.Saving,
                    canSaveAgain = (state as? EditorState.SaveChecked)?.matched == false,
                    onBack = { nav.popBackStack() },
                    onContinue = {
                        vm.close()
                        nav.showSaved(origin)
                    },
                    onSaveAgain = vm::saveAgain,
                    onInspect = null,
                )
            }
        }
    }
}

/** The contracts state holder when the contracts graph is on the back stack (33 and 46 sit above it). */
@Composable
private fun existingContractsViewModel(nav: NavHostController): ContractsViewModel? {
    val owner = remember(nav) { runCatching { nav.getBackStackEntry<ContractsGraph>() }.getOrNull() } ?: return null
    val deps = configFlowDependencies()
    return viewModel(owner, factory = contractsFactory(deps))
}

/** The Wizard verifies its receipt with wizard-get-commit; that side of 33/46 is WP09's. */
@Composable
private fun WizardSaveOutcomePlaceholder(nav: NavHostController, title: Int) {
    PlaceholderScreen(
        title,
        WP09,
        onBack = { nav.popBackStack() },
        argument = "origin=wizard",
        footer = listOf(navAction(R.string.save_unknown_verify, ActionStyle.Primary) { nav.popBackStack() }),
    )
}
