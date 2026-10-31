package phpvpn.android.ui.navigation

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import phpvpn.android.R
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.ComponentPreviews
import phpvpn.android.ui.components.DialogCard
import phpvpn.android.ui.components.PreviewSurface

/**
 * Screen 32 as a modal dialog: Keep editing (primary) closes it and leaves
 * the form untouched, Discard changes (danger) drops the draft.
 */
@Composable
fun UnsavedChangesDialog(onKeepEditing: () -> Unit, onDiscard: () -> Unit) {
    Dialog(onDismissRequest = onKeepEditing, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Box(Modifier.padding(20.dp)) { UnsavedChangesCard(onKeepEditing, onDiscard) }
    }
}

@Composable
private fun UnsavedChangesCard(onKeepEditing: () -> Unit, onDiscard: () -> Unit) {
    DialogCard(
        title = stringResource(R.string.discard_heading),
        modifier = Modifier.fillMaxWidth(),
        eyebrow = stringResource(R.string.discard_eyebrow),
        paragraphs = listOf(stringResource(R.string.discard_text)),
    ) {
        ActionButton(stringResource(R.string.action_keep_editing), onKeepEditing, style = ActionStyle.Primary)
        ActionButton(stringResource(R.string.action_discard_changes), onDiscard, style = ActionStyle.Danger)
    }
}

/**
 * The shared "Unsaved changes" pattern (32) for 12-16, 19-22 and 23: while
 * [hasUnsavedChanges] is true, system Back opens the dialog instead of
 * leaving. Returns the action for explicit Cancel / Discard buttons, which
 * asks the same question (or leaves directly when nothing changed).
 * [onDiscard] decides where Discard leads (the whole wizard, the contracts
 * flow, the editor, or just the current detail).
 */
@Composable
fun rememberUnsavedChangesGuard(hasUnsavedChanges: Boolean, onDiscard: () -> Unit): () -> Unit {
    var asking by rememberSaveable { mutableStateOf(false) }
    BackHandler(enabled = hasUnsavedChanges && !asking) { asking = true }
    if (asking) {
        UnsavedChangesDialog(
            onKeepEditing = { asking = false },
            onDiscard = {
                asking = false
                onDiscard()
            },
        )
    }
    return { if (hasUnsavedChanges) asking = true else onDiscard() }
}

@ComponentPreviews
@Composable
private fun UnsavedChangesPreview() = PreviewSurface {
    UnsavedChangesCard({}, {})
}
