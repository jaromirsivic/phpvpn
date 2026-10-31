package phpvpn.android.ui.screens.wizard

import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import phpvpn.android.R
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.errors.ErrorTexts
import phpvpn.android.bridge.ipc.OperationKind
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.errors.ErrorStrings
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.EntryRow
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.ScreenScaffold
import phpvpn.android.ui.components.Stepper
import phpvpn.android.ui.components.TopBar
import phpvpn.android.ui.navigation.UnsavedChangesDialog

/**
 * Frame of the Wizard screens: top bar with Back, optional stepper (1 to 4),
 * scrolling body and the pinned footer actions.
 */
@Composable
internal fun WizardScaffold(
    title: String,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
    step: Int? = null,
    footer: (@Composable ColumnScope.() -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    ScreenScaffold(
        topBar = { TopBar(title, onBack = onBack) },
        modifier = modifier,
        footer = footer,
    ) {
        if (step != null) Stepper(step)
        content()
    }
}

/** Progress of a running check, save, reset or disconnect (UX08 catalogue), as a polite notice. */
@Composable
internal fun WizardProgress(state: WizardScreenState) {
    val text = progressText(state) ?: return
    Notice(text, kind = NoticeKind.Info)
}

@Composable
private fun progressText(state: WizardScreenState): String? = when {
    state.committing -> stringResource(R.string.wp09_saving)
    state.resetting -> stringResource(R.string.wp09_resetting)
    state.disconnecting -> stringResource(R.string.wp09_waiting_disconnect)
    state.step == phpvpn.android.bridge.controller.WizardStep.LOADING -> stringResource(R.string.wp09_loading)
    state.busy -> when (state.attemptKind) {
        OperationKind.URL -> stringResource(R.string.wp09_checking_url)
        OperationKind.SECRET -> stringResource(R.string.wp09_checking_secret)
        OperationKind.ACCESS -> stringResource(R.string.wp09_checking_access)
        else -> stringResource(R.string.wp09_loading)
    }
    else -> null
}

/** The message text of a received error code: chosen by code, never the received description. */
@Composable
internal fun errorMessageText(code: String): String = stringResource(ErrorStrings.resource(ErrorTexts.messageKey(code)))

/** The suggested action of a Wizard error (`WizardIpc::safeError` by code and field). */
@Composable
internal fun errorActionText(code: String, field: String?): String =
    stringResource(ErrorStrings.resource(ErrorTexts.wizardActionKey(code, field)))

internal fun isTimeout(code: String?): Boolean =
    code == ErrorCode.TIMEOUT.wireName || code == ErrorCode.TRANSPORT_TIMEOUT.wireName

/** Text of a local validation problem (no IPC happened). */
@Composable
internal fun localProblemText(problem: LocalProblem, vpnCidr: String?): String = when (problem) {
    LocalProblem.URL_INVALID -> stringResource(R.string.wp09_url_invalid)
    LocalProblem.URL_PLACEHOLDER -> stringResource(R.string.wp09_url_placeholder)
    LocalProblem.SECRET_LITERAL -> stringResource(R.string.wp09_secret_literal_invalid)
    LocalProblem.NO_SUGGESTION -> stringResource(R.string.wp09_identity_no_suggestion)
    LocalProblem.IP_INVALID -> stringResource(R.string.wp09_identity_ip_invalid, vpnCidr.orEmpty())
    LocalProblem.KEY_INVALID -> stringResource(R.string.wp09_identity_key_invalid)
    LocalProblem.NO_CONTEXT -> errorMessageText(problem.code.wireName)
}

/**
 * Error text under [field], only when the evaluated error marks this field
 * (wizard_window.cpp: transport failures and timeouts never mark an input).
 */
@Composable
internal fun fieldError(state: WizardScreenState, field: String): String? {
    if (state.invalidField != field) return null
    state.localProblem?.let { return localProblemText(it, state.vpnCidr) }
    val error = state.error ?: return null
    return errorMessageText(error.code)
}

/**
 * The failure of this step's last check, shown on the step after 18 was
 * left (the desktop error panel). Timeouts keep the inputs valid.
 */
@Composable
internal fun StepErrorNotice(state: WizardScreenState, kind: OperationKind) {
    val error = state.error ?: return
    if (state.attemptKind != kind || state.busy || state.localProblem != null) return
    val text = if (isTimeout(error.code)) {
        stringResource(R.string.wp09_error_timeout_text)
    } else {
        errorMessageText(error.code) + "\n" + errorActionText(error.code, error.field)
    }
    Notice(text, kind = NoticeKind.Error, title = stringResource(R.string.wp09_action_failed_title))
}

/** Entry to 30/31, only when the failed probe really captured an HTTP exchange. */
@Composable
internal fun HttpDetailEntry(state: WizardScreenState, onOpen: () -> Unit) {
    if (state.httpDiagnostic == null) return
    Card {
        EntryRow(
            stringResource(R.string.entry_http_error_details),
            onClick = onOpen,
            detail = stringResource(R.string.wizard_error_http_detail),
        )
    }
}

/** The text of a [SecretText] for one text field, kept until the secret changes. */
@Composable
internal fun rememberRevealed(secret: SecretText): String = remember(secret) { secret.reveal() }

/**
 * "Cancel setup" of 14 to 16: asks with 32 when the draft has changes. The
 * system Back of these steps goes to the previous step instead (the desktop
 * Back keeps the draft), so this guard does not intercept Back.
 */
@Composable
internal fun rememberCancelSetup(dirty: Boolean, onDiscard: () -> Unit): () -> Unit {
    var asking by rememberSaveable { mutableStateOf(false) }
    if (asking) {
        UnsavedChangesDialog(
            onKeepEditing = { asking = false },
            onDiscard = {
                asking = false
                onDiscard()
            },
        )
    }
    return { if (dirty) asking = true else onDiscard() }
}
