package phpvpn.android.ui.screens.editor

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.bridge.controller.EditorNotice
import phpvpn.android.bridge.controller.EditorSession
import phpvpn.android.bridge.controller.EditorState
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.CodeBlock
import phpvpn.android.ui.components.DialogCard
import phpvpn.android.ui.components.Eyebrow
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Hint
import phpvpn.android.ui.components.Intro
import phpvpn.android.ui.components.MetaLabel
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.ScreenScaffold
import phpvpn.android.ui.components.TextInput
import phpvpn.android.ui.components.TopBar
import phpvpn.android.ui.screens.secure.SecureWindow
import phpvpn.android.ui.screens.contracts.availabilityCode
import phpvpn.android.ui.screens.errorAction
import phpvpn.android.ui.screens.errorMessage

/** Callbacks of the editor route (23, 39). */
class EditorActions(
    val onLeave: () -> Unit,
    val onReset: () -> Unit,
    val onVerifySave: () -> Unit,
)

/** The session behind every editing-like state. */
internal fun EditorState.session(): EditorSession? = when (this) {
    is EditorState.Editing -> session
    is EditorState.Resetting -> session
    is EditorState.Saving -> session
    is EditorState.SaveUnknown -> session
    is EditorState.Checking -> session
    is EditorState.SaveChecked -> session
    else -> null
}

/**
 * 23 Configuration editor with its variant 39 JSON error. The text is the
 * actual document from `get-config-document`, monospace, with the private
 * app path of the service instead of a desktop path. FLAG_SECURE.
 */
@Composable
fun EditorScreen(vm: EditorViewModel, state: EditorState, actions: EditorActions) {
    SecureWindow()
    val rejection = vm.rejection
    val session = state.session()
    when {
        session == null -> EditorStatus(vm, state, actions)
        rejection != null && state is EditorState.Editing -> JsonErrorContent(vm, rejection)
        else -> EditorContent(vm, state, session, actions)
    }
}

@Composable
private fun EditorContent(vm: EditorViewModel, state: EditorState, session: EditorSession, actions: EditorActions) {
    val editing = state is EditorState.Editing
    val unknown = state is EditorState.SaveUnknown || state is EditorState.Checking || state is EditorState.SaveChecked
    ScreenScaffold(
        topBar = { TopBar(stringResource(R.string.editor_title), onBack = actions.onLeave) },
        footer = {
            if (unknown) {
                ActionButton(stringResource(R.string.save_unknown_verify), actions.onVerifySave, style = ActionStyle.Primary)
            } else {
                ActionButton(
                    stringResource(if (state is EditorState.Saving) R.string.config_saving else R.string.editor_save),
                    vm::save,
                    Modifier.testTag(TAG_EDITOR_SAVE),
                    style = ActionStyle.Primary,
                    enabled = editing,
                )
            }
            ActionButton(stringResource(R.string.action_discard_changes), actions.onLeave, style = ActionStyle.Ghost, enabled = state !is EditorState.Saving)
        },
    ) {
        Eyebrow(stringResource(R.string.editor_eyebrow))
        Heading(stringResource(R.string.editor_heading))
        Intro(stringResource(R.string.editor_intro))
        when (vm.createdNow) {
            true -> Notice(stringResource(R.string.editor_created_text), kind = NoticeKind.Success, title = stringResource(R.string.editor_created_title))
            false -> Notice(stringResource(R.string.editor_existing_text), kind = NoticeKind.Info, title = stringResource(R.string.editor_existing_title))
            null -> Unit
        }
        if (session.availability == ConfigAvailability.INVALID || session.availability == ConfigAvailability.REPAIRABLE) {
            Notice(stringResource(R.string.editor_document_invalid_text), kind = NoticeKind.Warn, title = stringResource(R.string.editor_document_invalid_title))
        }
        when (val notice = (state as? EditorState.Editing)?.notice) {
            EditorNotice.NotSent -> Notice(stringResource(R.string.config_save_not_sent), kind = NoticeKind.Error)
            is EditorNotice.ResetFailed -> Notice(stringResource(R.string.editor_reset_failed), kind = NoticeKind.Error)
            else -> Unit
        }
        if (unknown) {
            Notice(stringResource(R.string.editor_unknown_text), kind = NoticeKind.Warn, title = stringResource(R.string.save_unknown_notice_title))
        }
        DocumentField(
            vm,
            label = stringResource(if (vm.dirty) R.string.editor_field_dirty else R.string.editor_field),
            enabled = editing,
            error = null,
        )
        Hint(stringResource(R.string.editor_path, session.configPath))
        Notice(stringResource(R.string.editor_notice_text), kind = NoticeKind.Warn, title = stringResource(R.string.editor_notice_title))
        ActionButton(stringResource(R.string.editor_reset), actions.onReset, Modifier.testTag(TAG_EDITOR_RESET), style = ActionStyle.Ghost, enabled = editing)
    }
}

