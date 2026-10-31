package phpvpn.android.ui.components

import androidx.annotation.DrawableRes
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/** The four state icons of assets/phpvpn*.svg. */
enum class StateIcon(@param:DrawableRes val drawable: Int) {
    Brand(R.drawable.ic_state_brand),
    Connected(R.drawable.ic_state_connected),
    Connecting(R.drawable.ic_state_connecting),
    Disconnected(R.drawable.ic_state_disconnected),
}

/**
 * The round "orb" with a state icon (`.orb`, `.hero.compact .orb`). The icon
 * is decorative unless [contentDescription] is given: the hero tag and title
 * already state the connection state in words.
 */
@Composable
fun Orb(icon: StateIcon, modifier: Modifier = Modifier, compact: Boolean = false, contentDescription: String? = null) {
    val colors = PhpvpnTokens.colors
    val outer: Dp = if (compact) 76.dp else 112.dp
    val ring: Dp = if (compact) 7.dp else 10.dp
    val iconSize: Dp = if (compact) 40.dp else 58.dp
    // box-shadow 0 0 0 1px draws a 1 dp outline outside the ring.
    Box(
        modifier
            .size(outer + 2.dp)
            .background(colors.orbOutline, CircleShape)
            .padding(1.dp)
            .background(colors.orbRing, CircleShape)
            .padding(ring)
            .background(colors.surface, CircleShape),
        contentAlignment = Alignment.Center,
    ) {
        Image(painterResource(icon.drawable), contentDescription, Modifier.size(iconSize))
    }
}

/**
 * Centred status block (`.hero`): orb, optional [badge] (usually a [Tag] or
 * an [Eyebrow]), title and subtitle.
 */
@Composable
fun Hero(
    icon: StateIcon,
    title: String,
    modifier: Modifier = Modifier,
    subtitle: String? = null,
    compact: Boolean = false,
    iconDescription: String? = null,
    badge: (@Composable () -> Unit)? = null,
) {
    val text = PhpvpnTokens.text
    Column(
        modifier
            .fillMaxWidth()
            .padding(top = if (compact) 0.dp else 12.dp, bottom = if (compact) 0.dp else 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Orb(
            icon,
            Modifier.padding(top = if (compact) 0.dp else 8.dp),
            compact = compact,
            contentDescription = iconDescription,
        )
        Spacer(Modifier.height(if (compact) 14.dp else 24.dp))
        if (badge != null) {
            badge()
            Spacer(Modifier.height(12.dp))
        }
        Text(
            title,
            Modifier.semantics { heading() },
            color = PhpvpnTokens.colors.ink,
            style = if (compact) text.heroTitleCompact else text.heroTitle,
            textAlign = TextAlign.Center,
        )
        if (subtitle != null) {
            Spacer(Modifier.height(9.dp))
            Text(subtitle, color = PhpvpnTokens.colors.muted, style = text.heroSubtitle, textAlign = TextAlign.Center)
        }
    }
}

@ComponentPreviews
@Composable
private fun HeroPreview() = PreviewSurface {
    Hero(
        StateIcon.Connected,
        stringResource(R.string.connected_heading),
        subtitle = stringResource(R.string.connected_subtitle, stringResource(R.string.duration_min_sec, 24, 18)),
        compact = true,
        badge = { Tag(stringResource(R.string.tag_connected)) },
    )
    Hero(
        StateIcon.Connecting,
        stringResource(R.string.connecting_heading),
        subtitle = stringResource(R.string.connecting_subtitle),
        badge = { Tag(stringResource(R.string.tag_connecting), kind = TagKind.Warn) },
    )
    Hero(
        StateIcon.Disconnected,
        stringResource(R.string.disconnected_heading),
        subtitle = stringResource(R.string.disconnected_subtitle),
        badge = { Tag(stringResource(R.string.tag_disconnected), kind = TagKind.Neutral) },
    )
    Hero(
        StateIcon.Brand,
        stringResource(R.string.welcome_heading),
        subtitle = stringResource(R.string.welcome_intro),
        badge = { Eyebrow(stringResource(R.string.welcome_eyebrow)) },
    )
}
