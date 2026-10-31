package phpvpn.android.ui.components

import androidx.compose.foundation.Image
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/**
 * App bar (`.appbar`) under the white status bar area. With [onBack] it shows
 * a 48 dp Back button and the screen title (nested screens); without it the
 * brand logo. [platformEyebrow] adds the "ANDROID" label of the main
 * destinations, whose title is the brand name.
 */
@Composable
fun TopBar(
    title: String,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
    platformEyebrow: Boolean = false,
) {
    val colors = PhpvpnTokens.colors
    Surface(modifier.fillMaxWidth(), color = colors.surface) {
        Row(
            Modifier
                .windowInsetsPadding(WindowInsets.statusBars)
                .padding(start = 20.dp, end = 20.dp, top = 10.dp, bottom = 16.dp)
                .heightIn(min = 38.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (onBack != null) {
                val interaction = remember { MutableInteractionSource() }
                // margin-left: -10px of .back keeps the glyph aligned with the content edge.
                Surface(
                    onClick = onBack,
                    modifier = Modifier
                        .offset(x = (-12).dp)
                        .then(focusOutline(interaction, CircleShape)),
                    shape = CircleShape,
                    color = colors.surface,
                    interactionSource = interaction,
                ) {
                    Box(Modifier.size(PhpvpnTokens.dimens.touchTarget), contentAlignment = Alignment.Center) {
                        Icon(painterResource(R.drawable.ic_back), stringResource(R.string.common_back), Modifier.size(24.dp), tint = colors.ink)
                    }
                }
            } else {
                Image(
                    painterResource(R.drawable.ic_state_brand),
                    contentDescription = null,
                    modifier = Modifier
                        .size(28.dp)
                        .clip(PhpvpnTokens.shapes.brand),
                )
            }
            // A flow row moves "ANDROID" under the title when both do not fit
            // (320 dp, large fonts) instead of breaking the brand name.
            FlowRow(
                Modifier
                    .weight(1f)
                    .offset(x = if (onBack != null) (-12).dp else 0.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                itemVerticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    title,
                    Modifier.semantics { heading() },
                    color = colors.ink,
                    style = PhpvpnTokens.text.appBarTitle,
                )
                if (platformEyebrow) {
                    Text(stringResource(R.string.common_platform_eyebrow), color = colors.muted, style = PhpvpnTokens.text.appBarEyebrow)
                }
            }
        }
    }
}

/** Top bar of the four main destinations: logo, "PHPVPN", "ANDROID". */
@Composable
fun MainTopBar(modifier: Modifier = Modifier) {
    TopBar(stringResource(R.string.brand_name), modifier, platformEyebrow = true)
}

@ComponentPreviews
@Composable
private fun TopBarPreview() = PreviewSurface {
    MainTopBar()
    TopBar(stringResource(R.string.wizard_url_title), onBack = {})
    TopBar(stringResource(R.string.welcome_title))
    TopBar(stringResource(R.string.attention_title), onBack = {})
}

