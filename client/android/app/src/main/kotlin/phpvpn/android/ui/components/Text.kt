package phpvpn.android.ui.components

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

// Typographic building blocks named after the CSS classes of the design
// (.eyebrow, .appbody h2, .intro, .hint, .section-title, .meta-label).

/** Upper-case label above a heading (`.eyebrow`). */
@Composable
fun Eyebrow(text: String, modifier: Modifier = Modifier, color: Color = PhpvpnTokens.colors.muted) {
    Text(text, modifier, color = color, style = PhpvpnTokens.text.eyebrow)
}

/** Screen heading (`.appbody h2`), exposed as a heading to accessibility services. */
@Composable
fun Heading(text: String, modifier: Modifier = Modifier, textAlign: TextAlign? = null) {
    Text(
        text,
        modifier.semantics { heading() },
        color = PhpvpnTokens.colors.ink,
        style = PhpvpnTokens.text.heading,
        textAlign = textAlign,
    )
}

/** Card heading (`.appbody h3`). */
@Composable
fun Title(text: String, modifier: Modifier = Modifier) {
    Text(text, modifier.semantics { heading() }, color = PhpvpnTokens.colors.ink, style = PhpvpnTokens.text.title)
}

/** Lead paragraph under a heading (`.intro`). */
@Composable
fun Intro(text: String, modifier: Modifier = Modifier, textAlign: TextAlign? = null) {
    Text(text, modifier, color = PhpvpnTokens.colors.muted, style = PhpvpnTokens.text.intro, textAlign = textAlign)
}

/** Small explanatory text (`.hint`). */
@Composable
fun Hint(text: String, modifier: Modifier = Modifier) {
    Text(text, modifier, color = PhpvpnTokens.colors.muted, style = PhpvpnTokens.text.hint)
}

/** Caption above a value inside a card (`.meta-label`). */
@Composable
fun MetaLabel(text: String, modifier: Modifier = Modifier) {
    Text(text, modifier, color = PhpvpnTokens.colors.muted, style = PhpvpnTokens.text.small)
}

/** Row with a bold title and an optional trailing slot (`.section-title`). */
@Composable
fun SectionTitle(text: String, modifier: Modifier = Modifier, trailing: (@Composable () -> Unit)? = null) {
    Row(
        modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(PhpvpnTokens.dimens.cardGap),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(text, Modifier.weight(1f), color = PhpvpnTokens.colors.ink, style = PhpvpnTokens.text.sectionTitle)
        trailing?.invoke()
    }
}

@ComponentPreviews
@Composable
private fun TextPreview() = PreviewSurface {
    Column(verticalArrangement = Arrangement.spacedBy(PhpvpnTokens.dimens.screenGap)) {
        Eyebrow(stringResource(R.string.settings_eyebrow))
        Heading(stringResource(R.string.wizard_url_heading))
        Intro(stringResource(R.string.wizard_url_intro))
        Title(stringResource(R.string.welcome_steps_title))
        Hint(stringResource(R.string.wizard_url_hint))
        SectionTitle(stringResource(R.string.server_heading)) { Hint(stringResource(R.string.tag_online)) }
    }
}
