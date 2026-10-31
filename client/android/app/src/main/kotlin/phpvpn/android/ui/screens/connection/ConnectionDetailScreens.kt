package phpvpn.android.ui.screens.connection

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import phpvpn.android.R
import phpvpn.android.bridge.errors.ConnectionErrorCode
import phpvpn.android.errors.ErrorStrings
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.CheckState
import phpvpn.android.ui.components.ConnectionChecks
import phpvpn.android.ui.components.Eyebrow
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Intro
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.ScreenScaffold
import phpvpn.android.ui.components.TopBar
import phpvpn.android.ui.screens.common.ServerCard
import phpvpn.android.ui.state.ConnectionViewModel
import phpvpn.android.ui.state.VpnConsent
import phpvpn.android.ui.state.rememberVpnConsentLauncher

/**
 * 37 (`connection/permission`): Android denied the VPN. Not a server error;
 * the configuration is untouched. Retry asks the system again; only a granted
 * consent starts Connect and returns to the status ([onDone]).
 */
@Composable
fun PermissionDeniedScreen(viewModel: ConnectionViewModel, consent: VpnConsent, onDone: () -> Unit) {
    val launcher = rememberVpnConsentLauncher { granted ->
        // A second denial stays here; it must not bounce through Connection.
        if (granted) {
            viewModel.onConsentResult(true)
            onDone()
        }
    }
    ScreenScaffold(
        topBar = { TopBar(stringResource(R.string.permission_denied_title), onBack = onDone) },
        footer = {
            ActionButton(
                stringResource(R.string.permission_denied_retry),
                {
                    if (!consent.request(launcher)) {
                        viewModel.onConsentResult(true)
                        onDone()
                    }
                },
                style = ActionStyle.Primary,
            )
            ActionButton(stringResource(R.string.permission_denied_stay), onDone, style = ActionStyle.Ghost)
        },
    ) {
        PermissionContent(Modifier.testTag(ConnectionTags.PERMISSION))
    }
}

/**
 * 48 (`connection/checks`): read-only view of the four checks of the last
 * connection. Opening it never sends Connect; it only reads the status the
 * Connection destination already polls.
 */
@Composable
fun ConnectionChecksScreen(viewModel: ConnectionViewModel, onBack: () -> Unit) {
    val model by viewModel.screen.collectAsStateWithLifecycle()
    val checks = model.lastChecks ?: NO_CHECKS
    val heading = when {
        checks.all { it == CheckState.Done } -> R.string.checks_heading
        checks.any { it == CheckState.Fail } -> R.string.checks_heading_failed
        else -> R.string.checks_heading_incomplete
    }
    ScreenScaffold(topBar = { TopBar(stringResource(R.string.checks_title), onBack = onBack) }) {
        Eyebrow(stringResource(R.string.traffic_eyebrow), Modifier.testTag(CHECKS_TAG))
        Heading(stringResource(heading))
        Intro(stringResource(R.string.checks_intro))
        if (model.checksStale) {
            val error = ConnectionErrorCode.STATUS_UNAVAILABLE
            Notice(
                stringResource(ErrorStrings.resource(error.message)),
                kind = NoticeKind.Warn,
                title = stringResource(ErrorStrings.resource(error.title)),
            )
        }
        ConnectionChecks(checks)
        ServerCard(model.config)
    }
}

const val CHECKS_TAG = "connection-48"
