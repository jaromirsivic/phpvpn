package phpvpn.android.ui.screens.wizard

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavController
import phpvpn.android.R
import phpvpn.android.bridge.controller.WizardStep
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.OperationKind
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.DialogCard
import phpvpn.android.ui.components.EntryRow
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Intro
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.TextInput
import phpvpn.android.ui.navigation.DiagnosticResponse
import phpvpn.android.ui.navigation.DiagnosticSource
import phpvpn.android.ui.navigation.WizardHeaders
import phpvpn.android.ui.navigation.leaveWizard
import phpvpn.android.ui.navigation.openEditor
import phpvpn.android.ui.navigation.rememberUnsavedChangesGuard

/** 12 in the graph, with Reset (40) as its dialog. */
@Composable
fun WizardUrlRoute(nav: NavController, entry: NavBackStackEntry) {
    val vm = wizardViewModel(nav, entry)
    val state by vm.state.collectAsStateWithLifecycle()
    WizardNavigationEffect(vm, nav)
    // Back on 12 means the header editor (13) was left: its unaccepted text is gone.
    LaunchedEffect(Unit) { vm.endHeadersEdit() }
    val leave = rememberUnsavedChangesGuard(state.dirty) { if (vm.leave()) nav.leaveWizard() }
    // The first step has no previous step (its desktop Back is disabled): Back leaves the Wizard.
    BackHandler(enabled = !state.committing) { leave() }
    var resetAsked by rememberSaveable { mutableStateOf(false) }
    UrlScreen(
        state = state,
        onBack = leave,
        onUrl = vm::setUrl,
        onHeaders = { nav.navigate(WizardHeaders) { launchSingleTop = true } },
        onVerify = { vm.verify() },
        onCancel = leave,
        onReset = { resetAsked = true },
        onRetryOpen = { vm.reopen() },
        onInspect = { nav.openEditor() },
        onHttpDetail = { nav.navigate(DiagnosticResponse(DiagnosticSource.Wizard)) },
    )
    if (resetAsked) {
        WizardResetDialog(
            onStartOver = {
                resetAsked = false
                vm.reset()
            },
            onKeepEditing = { resetAsked = false },
        )
    }
}

/**
 * 12 Server address: URL input and the separate HTTP headers draft. Verify
 * changes the screen only after the real result of the check (the
 * navigation follows the session state). A malformed or placeholder URL is
 * rejected before any request; a transport failure never marks a valid URL
 * as invalid, an incompatible server version does. A configuration the
 * service could not read offers Start with defaults and the editor.
 */
@Composable
fun UrlScreen(
    state: WizardScreenState,
    onBack: () -> Unit,
    onUrl: (String) -> Unit,
    onHeaders: () -> Unit,
    onVerify: () -> Unit,
    onCancel: () -> Unit,
    onReset: () -> Unit,
    onRetryOpen: () -> Unit,
    onInspect: () -> Unit,
    onHttpDetail: () -> Unit,
) {
    val loadError = state.step == WizardStep.LOAD_ERROR
    WizardScaffold(
        title = stringResource(R.string.wizard_url_title),
        onBack = onBack,
        step = 1,
        footer = {
            ActionButton(
                stringResource(R.string.wizard_verify_continue),
                onVerify,
                style = ActionStyle.Primary,
                enabled = state.editable && state.step == WizardStep.SERVER_URL,
            )
            ActionButton(stringResource(R.string.wizard_cancel_setup), onCancel, style = ActionStyle.Ghost, enabled = !state.committing)
        },
    ) {
        Heading(stringResource(R.string.wizard_url_heading))
        Intro(stringResource(R.string.wizard_url_intro))
        if (loadError) {
            LoadErrorBlock(state, onReset, onRetryOpen, onInspect)
        } else {
            UrlForm(state, onUrl, onHeaders, onReset, onHttpDetail)
        }
    }
}

