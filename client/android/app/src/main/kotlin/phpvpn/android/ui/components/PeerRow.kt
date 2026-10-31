package phpvpn.android.ui.components

import androidx.compose.foundation.LocalIndication
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/**
 * Contract target in the contracts list (`.peer` inside `.row.link`):
 * symbol, address, state detail, tag and chevron. One touch target.
 */
@Composable
fun PeerRow(
    address: String,
    detail: String,
    tagText: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    tagKind: TagKind = TagKind.Success,
    divider: Boolean = false,
) {
    val interaction = remember { MutableInteractionSource() }
    WithDivider(divider) {
        BoxWithConstraints(modifier.fillMaxWidth()) {
            // With little room per character (320 dp, large fonts) the tag
            // moves under the detail so the address is not squeezed.
            val stacked = maxWidth / LocalDensity.current.fontScale < 280.dp
            Row(
                Modifier
                    .fillMaxWidth()
                    .then(focusOutline(interaction, PhpvpnTokens.shapes.field))
                    .clickable(interactionSource = interaction, indication = LocalIndication.current, role = Role.Button, onClick = onClick)
                    .heightIn(min = 52.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                // The symbol is decorative; it gives way to the address when space is short.
                if (!stacked) SymbolIcon(painterResource(R.drawable.ic_swap))
                Column(Modifier.weight(1f)) {
                    Text(address, color = PhpvpnTokens.colors.ink, style = PhpvpnTokens.text.peerTitle)
                    Text(detail, Modifier.padding(top = 5.dp), color = PhpvpnTokens.colors.muted, style = PhpvpnTokens.text.peerDetail)
                    if (stacked) Tag(tagText, Modifier.padding(top = 6.dp), kind = tagKind, small = true)
                }
                if (!stacked) Tag(tagText, kind = tagKind, small = true)
                Icon(painterResource(R.drawable.ic_chevron_right), null, Modifier.size(24.dp), tint = PhpvpnTokens.colors.chevron)
            }
        }
    }
}

@ComponentPreviews
@Composable
private fun PeerRowPreview() = PreviewSurface {
    Card {
        PeerRow("10.80.0.2", stringResource(R.string.contracts_peer_unsaved), stringResource(R.string.tag_enabled), {})
        PeerRow(
            "10.80.0.3",
            stringResource(R.string.contracts_peer_key_configured),
            stringResource(R.string.tag_enabled),
            {},
            divider = true,
        )
        PeerRow(
            "10.80.0.4",
            stringResource(R.string.contracts_peer_disabled),
            stringResource(R.string.tag_disabled),
            {},
            tagKind = TagKind.Neutral,
            divider = true,
        )
    }
}
