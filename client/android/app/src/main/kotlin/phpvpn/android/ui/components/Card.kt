package phpvpn.android.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.stringResource
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/** `.card` (white) and `.card.blue` (pale) of the design. */
enum class CardVariant { Default, Blue }

/**
 * White rounded container with a hairline border (`.card`). Children are
 * spaced 12 dp apart (`.card > * + *`); rows add their own divider.
 */
@Composable
fun Card(
    modifier: Modifier = Modifier,
    variant: CardVariant = CardVariant.Default,
    content: @Composable ColumnScope.() -> Unit,
) {
    val colors = PhpvpnTokens.colors
    val shape = PhpvpnTokens.shapes.card
    val (background, border) = when (variant) {
        CardVariant.Default -> colors.surface to colors.line
        CardVariant.Blue -> colors.pale to colors.paleBorder
    }
    Column(
        modifier
            .fillMaxWidth()
            .clip(shape)
            .background(background)
            .border(PhpvpnTokens.dimens.hairline, border, shape)
            .padding(PhpvpnTokens.dimens.cardPadding),
        verticalArrangement = Arrangement.spacedBy(PhpvpnTokens.dimens.cardGap),
        content = content,
    )
}

@ComponentPreviews
@Composable
private fun CardPreview() = PreviewSurface {
    Card {
        MetaLabel(stringResource(R.string.server_card_label))
        InfoRow(stringResource(R.string.row_virtual_ip), "10.80.0.12")
        InfoRow(stringResource(R.string.row_vpn_network), "10.80.0.0/24", divider = true)
    }
    Card(variant = CardVariant.Blue) {
        Title(stringResource(R.string.welcome_steps_title))
    }
}