/** The document as a multi-line monospace field without autocorrect or suggestions. */
@Composable
private fun DocumentField(vm: EditorViewModel, label: String, enabled: Boolean, error: String?) {
    TextInput(
        value = vm.text,
        onValueChange = vm::edit,
        label = label,
        modifier = Modifier.testTag(TAG_EDITOR_TEXT),
        error = error,
        enabled = enabled,
        singleLine = false,
        minLines = 12,
        monospace = true,
        keyboardOptions = KeyboardOptions(
            capitalization = KeyboardCapitalization.None,
            autoCorrectEnabled = false,
            keyboardType = KeyboardType.Ascii,
        ),
    )
}

/**
 * 39 JSON error: the validator's text when the service supplies one (with its
 * line number), otherwise the text of the error code. Syntax errors and valid
 * JSON that breaks a constraint have different titles. The draft stays.
 */
@Composable
private fun JsonErrorContent(vm: EditorViewModel, rejection: EditorNotice.Rejected) {
    ScreenScaffold(
        topBar = { TopBar(stringResource(R.string.editor_invalid_title), onBack = vm::dismissRejection) },
        footer = { ActionButton(stringResource(R.string.editor_invalid_back), vm::dismissRejection, Modifier.testTag(TAG_EDITOR_BACK), style = ActionStyle.Primary) },
    ) {
        Eyebrow(stringResource(R.string.editor_invalid_eyebrow))
        Heading(stringResource(R.string.editor_invalid_heading))
        val detail = rejection.validationDescription.ifEmpty { errorMessage(rejection.code.wireName) }
        Notice(
            detail + " " + stringResource(R.string.editor_invalid_changes_kept),
            Modifier.testTag(TAG_EDITOR_ERROR),
            kind = NoticeKind.Error,
            title = stringResource(
                if (rejection.code == ErrorCode.CONFIG_INVALID_JSON) R.string.editor_invalid_notice_title else R.string.editor_invalid_value_title,
            ),
        )
        DocumentField(vm, stringResource(R.string.editor_field_dirty), enabled = true, error = stringResource(R.string.editor_invalid_error))
    }
}

/** Loading, unreadable document (36 rules) and failed reads inside the editor route. */
@Composable
private fun EditorStatus(vm: EditorViewModel, state: EditorState, actions: EditorActions) {
    ScreenScaffold(
        topBar = { TopBar(stringResource(R.string.editor_title), onBack = actions.onLeave) },
        footer = when (state) {
            is EditorState.Failed, is EditorState.Unavailable -> {
                {
                    ActionButton(stringResource(R.string.config_action_try_again), vm::open, style = ActionStyle.Primary)
                    ActionButton(stringResource(R.string.config_action_close), actions.onLeave, style = ActionStyle.Ghost)
                }
            }
            else -> null
        },
    ) {
        Eyebrow(stringResource(R.string.editor_eyebrow))
        when (state) {
            is EditorState.Unavailable -> {
                Heading(stringResource(R.string.attention_heading))
                val code = state.reasonCode ?: availabilityCode(state.availability)
                Notice(errorAction(code), kind = NoticeKind.Error, title = errorMessage(code))
                Hint(stringResource(R.string.config_path_line, state.configPath))
            }
            is EditorState.Failed -> {
                Heading(stringResource(R.string.editor_failed_title))
                Notice(errorAction(state.failure.code), kind = NoticeKind.Error, title = errorMessage(state.failure.code))
                state.failure.configPath?.let { Hint(stringResource(R.string.config_path_line, it)) }
            }
            else -> {
                Heading(stringResource(R.string.editor_heading))
                Hint(stringResource(R.string.editor_loading))
            }
        }
    }
}

/**
 * 24 Reset configuration (confirmation) and 42 Default editor content (the
 * result). Reset uses get-default-config and changes only the draft; the
 * file changes only with Save.
 */