@Composable
private fun UrlForm(
    state: WizardScreenState,
    onUrl: (String) -> Unit,
    onHeaders: () -> Unit,
    onReset: () -> Unit,
    onHttpDetail: () -> Unit,
) {
    TextInput(
        value = state.serverUrl,
        onValueChange = onUrl,
        label = stringResource(R.string.wizard_url_field),
        hint = stringResource(R.string.wizard_url_hint),
        error = fieldError(state, WizardViewModel.FIELD_URL),
        enabled = state.editable,
        keyboardOptions = KeyboardOptions(autoCorrectEnabled = false, keyboardType = KeyboardType.Uri, imeAction = ImeAction.Done),
    )
    Card {
        EntryRow(
            stringResource(R.string.wizard_url_headers),
            onClick = { if (state.editable) onHeaders() },
            detail = stringResource(R.string.wizard_url_headers_detail),
        )
    }
    Notice(
        stringResource(R.string.wizard_url_notice_text),
        kind = NoticeKind.Info,
        title = stringResource(R.string.wizard_url_notice_title),
    )
    WizardProgress(state)
    StepErrorNotice(state, OperationKind.URL)
    HttpDetailEntry(state, onHttpDetail)
    ActionButton(
        stringResource(R.string.wizard_url_defaults),
        onReset,
        style = ActionStyle.Ghost,
        enabled = !state.committing && !state.resetting && state.step != WizardStep.LOADING,
    )
}

@Composable
private fun LoadErrorBlock(state: WizardScreenState, onReset: () -> Unit, onRetryOpen: () -> Unit, onInspect: () -> Unit) {
    val reason = when (state.availability) {
        ConfigAvailability.UNREADABLE -> R.string.wp09_load_error_unreadable
        ConfigAvailability.ENCODING_ERROR -> R.string.wp09_load_error_encoding
        ConfigAvailability.TOO_LARGE -> R.string.wp09_load_error_too_large
        ConfigAvailability.INVALID -> R.string.wp09_load_error_invalid
        else -> R.string.wp09_load_error_open
    }
    val opened = state.availability != null
    val details = buildList {
        add(stringResource(reason))
        if (!opened) state.error?.let { add(errorMessageText(it.code)) }
        if (opened) add(stringResource(R.string.wp09_load_error_action))
    }
    Notice(details.joinToString("\n"), kind = NoticeKind.Error, title = stringResource(R.string.wp09_load_error_title))
    WizardProgress(state)
    if (opened) {
        ActionButton(stringResource(R.string.wizard_url_defaults), onReset, style = ActionStyle.Primary, enabled = !state.resetting)
    } else {
        ActionButton(stringResource(R.string.wp09_try_again), onRetryOpen, style = ActionStyle.Primary)
    }
    ActionButton(stringResource(R.string.action_inspect_configuration), onInspect)
}

/**
 * 40 Reset wizard: the draft returns to the service defaults (placeholder
 * URL, default secret, new defaults); the saved file changes only with
 * Finish.
 */
@Composable
fun WizardResetDialog(onStartOver: () -> Unit, onKeepEditing: () -> Unit) {
    Dialog(onDismissRequest = onKeepEditing, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Box(Modifier.padding(20.dp)) { WizardResetCard(onStartOver, onKeepEditing) }
    }
}

@Composable
internal fun WizardResetCard(onStartOver: () -> Unit, onKeepEditing: () -> Unit) {
    DialogCard(
        title = stringResource(R.string.wizard_reset_heading),
        modifier = Modifier.fillMaxWidth(),
        eyebrow = stringResource(R.string.wizard_eyebrow),
        paragraphs = listOf(stringResource(R.string.wizard_reset_text), stringResource(R.string.wizard_reset_text_saved)),
    ) {
        ActionButton(stringResource(R.string.wizard_reset_confirm), onStartOver, style = ActionStyle.Danger)
        ActionButton(stringResource(R.string.action_keep_editing), onKeepEditing)
    }
}
