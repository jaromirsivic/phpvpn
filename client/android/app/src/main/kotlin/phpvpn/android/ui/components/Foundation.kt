package phpvpn.android.ui.components

import android.view.View
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.interaction.InteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.tooling.preview.Preview
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTheme
import phpvpn.android.ui.theme.PhpvpnTokens

/**
 * Visible keyboard focus (`:focus-visible { outline: 3px }` of the design).
 * Returns a modifier to chain on the focusable element.
 */
@Composable
fun focusOutline(interactionSource: InteractionSource, shape: Shape): Modifier {
    val focused by interactionSource.collectIsFocusedAsState()
    return if (focused) {
        Modifier.border(PhpvpnTokens.dimens.focusWidth, PhpvpnTokens.colors.focus, shape)
    } else {
        Modifier
    }
}

/** Separator between rows (`.row + .row { border-top }`), with the 12 dp gap below it. */
@Composable
fun RowDivider(modifier: Modifier = Modifier, color: Color = PhpvpnTokens.colors.divider) {
    Box(
        modifier
            .fillMaxWidth()
            .height(PhpvpnTokens.dimens.hairline)
            .background(color),
    )
}

/** Wraps [content] in a column that starts with a [RowDivider] when [divider] is set. */
@Composable
internal fun WithDivider(divider: Boolean, content: @Composable () -> Unit) {
    if (!divider) {
        content()
        return
    }
    Column(verticalArrangement = Arrangement.spacedBy(PhpvpnTokens.dimens.cardGap)) {
        RowDivider()
        content()
    }
}

/**
 * Keeps the autofill framework away from secrets: while any composable using
 * this is on screen, the hosting view and its virtual children are not
 * offered to autofill services. Compose itself marks password fields as
 * autofill candidates, so the exclusion has to happen at the view level.
 */
@Composable
internal fun ExcludeFromAutofill() {
    val view = LocalView.current
    DisposableEffect(view) {
        val count = (view.getTag(R.id.autofill_exclusion_count) as? Int ?: 0) + 1
        view.setTag(R.id.autofill_exclusion_count, count)
        if (count == 1) {
            view.setTag(R.id.autofill_previous_mode, view.importantForAutofill)
            view.importantForAutofill = View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS
        }
        onDispose {
            val left = (view.getTag(R.id.autofill_exclusion_count) as? Int ?: 1) - 1
            view.setTag(R.id.autofill_exclusion_count, left)
            if (left == 0) {
                view.importantForAutofill =
                    view.getTag(R.id.autofill_previous_mode) as? Int ?: View.IMPORTANT_FOR_AUTOFILL_AUTO
            }
        }
    }
}

/** Every component preview at the reference width and at 320 dp with 200 % text. */
@Preview(name = "412 dp", widthDp = 412)
@Preview(name = "320 dp, 200 % text", widthDp = 320, fontScale = 2f)
annotation class ComponentPreviews

/** Theme and page background for previews and the debug catalog. */
@Composable
fun PreviewSurface(content: @Composable ColumnScope.() -> Unit) {
    PhpvpnTheme {
        Column(
            Modifier
                .background(PhpvpnTokens.colors.bg)
                .padding(PhpvpnTokens.dimens.screenPaddingH),
            verticalArrangement = Arrangement.spacedBy(PhpvpnTokens.dimens.cardGap),
            content = content,
        )
    }
}