@Composable
fun EditorResetScreen(
    vm: EditorViewModel,
    state: EditorState,
    onKeep: () -> Unit,
    onContinue: () -> Unit,
    onWizard: () -> Unit,
) {
    SecureWindow()
    if (vm.resetLoaded == true) {
        ScreenScaffold(
            topBar = { TopBar(stringResource(R.string.editor_default_title), onBack = onContinue) },
            footer = { ActionButton(stringResource(R.string.editor_default_continue), onContinue, Modifier.testTag(TAG_RESET_CONTINUE), style = ActionStyle.Primary) },
        ) {
            Eyebrow(stringResource(R.string.discard_eyebrow))
            Heading(stringResource(R.string.editor_default_heading))
            Notice(stringResource(R.string.editor_default_notice_text), kind = NoticeKind.Info, title = stringResource(R.string.editor_default_notice_title))
            MetaLabel(stringResource(R.string.editor_default_document))
            CodeBlock(vm.text)
            Hint(stringResource(R.string.editor_default_hint))
            ActionButton(stringResource(R.string.editor_default_wizard), onWizard)
        }
        return
    }
    val busy = state is EditorState.Resetting
    ScreenScaffold(topBar = { TopBar(stringResource(R.string.reset_title), onBack = onKeep) }) {
        Spacer(Modifier.height(36.dp))
        DialogCard(
            stringResource(R.string.reset_heading),
            eyebrow = stringResource(R.string.reset_eyebrow),
            paragraphs = listOf(stringResource(R.string.reset_text), stringResource(R.string.reset_text_save)),
        ) {
            ActionButton(stringResource(R.string.reset_confirm), vm::reset, Modifier.testTag(TAG_RESET_CONFIRM), style = ActionStyle.Danger, enabled = !busy)
            ActionButton(stringResource(R.string.reset_keep), onKeep, enabled = !busy)
        }
    }
}

/**
 * 36a Create default configuration (dialog over 00, 10 and 36): the actual
 * absolute path from the service; Yes sends create-default-config and opens
 * the actual result, No creates nothing.
 */
@Composable
fun CreateDefaultDialogContent(state: EditorState, onYes: () -> Unit, onNo: () -> Unit) {
    val paragraphs = buildList {
        when (state) {
            is EditorState.ConfirmCreate -> add(stringResource(R.string.create_default_text, state.configPath))
            is EditorState.Creating -> add(stringResource(R.string.create_default_creating))
            is EditorState.Failed -> {
                add(stringResource(R.string.create_default_failed))
                add(errorMessage(state.failure.code) + " " + errorAction(state.failure.code))
                state.failure.configPath?.let { add(stringResource(R.string.config_path_line, it)) }
            }
            is EditorState.Unavailable -> {
                val code = state.reasonCode ?: availabilityCode(state.availability)
                add(errorMessage(code) + " " + errorAction(code))
                add(stringResource(R.string.config_path_line, state.configPath))
            }
            else -> add(stringResource(R.string.create_default_checking))
        }
    }
    // Scrolls with large fonts on narrow screens (320 dp, 200 %) so Yes and No stay reachable.
    Box(Modifier.verticalScroll(rememberScrollState()).padding(20.dp).testTag(TAG_CREATE_DIALOG)) {
        DialogCard(stringResource(R.string.create_default_title), Modifier.fillMaxWidth(), paragraphs = paragraphs) {
            if (state is EditorState.ConfirmCreate) {
                ActionButton(stringResource(R.string.common_yes), onYes, Modifier.testTag(TAG_CREATE_YES), style = ActionStyle.Primary)
                ActionButton(stringResource(R.string.common_no), onNo, Modifier.testTag(TAG_CREATE_NO))
            } else {
                ActionButton(stringResource(R.string.config_action_close), onNo, Modifier.testTag(TAG_CREATE_NO), enabled = state !is EditorState.Creating)
            }
        }
    }
}

const val TAG_EDITOR_TEXT = "editor-text"
const val TAG_EDITOR_SAVE = "editor-save"
const val TAG_EDITOR_RESET = "editor-reset"
const val TAG_EDITOR_ERROR = "editor-error"
const val TAG_EDITOR_BACK = "editor-back"
const val TAG_RESET_CONFIRM = "reset-confirm"
const val TAG_RESET_CONTINUE = "reset-continue"
const val TAG_CREATE_DIALOG = "create-default-dialog"
const val TAG_CREATE_YES = "create-default-yes"
const val TAG_CREATE_NO = "create-default-no"
