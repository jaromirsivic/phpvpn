package phpvpn.android.ui.theme

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/** Corner radii of styles.css. */
@Immutable
data class PhpvpnShapes(
    val card: RoundedCornerShape = RoundedCornerShape(18.dp),
    val metric: RoundedCornerShape = RoundedCornerShape(16.dp),
    val action: RoundedCornerShape = RoundedCornerShape(14.dp),
    val notice: RoundedCornerShape = RoundedCornerShape(14.dp),
    val field: RoundedCornerShape = RoundedCornerShape(12.dp),
    val code: RoundedCornerShape = RoundedCornerShape(12.dp),
    val symbol: RoundedCornerShape = RoundedCornerShape(12.dp),
    val dialog: RoundedCornerShape = RoundedCornerShape(24.dp),
    val pill: RoundedCornerShape = RoundedCornerShape(percent = 50),
    val brand: RoundedCornerShape = RoundedCornerShape(6.dp),
)

/** Spacing and sizes of styles.css that more than one component uses. */
@Immutable
data class PhpvpnDimens(
    val screenPaddingH: Dp = 20.dp,
    val screenPaddingV: Dp = 24.dp,
    val screenGap: Dp = 18.dp,
    val cardPadding: Dp = 18.dp,
    val cardGap: Dp = 12.dp,
    val actionMinHeight: Dp = 50.dp,
    val touchTarget: Dp = 48.dp,
    val focusWidth: Dp = 3.dp,
    val hairline: Dp = 1.dp,
)

/** Everything the components need from the theme, in one CompositionLocal. */
@Immutable
data class PhpvpnTokenSet(
    val colors: PhpvpnColors,
    val text: PhpvpnTextStyles = PhpvpnTextStyles(),
    val shapes: PhpvpnShapes = PhpvpnShapes(),
    val dimens: PhpvpnDimens = PhpvpnDimens(),
)

val LocalPhpvpnTokens = staticCompositionLocalOf { PhpvpnTokenSet(LightPhpvpnColors) }

/** Shorthand used by the components: `PhpvpnTokens.colors.ink`. */
object PhpvpnTokens {
    val colors: PhpvpnColors
        @Composable @ReadOnlyComposable get() = LocalPhpvpnTokens.current.colors
    val text: PhpvpnTextStyles
        @Composable @ReadOnlyComposable get() = LocalPhpvpnTokens.current.text
    val shapes: PhpvpnShapes
        @Composable @ReadOnlyComposable get() = LocalPhpvpnTokens.current.shapes
    val dimens: PhpvpnDimens
        @Composable @ReadOnlyComposable get() = LocalPhpvpnTokens.current.dimens
}

/** Maps the design tokens onto the Material 3 roles used by stock components. */
fun phpvpnColorScheme(c: PhpvpnColors) = lightColorScheme(
    primary = c.blue,
    onPrimary = c.onBlue,
    primaryContainer = c.pale,
    onPrimaryContainer = c.noticeInfoFg,
    inversePrimary = c.paleBorder,
    secondary = c.muted,
    onSecondary = c.onBlue,
    secondaryContainer = c.pale,
    onSecondaryContainer = c.blue,
    tertiary = c.green,
    onTertiary = c.onBlue,
    tertiaryContainer = c.tagSuccessBg,
    onTertiaryContainer = c.green,
    background = c.bg,
    onBackground = c.ink,
    surface = c.surface,
    onSurface = c.ink,
    surfaceVariant = c.codeBg,
    onSurfaceVariant = c.muted,
    surfaceTint = c.blue,
    inverseSurface = c.toastBg,
    inverseOnSurface = c.onBlue,
    error = c.red,
    onError = c.onBlue,
    errorContainer = c.noticeErrorBg,
    onErrorContainer = c.noticeErrorFg,
    outline = c.fieldBorder,
    outlineVariant = c.line,
    scrim = c.ink,
    surfaceBright = c.surface,
    surfaceDim = c.bg,
    surfaceContainerLowest = c.surface,
    surfaceContainerLow = c.surface,
    surfaceContainer = c.surface,
    surfaceContainerHigh = c.surface,
    surfaceContainerHighest = c.surface,
)

/**
 * The application theme. v1 has only the light token set (R7); a dark theme
 * adds a second [PhpvpnColors] instance and picks it here.
 */
@Composable
fun PhpvpnTheme(content: @Composable () -> Unit) {
    val tokens = PhpvpnTokenSet(LightPhpvpnColors)
    CompositionLocalProvider(LocalPhpvpnTokens provides tokens) {
        MaterialTheme(
            colorScheme = phpvpnColorScheme(tokens.colors),
            typography = phpvpnTypography(tokens.text),
            shapes = Shapes(
                small = tokens.shapes.field,
                medium = tokens.shapes.action,
                large = tokens.shapes.card,
                extraLarge = tokens.shapes.dialog,
            ),
            content = content,
        )
    }
}
