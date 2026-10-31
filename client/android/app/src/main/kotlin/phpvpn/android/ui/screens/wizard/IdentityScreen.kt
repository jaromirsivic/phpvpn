package phpvpn.android.ui.screens.wizard

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavController
import phpvpn.android.R
import phpvpn.android.bridge.ipc.OperationKind
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Hint
import phpvpn.android.ui.components.Intro
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.SecretField
import phpvpn.android.ui.components.TextInput
import phpvpn.android.ui.components.ToggleRow
import phpvpn.android.ui.navigation.leaveWizard
import phpvpn.android.ui.screens.secure.SecureWindow

/** 15 in the graph. */
@Composable
fun WizardIdentityRoute(nav: NavController, entry: NavBackStackEntry) {
    val vm = wizardViewModel(nav, entry)
    val state by vm.state.collectAsStateWithLifecycle()
    WizardNavigationEffect(vm, nav)
    val cancel = rememberCancelSetup(state.dirty) { if (vm.leave()) nav.leaveWizard() }
    BackHandler { vm.back() }
    IdentityScreen(
        state = state,
        onBack = { vm.back() },
        onIp = vm::setIp,
        onAutoDetect = vm::setAutoDetectIp,
        onKey = vm::setAccessKey,
        onDefaultKey = vm::setDefaultKey,
        onVerify = { vm.verify() },
        onCancel = cancel,
    )
}

/**
 * 15 IP and access key. Automatic detection uses only the verified
 * suggestion of the last secret check and disables the IP field; without a
 * suggestion the screen says so and manual entry needs the switch off. The
 * default key (hex of the last octet, 32 times, the shared pure function)
 * disables the key field and follows every IP change; the service verifies
 * it again in check-access. The messages warning is always visible before
 * the action. The window is secure while this screen is shown.
 */
@Composable
fun IdentityScreen(
    state: WizardScreenState,
    onBack: () -> Unit,
    onIp: (String) -> Unit,
    onAutoDetect: (Boolean) -> Unit,
    onKey: (String) -> Unit,
    onDefaultKey: (Boolean) -> Unit,
    onVerify: () -> Unit,
    onCancel: () -> Unit,
) {
    SecureWindow()
    WizardScaffold(
        title = stringResource(R.string.wizard_identity_title),
        onBack = onBack,
        step = 3,
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
        Heading(stringResource(R.string.wizard_identity_heading))
        Intro(stringResource(R.string.wizard_identity_network, state.vpnCidr.orEmpty()))
        Hint(stringResource(R.string.wp09_identity_not_reservation))
        Card {
            TextInput(
                value = state.virtualIp,
                onValueChange = onIp,
                label = stringResource(R.string.wizard_identity_ip_field),
                error = fieldError(state, WizardViewModel.FIELD_IP),
                enabled = state.editable && !state.autoDetectIp,
                keyboardOptions = KeyboardOptions(autoCorrectEnabled = false, keyboardType = KeyboardType.Decimal, imeAction = ImeAction.Next),
            )
            ToggleRow(
                stringResource(R.string.wizard_identity_suggest_ip),
                state.autoDetectIp,
                onAutoDetect,
                enabled = state.editable,
            )
        }
        Card {
            SecretField(
                value = rememberRevealed(state.accessKey),
                onValueChange = onKey,
                label = stringResource(R.string.wizard_identity_key_field),
                hint = stringResource(if (state.useDefaultKey) R.string.wp09_identity_default_key_hint else R.string.wizard_identity_key_hint),
                error = fieldError(state, WizardViewModel.FIELD_KEY),
                enabled = state.editable && !state.useDefaultKey,
                monospace = true,
            )
            ToggleRow(
                stringResource(R.string.wizard_identity_default_key),
                state.useDefaultKey,
                onDefaultKey,
                enabled = state.editable,
            )
        }
        if (state.autoDetectIp && state.suggestedIp == null) {
            Notice(
                stringResource(R.string.wizard_identity_notice_text),
                kind = NoticeKind.Info,
                title = stringResource(R.string.wizard_identity_notice_title),
            )
        }
        Notice(
            stringResource(R.string.wp09_identity_messages_text),
            kind = NoticeKind.Warn,
            title = stringResource(R.string.wp09_identity_messages_title),
        )
        WizardProgress(state)
        StepErrorNotice(state, OperationKind.ACCESS)
    }
}
