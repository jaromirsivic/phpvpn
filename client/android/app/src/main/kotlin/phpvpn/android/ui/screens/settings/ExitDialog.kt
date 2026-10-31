package phpvpn.android.ui.screens.settings

import androidx.activity.compose.LocalActivity
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import phpvpn.android.R
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.DialogCard
import phpvpn.android.ui.components.Title
import phpvpn.android.ui.state.ExitEffect
import phpvpn.android.ui.state.ExitViewModel
import phpvpn.android.ui.theme.PhpvpnTokens

const val EXIT_TAG = "exit-35"

/**
 * Exit dialog (35). Leaving the window is not stopping the VPN: "Close
 * window" only finishes the activity; "Disconnect and close" waits for the
 * confirmed disconnected state and then removes the task. The process is
 * never killed; an unconfirmed Disconnect returns to Connection (07 or 38).
 */
@Composable
fun ExitDialog(viewModel: ExitViewModel, onShowConnection: () -> Unit) {
    val activity = LocalActivity.current
    val disconnecting by viewModel.disconnecting.collectAsStateWithLifecycle()
    LaunchedEffect(viewModel) {
        viewModel.effects.collect { effect ->
            when (effect) {
                ExitEffect.CloseWindow -> activity?.finish()
                ExitEffect.CloseTask -> activity?.finishAndRemoveTask()
                ExitEffect.ShowConnection -> onShowConnection()
            }
        }
    }
    Box(Modifier.padding(20.dp)) {
        DialogCard(
            stringResource(R.string.exit_heading),
            Modifier
                .fillMaxWidth()
                .testTag(EXIT_TAG),
            eyebrow = stringResource(R.string.exit_eyebrow),
            paragraphs = listOf(stringResource(R.string.exit_intro)),
        ) {
            Card {
                Title(stringResource(R.string.exit_keep_title))
                Text(stringResource(R.string.exit_keep_text), color = PhpvpnTokens.colors.muted, style = PhpvpnTokens.text.body)
                ActionButton(stringResource(R.string.exit_close_window), viewModel::closeWindow, enabled = !disconnecting)
            }
            Card {
                Title(stringResource(R.string.exit_stop_title))
                Text(stringResource(R.string.exit_stop_text), color = PhpvpnTokens.colors.muted, style = PhpvpnTokens.text.body)
                ActionButton(
                    stringResource(if (disconnecting) R.string.disconnecting_progress else R.string.exit_disconnect_close),
                    viewModel::disconnectAndClose,
                    style = ActionStyle.Danger,
                    enabled = !disconnecting,
                )
            }
        }
    }
}
