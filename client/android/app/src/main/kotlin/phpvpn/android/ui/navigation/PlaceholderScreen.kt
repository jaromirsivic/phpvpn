package phpvpn.android.ui.navigation

import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.BottomNav
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.DialogCard
import phpvpn.android.ui.components.EntryRow
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Hint
import phpvpn.android.ui.components.MainDestination
import phpvpn.android.ui.components.MainTopBar
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.ScreenScaffold
import phpvpn.android.ui.components.TopBar

/** A navigation action offered by a placeholder: label already resolved from resources. */
@Immutable
data class PlaceholderAction(
    val label: String,
    val onClick: () -> Unit,
    val style: ActionStyle = ActionStyle.Secondary,
)

/**
 * WP07 stand-in for a screen of WP08 to WP10. It renders the screen title
 * through the real components, says which work package fills it, and offers
 * the outgoing links of android-screen-map.md so the graph can be walked.
 * The frame (top bar, footer, bottom navigation) is already the final one.
 */
@Composable
fun PlaceholderScreen(
    @StringRes title: Int,
    workPackage: Int,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
    main: MainDestination? = null,
    onSelectMain: (MainDestination) -> Unit = {},
    argument: String? = null,
    links: List<PlaceholderAction> = emptyList(),
    footer: List<PlaceholderAction> = emptyList(),
    extra: (@Composable ColumnScope.() -> Unit)? = null,
) {
    val titleText = stringResource(title)
    ScreenScaffold(
        modifier = modifier,
        topBar = {
            if (main != null) MainTopBar() else TopBar(titleText, onBack = onBack)
        },
        footer = if (footer.isEmpty()) null else {
            { footer.forEach { ActionButton(it.label, it.onClick, style = it.style) } }
        },
        bottomBar = main?.let { { BottomNav(it, onSelectMain) } },
    ) {
        Heading(titleText)
        Notice(
            stringResource(R.string.placeholder_notice_text, workPackage),
            kind = NoticeKind.Info,
            title = stringResource(R.string.placeholder_notice_title),
        )
        if (argument != null) Hint(stringResource(R.string.placeholder_argument, argument))
        extra?.invoke(this)
        if (links.isNotEmpty()) {
            Card {
                links.forEachIndexed { index, link ->
                    EntryRow(link.label, link.onClick, divider = index > 0)
                }
            }
        }
    }
}

/**
 * Placeholder content of a dialog destination (35, 36a). The navigation
 * library already hosts it in a window dialog without platform width.
 */
@Composable
fun PlaceholderDialog(
    title: String,
    paragraphs: List<String>,
    actions: List<PlaceholderAction>,
    eyebrow: String? = null,
) {
    Box(Modifier.padding(20.dp)) {
        DialogCard(title, Modifier.fillMaxWidth(), eyebrow = eyebrow, paragraphs = paragraphs) {
            actions.forEach { ActionButton(it.label, it.onClick, style = it.style) }
        }
    }
}
