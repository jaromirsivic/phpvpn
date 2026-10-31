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
import kotlinx.coroutines.flow.MutableStateFlow
import phpvpn.android.bridge.controller.EditorState
import phpvpn.android.ui.components.MainDestination
import phpvpn.android.ui.screens.configFlowDependencies
import phpvpn.android.ui.screens.editor.CreateDefaultDialogContent
import phpvpn.android.ui.screens.editor.EditorActions
import phpvpn.android.ui.screens.editor.EditorNavigation
import phpvpn.android.ui.screens.editor.EditorResetScreen
import phpvpn.android.ui.screens.editor.EditorScreen
import phpvpn.android.ui.screens.editor.EditorViewModel
import phpvpn.android.ui.screens.save.SaveOutcomePhase
import phpvpn.android.ui.screens.save.SaveOutcomeScreen
import phpvpn.android.ui.screens.save.SaveOutcomeState
import phpvpn.android.ui.screens.save.followSaveOutcome
import phpvpn.android.ui.screens.save.rememberSaveOutcomeSource
import phpvpn.android.ui.screens.save.SavedScreen
import phpvpn.android.ui.screens.save.SavedViewModel

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
 * checked (46). 33 and 46 render the registered [SaveOutcomeSource] of their
 * origin (Wizard of WP09, contracts and editor of WP10; contract in
 * SaveOutcomeSource.kt) and follow every phase change with
 * [followSaveOutcome]: CONFIRMED → 17 (Wizard) or 25, NOT_SAVED → 16
 * (Wizard) or 46, CONFLICT → 46.
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
        SaveOutcomeRoute(nav, entry.toRoute<SaveUnknown>().origin, onChecked = false)
    }
    composable<SaveChecked>(deepLinks = link(MapPaths.SAVE_CHECKED)) { entry ->
        SaveOutcomeRoute(nav, entry.toRoute<SaveChecked>().origin, onChecked = true)
    }
}

@Composable
private fun SaveOutcomeRoute(nav: NavHostController, origin: SaveOrigin, onChecked: Boolean) {
    val source = rememberSaveOutcomeSource(origin)
    val idle = remember { MutableStateFlow(SaveOutcomeState()) }
    val state by (source?.state ?: idle).collectAsState()
    LaunchedEffect(origin, state.phase) {
        // On 46 the rule for CONFLICT (and NOT_SAVED outside the Wizard) is 46 itself:
        // navigating there again would replace this entry and run this effect forever.
        val staysOn46 = state.phase == SaveOutcomePhase.CONFLICT ||
            (state.phase == SaveOutcomePhase.NOT_SAVED && origin != SaveOrigin.Wizard)
        if (!(onChecked && staysOn46)) nav.followSaveOutcome(origin, state.phase)
    }
    SaveOutcomeScreen(
        origin,
        source,
        state,
        onBack = { nav.popBackStack() },
        // The editor's own draft is frozen underneath; a second editor cannot inspect it.
        onInspect = if (origin == SaveOrigin.Editor) null else ({ nav.openEditor() }),
    )
}
