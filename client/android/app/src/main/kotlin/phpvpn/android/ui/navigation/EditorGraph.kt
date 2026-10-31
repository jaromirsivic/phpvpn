package phpvpn.android.ui.navigation

import androidx.compose.ui.res.stringResource
import androidx.compose.ui.window.DialogProperties
import androidx.navigation.NavGraphBuilder
import androidx.navigation.NavHostController
import androidx.navigation.compose.composable
import androidx.navigation.compose.dialog
import androidx.navigation.compose.navigation
import androidx.navigation.toRoute
import phpvpn.android.R
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.MainDestination

/**
 * WP10: the configuration editor (23, 39), Reset (24, 42) and the dialog 36a
 * that creates the default configuration. Still the WP07 placeholder.
 */
internal fun NavGraphBuilder.editor(nav: NavHostController) {
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
}

/**
 * Shared save outcomes: Changes saved (25), Save result unknown (33) and Save
 * checked (46). The Wizard (WP09), the contracts and the editor (WP10) all
 * navigate here with their [SaveOrigin].
 */
internal fun NavGraphBuilder.saveOutcomes(nav: NavHostController) {
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
}
