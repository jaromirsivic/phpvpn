package phpvpn.android.ui.screens.wizard

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import androidx.navigation.NavController
import phpvpn.android.R
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.Eyebrow
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Intro
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.navigation.WizardExpired
import phpvpn.android.ui.navigation.WizardUrl
import phpvpn.android.ui.navigation.openEditor

/**
 * 47 after the expired session was dropped together with its graph: the old
 * verification results are gone and cannot authorise a save. Reopen starts
 * a new session (a new wizard-open), Inspect opens the editor.
 */
@Composable
fun WizardExpiredRoute(nav: NavController) {
    ExpiredScreen(
        onBack = { nav.popBackStack() },
        onReopen = {
            nav.navigate(WizardUrl) {
                popUpTo<WizardExpired> { inclusive = true }
                launchSingleTop = true
            }
        },
        onInspect = { nav.openEditor() },
    )
}

/** 47 Wizard session expired. */
@Composable
fun ExpiredScreen(onBack: () -> Unit, onReopen: () -> Unit, onInspect: () -> Unit) {
    WizardScaffold(
        title = stringResource(R.string.session_expired_title),
        onBack = onBack,
        footer = {
            ActionButton(stringResource(R.string.session_expired_reopen), onReopen, style = ActionStyle.Primary)
            ActionButton(stringResource(R.string.action_inspect_configuration), onInspect, style = ActionStyle.Ghost)
        },
    ) {
        Eyebrow(stringResource(R.string.wizard_eyebrow))
        Heading(stringResource(R.string.session_expired_heading))
        Notice(
            stringResource(R.string.session_expired_notice_text),
            kind = NoticeKind.Warn,
            title = stringResource(R.string.session_expired_notice_title),
        )
        Intro(stringResource(R.string.session_expired_intro))
    }
}
