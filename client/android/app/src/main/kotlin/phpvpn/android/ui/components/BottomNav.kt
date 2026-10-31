package phpvpn.android.ui.components

import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import androidx.compose.foundation.LocalIndication
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material3.Icon
import androidx.compose.material3.Surface
import androidx.compose.foundation.text.BasicText
import androidx.compose.foundation.text.TextAutoSize
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.paneTitle
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/** The four main destinations of the design (README, Navigation). */
enum class MainDestination(@param:StringRes val label: Int, @param:DrawableRes val icon: Int) {
    Connection(R.string.nav_connection, R.drawable.ic_nav_connection),
    Traffic(R.string.nav_traffic, R.drawable.ic_nav_traffic),
    Server(R.string.nav_server, R.drawable.ic_nav_server),
    Settings(R.string.nav_settings, R.drawable.ic_nav_settings),
}

/**
 * Bottom navigation (`.nav`): four equal items, icon in a pill that turns
 * pale blue when active, label under it. Labels wrap at large font scales.
 */
@Composable
fun BottomNav(selected: MainDestination?, onSelect: (MainDestination) -> Unit, modifier: Modifier = Modifier) {
    val colors = PhpvpnTokens.colors
    val navLabel = stringResource(R.string.nav_main)
    Surface(modifier.fillMaxWidth(), color = colors.surface) {
        Column(Modifier.semantics { paneTitle = navLabel }) {
            RowDivider(color = colors.line)
            Row(
                Modifier
                    .windowInsetsPadding(WindowInsets.navigationBars)
                    .padding(start = 7.dp, end = 7.dp, top = 10.dp, bottom = 8.dp)
                    .selectableGroup(),
            ) {
                MainDestination.entries.forEach { destination ->
                    val active = destination == selected
                    val tint = if (active) colors.blue else colors.muted
                    val interaction = remember { MutableInteractionSource() }
                    Column(
                        Modifier
                            .weight(1f)
                            .then(focusOutline(interaction, PhpvpnTokens.shapes.field))
                            .selectable(
                                selected = active,
                                interactionSource = interaction,
                                indication = LocalIndication.current,
                                role = Role.Tab,
                                onClick = { onSelect(destination) },
                            )
                            .heightIn(min = 56.dp)
                            .padding(vertical = 4.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                    ) {
                        Box(
                            Modifier
                                .size(width = 52.dp, height = 27.dp)
                                .background(if (active) colors.pale else Color.Transparent, PhpvpnTokens.shapes.pill),
                            contentAlignment = Alignment.Center,
                        ) {
                            Icon(painterResource(destination.icon), null, Modifier.size(20.dp), tint = tint)
                        }
                        // One line that shrinks with large font scales instead of
                        // breaking inside a word, but never below the design's
                        // unscaled 10 px, where it may wrap between words.
                        val style = PhpvpnTokens.text.navLabel
                        val minSize = with(LocalDensity.current) { 10.dp.toSp() }
                        BasicText(
                            stringResource(destination.label),
                            Modifier.padding(top = 5.dp, start = 2.dp, end = 2.dp),
                            style = style.copy(color = tint, textAlign = TextAlign.Center),
                            maxLines = if (minSize < style.fontSize) 1 else Int.MAX_VALUE,
                            autoSize = if (minSize < style.fontSize) {
                                TextAutoSize.StepBased(minFontSize = minSize, maxFontSize = style.fontSize)
                            } else {
                                null
                            },
                        )
                    }
                }
            }
        }
    }
}

@ComponentPreviews
@Composable
private fun BottomNavPreview() = PreviewSurface {
    MainDestination.entries.forEach { BottomNav(it, {}) }
}
