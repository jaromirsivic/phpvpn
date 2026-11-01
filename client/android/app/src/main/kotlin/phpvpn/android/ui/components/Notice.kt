package phpvpn.android.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/**
 * Kind of a [Notice]. The design's unnamed `.notice` is [Warn] (amber, waiting
 * or uncertainty); `info`, `error` and `success` are its variants.
 */
enum class NoticeKind { Info, Warn, Error, Success }

/**
 * Callout with an optional bold title (`.notice`) and an optional [action]
 * under the text (one ActionButton, 02 "Server address not set"). Errors are
 * a polite live region so TalkBack announces them when they appear.
 */
@Composable
fun Notice(
    text: String?,
    modifier: Modifier = Modifier,
    kind: NoticeKind = NoticeKind.Info,
    title: String? = null,
    action: (@Composable ColumnScope.() -> Unit)? = null,
) {
    val colors = PhpvpnTokens.colors
    val (background, foreground, border) = when (kind) {
        NoticeKind.Info -> Triple(colors.noticeInfoBg, colors.noticeInfoFg, colors.noticeInfoBorder)
        NoticeKind.Warn -> Triple(colors.noticeWarnBg, colors.noticeWarnFg, colors.noticeWarnBorder)
        NoticeKind.Error -> Triple(colors.noticeErrorBg, colors.noticeErrorFg, colors.noticeErrorBorder)
        NoticeKind.Success -> Triple(colors.noticeSuccessBg, colors.noticeSuccessFg, colors.noticeSuccessBorder)
    }
    val shape = PhpvpnTokens.shapes.notice
    Column(
        modifier
            .fillMaxWidth()
            .background(background, shape)
            .border(PhpvpnTokens.dimens.hairline, border, shape)
            .padding(horizontal = 16.dp, vertical = 14.dp)
            .semantics(mergeDescendants = true) {
                if (kind == NoticeKind.Error) liveRegion = LiveRegionMode.Polite
            },
    ) {
        if (title != null) {
            Text(title, color = foreground, style = PhpvpnTokens.text.noticeTitle)
        }
        if (text != null) {
            Text(
                text,
                Modifier.padding(top = if (title != null) 4.dp else 0.dp),
                color = foreground,
                style = PhpvpnTokens.text.notice,
            )
        }
        if (action != null) {
            Column(Modifier.padding(top = 12.dp), content = action)
        }
    }
}

@ComponentPreviews
@Composable
private fun NoticePreview() = PreviewSurface {
    Notice(
        stringResource(R.string.wizard_url_notice_text),
        title = stringResource(R.string.wizard_url_notice_title),
    )
    Notice(
        stringResource(R.string.reconnecting_notice_text),
        kind = NoticeKind.Warn,
        title = stringResource(R.string.reconnecting_notice_title),
    )
    Notice(
        stringResource(R.string.connection_error_notice_text),
        kind = NoticeKind.Error,
        title = stringResource(R.string.connection_error_notice_title),
    )
    Notice("vpn.example.com", kind = NoticeKind.Success, title = stringResource(R.string.wizard_secret_url_verified))
}
