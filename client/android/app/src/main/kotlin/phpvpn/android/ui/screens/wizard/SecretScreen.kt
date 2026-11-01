package phpvpn.android.ui.screens.wizard

import androidx.activity.compose.BackHandler
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavController
import phpvpn.android.R
import phpvpn.android.bridge.ipc.CheckpointResult
import phpvpn.android.bridge.ipc.OperationKind
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Intro
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.SecretField
import phpvpn.android.ui.components.ToggleRow
import phpvpn.android.ui.navigation.DiagnosticResponse
import phpvpn.android.ui.navigation.DiagnosticSource
import phpvpn.android.ui.navigation.leaveWizard
import phpvpn.android.ui.screens.secure.SecureWindow

/** 14 in the graph. */
@Composable
fun WizardSecretRoute(nav: NavController, entry: NavBackStackEntry) {
    val vm = wizardViewModel(nav, entry)
    val state by vm.state.collectAsStateWithLifecycle()
    WizardNavigationEffect(vm, nav)
    val cancel = rememberCancelSetup(state.dirty) { if (vm.leave()) nav.leaveWizard() }
    // Back is the desktop Back: the previous step with the draft kept.
    BackHandler { vm.back() }
    SecretScreen(
        state = state,
        onBack = { vm.back() },
        onSecret = vm::setSecret,
        onDefaultSecret = vm::setDefaultSecret,
        onVerify = { vm.verify() },
        onCancel = cancel,
        onHttpDetail = { nav.navigate(DiagnosticResponse(DiagnosticSource.Wizard)) },
    )
}

/**
 * 14 Server secret: own or default secret (the default switch disables the
 * field and puts the publicly known default in it), hidden by default with
 * a temporary Show, a JSON string literal form for secrets with control
 * characters. The window is secure while this screen is shown. Errors never
 * repeat the secret.
 */
@Composable
fun SecretScreen(
    state: WizardScreenState,
    onBack: () -> Unit,
    onSecret: (String) -> Unit,
    onDefaultSecret: (Boolean) -> Unit,
    onVerify: () -> Unit,
    onCancel: () -> Unit,
    onHttpDetail: () -> Unit,
) {
    SecureWindow()
    WizardScaffold(
        title = stringResource(R.string.wizard_secret_title),
        onBack = onBack,
        step = 2,
        footer = {
            ActionButton(
                stringResource(R.string.wizard_verify_continue),
                onVerify,
                style = ActionStyle.Primary,
                enabled = state.editable,
            )
            ActionButton(stringResource(R.string.wizard_cancel_setup), onCancel, style = ActionStyle.Ghost, enabled = !state.committing)
        },
    ) {
        Heading(stringResource(R.string.wizard_secret_heading))
        Intro(stringResource(R.string.wizard_secret_intro))
        if (state.checks[OperationKind.URL.ordinal] == CheckpointResult.SUCCESS) {
            Notice(
                WizardRules.serverUrlHost(state.serverUrl) ?: state.serverUrl,
                kind = NoticeKind.Success,
                title = stringResource(R.string.wizard_secret_url_verified),
            )
        }
        SecretField(
            value = rememberRevealed(state.secretText),
            onValueChange = onSecret,
            label = stringResource(if (state.literalSecret) R.string.wp09_secret_literal_field else R.string.wizard_secret_field),
            hint = stringResource(if (state.literalSecret) R.string.wp09_secret_literal_hint else R.string.wp09_secret_hint),
            error = fieldError(state, WizardViewModel.FIELD_SECRET),
            enabled = state.editable && !state.useDefaultSecret,
            monospace = state.literalSecret,
        )
        Card {
            ToggleRow(
                stringResource(R.string.wizard_secret_use_default),
                state.useDefaultSecret,
                onDefaultSecret,
                enabled = state.editable,
            )
        }
        // Always visible as in the design: the default switch is right above it.
        Notice(
            stringResource(R.string.wizard_secret_notice_text),
            kind = NoticeKind.Warn,
            title = stringResource(R.string.wizard_secret_notice_title),
        )
        WizardProgress(state)
        StepErrorNotice(state, OperationKind.SECRET)
        HttpDetailEntry(state, onHttpDetail)
    }
}
