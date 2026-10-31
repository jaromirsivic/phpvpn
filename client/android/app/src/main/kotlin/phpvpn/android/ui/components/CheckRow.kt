package phpvpn.android.ui.components

import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/** State of one connection check (`.check .bullet` variants). */
enum class CheckState(@param:StringRes val text: Int, @param:StringRes val symbol: Int) {
    Pending(R.string.check_state_pending, R.string.symbol_check_pending),
    Done(R.string.check_state_done, R.string.symbol_check_done),
    Fail(R.string.check_state_fail, R.string.symbol_check_fail),
    Off(R.string.check_state_off, R.string.symbol_check_off),
}

/**
 * One check (`.check`): a decorative bullet and the label with its state in
 * words, read by TalkBack as one item.
 */
@Composable
fun CheckRow(label: String, state: CheckState, modifier: Modifier = Modifier, divider: Boolean = false) {
    val colors = PhpvpnTokens.colors
    val (background, foreground) = when (state) {
        CheckState.Done -> colors.checkDoneBg to colors.green
        CheckState.Pending -> colors.checkPendingBg to colors.blue
        CheckState.Off -> colors.checkOffBg to colors.checkOffFg
        CheckState.Fail -> colors.checkFailBg to colors.red
    }
    // The glyph is decorative and sits in a fixed 26 dp circle, so it does
    // not follow font scaling (the label and state text do).
    val glyphSize = with(LocalDensity.current) { 13.dp.toSp() }
    Column {
        if (divider) RowDivider()
        Row(
            modifier
                .fillMaxWidth()
                .padding(vertical = 12.dp)
                .semantics(mergeDescendants = true) { },
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                Modifier
                    .size(26.dp)
                    .background(background, CircleShape)
                    .clearAndSetSemantics { },
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    stringResource(state.symbol),
                    color = foreground,
                    style = PhpvpnTokens.text.checkBullet.copy(fontSize = glyphSize, lineHeight = glyphSize),
                )
            }
            Column(Modifier.weight(1f)) {
                Text(label, color = colors.ink, style = PhpvpnTokens.text.check)
                Text(
                    stringResource(state.text),
                    Modifier.padding(top = 4.dp),
                    color = colors.muted,
                    style = PhpvpnTokens.text.checkDetail,
                )
            }
        }
    }
}

/** The four desktop checks in their fixed order (server URL, secret, access key, contracts). */
@Composable
fun ConnectionChecks(states: List<CheckState>, modifier: Modifier = Modifier) {
    require(states.size == CheckLabels.size) { "expected ${CheckLabels.size} check states" }
    Card(modifier) {
        // The card spaces children 12 dp apart; checks bring their own padding.
        Column {
            CheckLabels.forEachIndexed { index, label ->
                CheckRow(stringResource(label), states[index], divider = index > 0)
            }
        }
    }
}

private val CheckLabels = listOf(
    R.string.check_server_url,
    R.string.check_server_secret,
    R.string.check_access_key,
    R.string.check_contracts,
)

@ComponentPreviews
@Composable
private fun CheckRowPreview() = PreviewSurface {
    ConnectionChecks(listOf(CheckState.Done, CheckState.Done, CheckState.Pending, CheckState.Off))
    ConnectionChecks(listOf(CheckState.Fail, CheckState.Off, CheckState.Off, CheckState.Off))
}
