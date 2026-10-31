package phpvpn.android.ui.components

import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.exclude
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.ui.unit.dp
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.windowInsetsBottomHeight
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/**
 * Screen frame of the design (`.device`): [topBar], the vertically scrolling
 * body (`.appbody`, 24/20 dp padding, 18 dp gaps), then the [footer]
 * actions and the [bottomBar], both pinned outside the scroll area. Only the
 * body scrolls, and only vertically: wide content must wrap.
 */
@Composable
fun ScreenScaffold(
    topBar: @Composable () -> Unit,
    modifier: Modifier = Modifier,
    footer: (@Composable ColumnScope.() -> Unit)? = null,
    bottomBar: (@Composable () -> Unit)? = null,
    scrollState: ScrollState = rememberScrollState(),
    content: @Composable ColumnScope.() -> Unit,
) {
    val dimens = PhpvpnTokens.dimens
    Column(
        modifier
            .fillMaxSize()
            .background(PhpvpnTokens.colors.bg)
            .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Horizontal))
            // The keyboard pushes the footer up; the navigation bar part of
            // the IME inset is already padded by the footer or bottom bar.
            .windowInsetsPadding(WindowInsets.ime.exclude(WindowInsets.navigationBars)),
    ) {
        topBar()
        Column(
            Modifier
                .weight(1f)
                .fillMaxWidth()
                .verticalScroll(scrollState)
                .padding(horizontal = dimens.screenPaddingH, vertical = dimens.screenPaddingV),
            verticalArrangement = Arrangement.spacedBy(dimens.screenGap),
            content = content,
        )
        if (footer != null) {
            FooterActions(padNavigationBar = bottomBar == null, content = footer)
        }
        when {
            bottomBar != null -> bottomBar()
            footer == null -> Spacer(Modifier.windowInsetsBottomHeight(WindowInsets.navigationBars))
        }
    }
}

@ComponentPreviews
@Composable
private fun ScreenScaffoldPreview() = PreviewSurface {
    Column(Modifier.fillMaxWidth()) {
        ScreenScaffold(
            topBar = { TopBar(stringResource(R.string.contracts_title), onBack = {}) },
            modifier = Modifier.fillMaxWidth().height(640.dp),
            footer = {
                ActionButton(pluralStringResource(R.plurals.contracts_save_changes, 2, 2), {}, style = ActionStyle.Primary)
                ActionButton(stringResource(R.string.action_discard_changes), {}, style = ActionStyle.Ghost)
            },
        ) {
            Eyebrow(stringResource(R.string.contracts_eyebrow, "10.80.0.12"))
            Heading(stringResource(R.string.contracts_heading))
            Intro(stringResource(R.string.contracts_intro))
        }
    }
}
