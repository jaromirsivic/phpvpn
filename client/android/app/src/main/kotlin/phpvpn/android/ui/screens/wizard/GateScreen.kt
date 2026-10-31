package phpvpn.android.ui.screens.wizard

import androidx.activity.compose.BackHandler
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavController
import phpvpn.android.R
import phpvpn.android.bridge.controller.WizardStep
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.ConnectionPhase
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.Eyebrow
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Hint
import phpvpn.android.ui.components.InfoRow
import phpvpn.android.ui.components.Intro
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.Tag
import phpvpn.android.ui.components.TagKind
import phpvpn.android.ui.navigation.leaveWizard
import phpvpn.android.ui.navigation.rememberUnsavedChangesGuard

/** 11 in the graph: the session decides whether the gate is needed at all. */
@Composable
fun WizardGateRoute(nav: NavController, entry: NavBackStackEntry) {
    val vm = wizardViewModel(nav, entry)
    val state by vm.state.collectAsStateWithLifecycle()
    WizardNavigationEffect(vm, nav)
    val leave = rememberUnsavedChangesGuard(state.dirty) { if (vm.leave()) nav.leaveWizard() }
    // The gate has no step behind it: Back always means leaving the Wizard.
    BackHandler { leave() }
    GateScreen(state, onLeave = leave, onDisconnect = { vm.disconnectAndContinue() })
}

/**
 * 11 Before setup (disconnectGate): explains the global interruption before
 * the action. "Disconnect and continue" sends one disconnect and waits for a
 * confirmed disconnected and idle state, then one wizard-resume; an already
 * disconnected client gets "Continue" (resume only). "Keep connected" leaves
 * the Wizard and the connection untouched. After a foreign Connect the screen
 * says so and keeps every entry.
 */
@Composable
fun GateScreen(state: WizardScreenState, onLeave: () -> Unit, onDisconnect: () -> Unit) {
    val working = state.disconnecting || state.busy || state.step == WizardStep.LOADING
    WizardScaffold(
        title = stringResource(R.string.gate_title),
        onBack = onLeave,
        footer = {
            ActionButton(
                stringResource(if (state.offlineKnown) R.string.wp09_gate_continue else R.string.gate_disconnect_continue),
                onDisconnect,
                style = ActionStyle.Primary,
                enabled = state.step == WizardStep.DISCONNECT_GATE && !working,
            )
            ActionButton(stringResource(R.string.gate_keep_connected), onLeave, style = ActionStyle.Ghost)
        },
    ) {
        Eyebrow(stringResource(R.string.wizard_eyebrow))
        Heading(stringResource(R.string.gate_heading))
        Intro(stringResource(R.string.gate_intro))
        val interrupted = state.error?.code == ErrorCode.WIZARD_CONNECTION_INTERRUPTED.wireName
        if (interrupted) {
            Notice(
                stringResource(R.string.wp09_gate_interrupted_text),
                kind = NoticeKind.Warn,
                title = stringResource(R.string.wp09_gate_interrupted_title),
            )
        }
        if (state.step != WizardStep.LOADING) {
            Card {
                InfoRow(stringResource(R.string.gate_current_status)) { StatusTag(state) }
                if (state.virtualIp.isNotEmpty()) {
                    InfoRow(stringResource(R.string.row_virtual_ip), state.virtualIp, divider = true)
                }
            }
        }
        if (state.offlineKnown) {
            Notice(stringResource(R.string.wp09_gate_offline), kind = NoticeKind.Info)
        } else {
            Notice(
                stringResource(R.string.gate_notice_text),
                kind = NoticeKind.Warn,
                title = stringResource(R.string.gate_notice_title),
            )
            Hint(stringResource(R.string.wp09_gate_no_reconnect))
        }
        WizardProgress(state)
        val failure = state.error
        if (failure != null && !interrupted && !state.disconnecting) {
            val text = if (failure.code == ErrorCode.IPC_UNAVAILABLE.wireName) {
                stringResource(R.string.wp09_gate_disconnect_failed)
            } else {
                errorMessageText(failure.code) + "\n" + errorActionText(failure.code, failure.field)
            }
            Notice(text, kind = NoticeKind.Error, title = stringResource(R.string.wp09_action_failed_title))
        }
    }
}

@Composable
private fun StatusTag(state: WizardScreenState) {
    val vpnState = state.vpnState
    val (text, kind) = when {
        state.offlineKnown -> R.string.wp09_gate_status_disconnected to TagKind.Neutral
        state.disconnecting || state.vpnPhase == ConnectionPhase.STOPPING ->
            R.string.wp09_gate_status_disconnecting to TagKind.Warn
        vpnState == ConnectionState.DISCONNECTED -> R.string.wp09_gate_status_disconnected to TagKind.Neutral
        vpnState == ConnectionState.CONNECTING -> R.string.wp09_gate_status_connecting to TagKind.Warn
        vpnState == ConnectionState.RECOVERING || state.vpnPhase == ConnectionPhase.RECONNECTING ->
            R.string.wp09_gate_status_reconnecting to TagKind.Warn
        vpnState == ConnectionState.ERROR -> R.string.wp09_gate_status_error to TagKind.Error
        else -> R.string.tag_connected_short to TagKind.Success
    }
    Tag(stringResource(text), kind = kind)
}
