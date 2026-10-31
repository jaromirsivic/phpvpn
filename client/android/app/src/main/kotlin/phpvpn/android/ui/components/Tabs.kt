package phpvpn.android.ui.components

import androidx.compose.foundation.LocalIndication
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/**
 * Underlined text tabs (`.tabs`): Who is online / Server data and Request /
 * Response. Each tab is a 48 dp selectable target with the Tab role.
 */
@Composable
fun Tabs(titles: List<String>, selectedIndex: Int, onSelect: (Int) -> Unit, modifier: Modifier = Modifier) {
    val colors = PhpvpnTokens.colors
    Box(modifier.fillMaxWidth()) {
        // Full-width baseline under all tabs (border-bottom of .tabs).
        Box(
            Modifier
                .align(Alignment.BottomStart)
                .fillMaxWidth()
                .height(PhpvpnTokens.dimens.hairline)
                .background(colors.line),
        )
        Row(Modifier.selectableGroup(), horizontalArrangement = Arrangement.spacedBy(20.dp)) {
            titles.forEachIndexed { index, title ->
                val selected = index == selectedIndex
                val interaction = remember { MutableInteractionSource() }
                Column(
                    Modifier
                        .width(IntrinsicSize.Max)
                        .then(focusOutline(interaction, PhpvpnTokens.shapes.field))
                        .selectable(
                            selected = selected,
                            interactionSource = interaction,
                            indication = LocalIndication.current,
                            role = Role.Tab,
                            onClick = { onSelect(index) },
                        )
                        .heightIn(min = PhpvpnTokens.dimens.touchTarget),
                    verticalArrangement = Arrangement.Bottom,
                ) {
                    Text(
                        title,
                        Modifier.padding(top = 8.dp, bottom = 13.dp),
                        color = if (selected) colors.blue else colors.muted,
                        style = if (selected) PhpvpnTokens.text.tabActive else PhpvpnTokens.text.tab,
                    )
                    val indicator = Modifier
                        .fillMaxWidth()
                        .height(3.dp)
                    if (selected) Box(indicator.background(colors.blue)) else Spacer(indicator)
                }
            }
        }
    }
}

@ComponentPreviews
@Composable
private fun TabsPreview() = PreviewSurface {
    Tabs(listOf(stringResource(R.string.server_tab_online), stringResource(R.string.server_tab_raw)), 0, {})
    Tabs(listOf(stringResource(R.string.http_tab_request), stringResource(R.string.http_tab_response)), 1, {})
}
