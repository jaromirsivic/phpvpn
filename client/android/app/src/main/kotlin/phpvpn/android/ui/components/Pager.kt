package phpvpn.android.ui.components

import androidx.annotation.DrawableRes
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.sizeIn
import androidx.compose.material3.Icon
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/**
 * Page switcher of long lists (`.pager`): range text and 48 dp previous/next
 * buttons. A missing callback hides that button, as on the first and last
 * page of the design.
 */
@Composable
fun Pager(
    rangeText: String,
    modifier: Modifier = Modifier,
    previousLabel: String? = null,
    onPrevious: (() -> Unit)? = null,
    nextLabel: String? = null,
    onNext: (() -> Unit)? = null,
) {
    Row(
        modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (onPrevious != null) PagerButton(R.drawable.ic_chevron_left, previousLabel, onPrevious)
        Text(rangeText, Modifier.weight(1f, fill = false), color = PhpvpnTokens.colors.muted, style = PhpvpnTokens.text.pager)
        if (onNext != null) PagerButton(R.drawable.ic_chevron_right, nextLabel, onNext)
    }
}

@Composable
private fun PagerButton(@DrawableRes icon: Int, label: String?, onClick: () -> Unit) {
    val shape = PhpvpnTokens.shapes.symbol
    val interaction = remember { MutableInteractionSource() }
    Surface(
        onClick = onClick,
        modifier = focusOutline(interaction, shape),
        shape = shape,
        color = PhpvpnTokens.colors.surface,
        border = BorderStroke(PhpvpnTokens.dimens.hairline, PhpvpnTokens.colors.line),
        interactionSource = interaction,
    ) {
        Box(Modifier.sizeIn(minWidth = PhpvpnTokens.dimens.touchTarget, minHeight = PhpvpnTokens.dimens.touchTarget), contentAlignment = Alignment.Center) {
            Icon(painterResource(icon), label, Modifier.size(20.dp), tint = PhpvpnTokens.colors.ink)
        }
    }
}

@ComponentPreviews
@Composable
private fun PagerPreview() = PreviewSurface {
    Pager(
        pluralStringResource(R.plurals.contracts_page_range, 252, 1, 4, 252),
        nextLabel = stringResource(R.string.contracts_page_next),
        onNext = {},
    )
    Pager(
        pluralStringResource(R.plurals.contracts_page_range, 252, 5, 8, 252),
        previousLabel = stringResource(R.string.contracts_page_previous),
        onPrevious = {},
        nextLabel = stringResource(R.string.contracts_page_next),
        onNext = {},
    )
}
