package phpvpn.android.ui.components

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/** `.action` variants: plain, `.primary`, `.danger` and `.ghost`. */
enum class ActionStyle { Secondary, Primary, Danger, Ghost }

/**
 * Full-width button of the design (`.action`): at least 50 dp high, 14 dp
 * corners, centred 14 sp semibold text that wraps instead of clipping.
 */
@Composable
fun ActionButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    style: ActionStyle = ActionStyle.Secondary,
    enabled: Boolean = true,
) {
    val colors = PhpvpnTokens.colors
    val (container, content, border) = when (style) {
        ActionStyle.Secondary -> Triple(colors.surface, colors.ink, colors.line)
        ActionStyle.Primary -> Triple(colors.blue, colors.onBlue, colors.blue)
        ActionStyle.Danger -> Triple(colors.dangerBg, colors.red, colors.dangerBorder)
        ActionStyle.Ghost -> Triple(Color.Transparent, colors.blue, Color.Transparent)
    }
    val shape = PhpvpnTokens.shapes.action
    val interaction = remember { MutableInteractionSource() }
    Surface(
        onClick = onClick,
        modifier = modifier
            .fillMaxWidth()
            .alpha(if (enabled) 1f else 0.5f)
            .then(focusOutline(interaction, shape)),
        enabled = enabled,
        shape = shape,
        color = container,
        contentColor = content,
        border = BorderStroke(PhpvpnTokens.dimens.hairline, border),
        interactionSource = interaction,
    ) {
        Box(
            Modifier
                .heightIn(min = PhpvpnTokens.dimens.actionMinHeight)
                .padding(horizontal = 16.dp, vertical = 12.dp),
            contentAlignment = Alignment.Center,
        ) {
            Text(text, color = content, style = PhpvpnTokens.text.action, textAlign = TextAlign.Center)
        }
    }
}

/** Side-by-side buttons sharing the width equally (`.actions`). */
@Composable
fun ActionRow(modifier: Modifier = Modifier, content: @Composable RowScope.() -> Unit) {
    Row(modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp), content = content)
}

/**
 * Bar of primary actions pinned under the scrolling content
 * (`.footer-actions`). [ScreenScaffold] places it outside the scroll area;
 * it pads for the navigation bar when it is the last element on screen.
 */
@Composable
fun FooterActions(
    modifier: Modifier = Modifier,
    padNavigationBar: Boolean = true,
    content: @Composable ColumnScope.() -> Unit,
) {
    Surface(modifier.fillMaxWidth(), color = PhpvpnTokens.colors.surface) {
        Column {
            RowDivider(color = PhpvpnTokens.colors.line)
            Column(
                Modifier
                    .then(if (padNavigationBar) Modifier.windowInsetsPadding(WindowInsets.navigationBars) else Modifier)
                    .padding(start = 20.dp, end = 20.dp, top = 14.dp, bottom = 16.dp),
                verticalArrangement = Arrangement.spacedBy(9.dp),
                content = content,
            )
        }
    }
}

@ComponentPreviews
@Composable
private fun ActionsPreview() = PreviewSurface {
    ActionButton(stringResource(R.string.wizard_verify_continue), {}, style = ActionStyle.Primary)
    ActionButton(stringResource(R.string.action_disconnect_vpn), {})
    ActionButton(stringResource(R.string.action_discard_changes), {}, style = ActionStyle.Danger)
    ActionButton(stringResource(R.string.wizard_cancel_setup), {}, style = ActionStyle.Ghost)
    ActionButton(stringResource(R.string.disconnecting_progress), {}, enabled = false)
    ActionRow {
        ActionButton(stringResource(R.string.password_key_default), {}, Modifier.weight(1f))
        ActionButton(stringResource(R.string.password_key_random), {}, Modifier.weight(1f))
    }
    FooterActions(padNavigationBar = false) {
        ActionButton(pluralStringResource(R.plurals.contracts_save_changes, 2, 2), {}, style = ActionStyle.Primary)
        ActionButton(stringResource(R.string.action_discard_changes), {}, style = ActionStyle.Ghost)
    }
}
