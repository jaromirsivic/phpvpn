package phpvpn.android.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/**
 * Meaning of a [Tag]. Green is confirmed success, amber waiting or
 * uncertainty, red an error, grey neutral (README, visual rules).
 */
enum class TagKind { Success, Neutral, Warn, Error }

/**
 * Status pill (`.tag`). The text is mandatory: colour is never the only
 * signal, so the tag reads the same to TalkBack and in greyscale.
 */
@Composable
fun Tag(text: String, modifier: Modifier = Modifier, kind: TagKind = TagKind.Success, small: Boolean = false) {
    val colors = PhpvpnTokens.colors
    val (background, foreground) = when (kind) {
        TagKind.Success -> colors.tagSuccessBg to colors.green
        TagKind.Neutral -> colors.tagNeutralBg to colors.muted
        TagKind.Warn -> colors.tagWarnBg to colors.amber
        TagKind.Error -> colors.tagErrorBg to colors.red
    }
    Text(
        text,
        modifier
            .background(background, PhpvpnTokens.shapes.pill)
            .padding(horizontal = 9.dp, vertical = 5.dp),
        color = foreground,
        style = if (small) PhpvpnTokens.text.tagSmall else PhpvpnTokens.text.tag,
    )
}

@ComponentPreviews
@Composable
private fun TagPreview() = PreviewSurface {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Tag(stringResource(R.string.tag_connected))
        Tag(stringResource(R.string.tag_disconnected), kind = TagKind.Neutral)
        Tag(stringResource(R.string.tag_connecting), kind = TagKind.Warn)
        Tag(stringResource(R.string.wizard_error_notice_title), kind = TagKind.Error)
        Tag(stringResource(R.string.tag_enabled), small = true)
    }
}
