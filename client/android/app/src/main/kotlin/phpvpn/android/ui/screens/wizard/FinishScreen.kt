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
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Hint
import phpvpn.android.ui.components.InfoRow
import phpvpn.android.ui.components.Intro
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.ToggleRow
import phpvpn.android.ui.navigation.leaveWizard
import phpvpn.android.ui.navigation.openEditor
import phpvpn.android.ui.screens.save.SaveOutcomePhase

/** 16 in the graph. */
@Composable
fun WizardFinishRoute(nav: NavController, entry: NavBackStackEntry) {
    val vm = wizardViewModel(nav, entry)
    val state by vm.state.collectAsStateWithLifecycle()
    val save by vm.saveOutcome.state.collectAsStateWithLifecycle()
    WizardNavigationEffect(vm, nav)
    val cancel = rememberCancelSetup(state.dirty) { if (vm.leave()) nav.leaveWizard() }
    // During the commit nothing may pretend a successful Cancel; an unknown
    // save has no previous step, leaving asks 32.
    BackHandler {
        when {
            state.committing -> Unit
            state.step == WizardStep.COMMIT_UNKNOWN -> cancel()
            else -> vm.back()
        }
    }
    FinishScreen(
        state = state,
        serviceRestarted = save.serviceRestarted,
        onBack = { if (state.step == WizardStep.COMMIT_UNKNOWN) cancel() else vm.back() },
        onCreateContracts = { vm.setCreateContracts(it) },
        onFinish = { vm.finish() },
        // The same single receipt check as "Verify save" on 33.
        onVerifySave = { vm.verifyCommit() },
        onInspect = { nav.openEditor() },
        onCancel = cancel,
        checking = save.phase == SaveOutcomePhase.CHECKING,
    )
}

/**
 * 16 Complete setup: what exists and what Finish would add, computed from
 * the original contracts and the own IP (`.2` to `.15` of a supported
 * 10.A.B.0/24 without the own address and existing peers). The default
 * contracts are added only at Finish; the option is disabled with an
 * explanation for unsupported networks. Finish is one atomic save; a failure
 * keeps the draft here, an unknown result goes to 33.
 */
@Composable
fun FinishScreen(
    state: WizardScreenState,
    serviceRestarted: Boolean,
    onBack: () -> Unit,
    onCreateContracts: (Boolean) -> Unit,
    onFinish: () -> Unit,
    onVerifySave: () -> Unit,
    onInspect: () -> Unit,
    onCancel: () -> Unit,
    checking: Boolean = false,
) {
    val unknown = state.step == WizardStep.COMMIT_UNKNOWN
    WizardScaffold(
        title = stringResource(R.string.wizard_finish_title),
        onBack = onBack,
        step = 4,
        footer = {
            if (unknown) {
                ActionButton(stringResource(R.string.save_unknown_verify), onVerifySave, style = ActionStyle.Primary, enabled = !checking)
            } else {
                ActionButton(
                    stringResource(R.string.wizard_finish_save),
                    onFinish,
                    style = ActionStyle.Primary,
                    enabled = state.editable && state.step == WizardStep.CONTRACTS,
                )
            }
            ActionButton(stringResource(R.string.wizard_cancel_setup), onCancel, style = ActionStyle.Ghost, enabled = !state.committing)
        },
    ) {
        Heading(stringResource(R.string.wizard_finish_heading))
        Intro(stringResource(R.string.wizard_finish_intro))
        Card {
            ToggleRow(
                stringResource(R.string.wizard_finish_add_defaults),
                state.createContracts,
                onCreateContracts,
                enabled = state.editable && state.contractsAllowed,
            )
            InfoRow(stringResource(R.string.wizard_finish_existing), state.existingContracts.toString())
            InfoRow(stringResource(R.string.wizard_finish_missing), state.missingContracts.toString(), divider = true)
            InfoRow(stringResource(R.string.wizard_finish_to_add), state.contractsToAdd.toString(), divider = true)
        }
        if (!state.contractsAllowed) {
            Notice(stringResource(R.string.wp09_finish_unsupported), kind = NoticeKind.Info)
        } else if (state.contractsToAdd > 0) {
            Notice(
                stringResource(R.string.wizard_finish_notice_text),
                kind = NoticeKind.Warn,
                title = stringResource(R.string.wizard_finish_notice_title),
            )
        }
        Hint(stringResource(R.string.wp09_finish_kept))
        Card {
            InfoRow(stringResource(R.string.wizard_finish_server), WizardRules.serverUrlHost(state.serverUrl) ?: state.serverUrl)
            InfoRow(stringResource(R.string.wizard_finish_this_phone), state.virtualIp, divider = true)
        }
        Hint(stringResource(R.string.wp09_finish_no_connect))
        WizardProgress(state)
        val error = state.error
        if (unknown) {
            val text = stringResource(R.string.wp09_finish_unknown_text) +
                if (serviceRestarted) "\n" + stringResource(R.string.wp09_finish_restarted) else ""
            Notice(text, kind = NoticeKind.Warn, title = stringResource(R.string.wp09_finish_unknown_title))
            ActionButton(stringResource(R.string.action_inspect_configuration), onInspect)
        } else if (error != null && state.step == WizardStep.CONTRACTS && !state.committing) {
            Notice(
                errorMessageText(error.code) + "\n" + errorActionText(error.code, error.field),
                kind = NoticeKind.Error,
                title = stringResource(R.string.wp09_finish_failed_title),
            )
        }
    }
}
