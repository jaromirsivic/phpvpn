package phpvpn.android.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.LocalIndication
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.sizeIn
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/**
 * Label and value on one line (`.row`): the label may carry a small detail,
 * the value is right aligned and wraps instead of overflowing.
 */
@Composable
fun InfoRow(
    label: String,
    value: String,
    modifier: Modifier = Modifier,
    detail: String? = null,
    divider: Boolean = false,
) {
    InfoRow(label, modifier, detail, divider) {
        Text(
            value,
            color = PhpvpnTokens.colors.ink,
            style = PhpvpnTokens.text.rowValue,
            textAlign = TextAlign.End,
        )
    }
}

/** [InfoRow] with any trailing content, e.g. a [Tag]. */
@Composable
fun InfoRow(
    label: String,
    modifier: Modifier = Modifier,
    detail: String? = null,
    divider: Boolean = false,
    value: @Composable () -> Unit,
) {
    WithDivider(divider) {
        Row(
            modifier
                .fillMaxWidth()
                .heightIn(min = 44.dp),
            horizontalArrangement = Arrangement.spacedBy(15.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(label, color = PhpvpnTokens.colors.ink, style = PhpvpnTokens.text.body)
                if (detail != null) {
                    Text(detail, Modifier.padding(top = 4.dp), color = PhpvpnTokens.colors.muted, style = PhpvpnTokens.text.small)
                }
            }
            // Label and value share the width; the value hugs the end edge (.row .value).
            Box(Modifier.weight(1f), contentAlignment = Alignment.CenterEnd) { value() }
        }
    }
}

/** Square symbol box of the link rows and peers (`.peer-symbol`). */
@Composable
fun SymbolBox(modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    Box(
        modifier
            .size(42.dp)
            .background(PhpvpnTokens.colors.symbolBg, PhpvpnTokens.shapes.symbol)
            .clearAndSetSemantics { },
        contentAlignment = Alignment.Center,
    ) { content() }
}

/** A text glyph in a [SymbolBox]; kept at a fixed size because it is decorative. */
@Composable
fun SymbolText(text: String) {
    val fixed = with(LocalDensity.current) { 18.dp.toSp() }
    SymbolBox {
        Text(text, color = PhpvpnTokens.colors.blue, style = PhpvpnTokens.text.symbol.copy(fontSize = fixed, lineHeight = fixed))
    }
}

/** A vector icon in a [SymbolBox]. */
@Composable
fun SymbolIcon(painter: Painter) {
    SymbolBox { Icon(painter, null, Modifier.size(22.dp), tint = PhpvpnTokens.colors.blue) }
}

/**
 * Navigation row with a chevron (`.row.link`, the design's `entry`). The
 * whole row is one 52 dp touch target and one accessibility node.
 */
@Composable
fun EntryRow(
    label: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    detail: String? = null,
    divider: Boolean = false,
    symbol: (@Composable () -> Unit)? = null,
) {
    val interaction = remember { MutableInteractionSource() }
    val shape = PhpvpnTokens.shapes.field
    WithDivider(divider) {
        Row(
            modifier
                .fillMaxWidth()
                .then(focusOutline(interaction, shape))
                .clickable(interactionSource = interaction, indication = LocalIndication.current, role = Role.Button, onClick = onClick)
                .heightIn(min = 52.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            symbol?.invoke()
            Column(Modifier.weight(1f)) {
                Text(label, color = PhpvpnTokens.colors.ink, style = PhpvpnTokens.text.bodyStrong)
                if (detail != null) {
                    Text(detail, Modifier.padding(top = 4.dp), color = PhpvpnTokens.colors.muted, style = PhpvpnTokens.text.small)
                }
            }
            Icon(
                painterResource(R.drawable.ic_chevron_right),
                contentDescription = null,
                modifier = Modifier.sizeIn(minWidth = 24.dp, minHeight = 24.dp),
                tint = PhpvpnTokens.colors.chevron,
            )
        }
    }
}

@ComponentPreviews
@Composable
private fun InfoRowPreview() = PreviewSurface {
    Card {
        InfoRow(stringResource(R.string.welcome_step_1), stringResource(R.string.welcome_step_1_value))
        InfoRow(stringResource(R.string.gate_current_status), divider = true) {
            Tag(stringResource(R.string.tag_connected_short))
        }
        InfoRow(stringResource(R.string.server_card_label), "https://vpn.example.com/phpvpn.php", divider = true)
    }
    Card {
        EntryRow(
            stringResource(R.string.settings_wizard),
            onClick = {},
            detail = stringResource(R.string.settings_wizard_detail),
            symbol = { SymbolText(stringResource(R.string.symbol_wizard)) },
        )
        EntryRow(
            stringResource(R.string.entry_communication_contracts),
            onClick = {},
            detail = stringResource(R.string.settings_contracts_detail),
            divider = true,
            symbol = { SymbolIcon(painterResource(R.drawable.ic_swap)) },
        )
    }
    EntryRow(stringResource(R.string.settings_exit), onClick = {}, detail = stringResource(R.string.settings_exit_detail))
}
