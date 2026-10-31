package phpvpn.android.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/**
 * Decision card (`.dialog`): eyebrow, title, paragraphs and stacked actions.
 * Used for 24, 32, 35, 36a and 40, both inside a window dialog and inline.
 */
@Composable
fun DialogCard(
    title: String,
    modifier: Modifier = Modifier,
    eyebrow: String? = null,
    paragraphs: List<String> = emptyList(),
    actions: @Composable ColumnScope.() -> Unit,
) {
    val colors = PhpvpnTokens.colors
    val shape = PhpvpnTokens.shapes.dialog
    Column(
        modifier
            .fillMaxWidth()
            .shadow(12.dp, shape, ambientColor = colors.shadow, spotColor = colors.shadow)
            .background(colors.surface, shape)
            .border(PhpvpnTokens.dimens.hairline, colors.line, shape)
            .padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(18.dp),
    ) {
        if (eyebrow != null) Eyebrow(eyebrow)
        Text(title, Modifier.semantics { heading() }, color = colors.ink, style = PhpvpnTokens.text.dialogTitle)
        paragraphs.forEach { Text(it, color = colors.muted, style = PhpvpnTokens.text.dialogBody) }
        Column(verticalArrangement = Arrangement.spacedBy(18.dp), content = actions)
    }
}

@ComponentPreviews
@Composable
private fun DialogCardPreview() = PreviewSurface {
    DialogCard(
        stringResource(R.string.discard_heading),
        eyebrow = stringResource(R.string.discard_eyebrow),
        paragraphs = listOf(stringResource(R.string.discard_text)),
    ) {
        ActionButton(stringResource(R.string.action_keep_editing), {}, style = ActionStyle.Primary)
        ActionButton(stringResource(R.string.action_discard_changes), {}, style = ActionStyle.Danger)
    }
}
