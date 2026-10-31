package phpvpn.android.ui.theme

import androidx.compose.material3.Typography
import androidx.compose.runtime.Immutable
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.LineHeightStyle
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import phpvpn.android.R

/**
 * Inter 4.1 (OFL, res/raw/inter_license.txt). Only the static weights the
 * design uses are bundled: 400, 600 (also stands in for the CSS 650 of the
 * titles) and 700 (tags, eyebrows).
 */
val InterFamily = FontFamily(
    Font(R.font.inter_regular, FontWeight.Normal),
    Font(R.font.inter_semibold, FontWeight.SemiBold),
    Font(R.font.inter_bold, FontWeight.Bold),
)

/** JSON, hex keys and HTTP dumps use the system monospace face. */
val MonoFamily: FontFamily = FontFamily.Monospace

// CSS pixels map 1:1 to sp/dp: the reference phone is 412 CSS px wide and
// most current phones are 412 dp wide.
private val Trim = LineHeightStyle(LineHeightStyle.Alignment.Center, LineHeightStyle.Trim.None)

private fun inter(size: Int, weight: FontWeight = FontWeight.Normal, lineHeight: Float = 1.4f, spacing: Float = 0f) =
    TextStyle(
        fontFamily = InterFamily,
        fontWeight = weight,
        fontSize = size.sp,
        lineHeight = (size * lineHeight).sp,
        letterSpacing = spacing.sp,
        lineHeightStyle = Trim,
    )

/**
 * Text styles named after the CSS selectors of styles.css. Colours are not
 * part of the style: components pick them from [PhpvpnColors].
 */
@Immutable
data class PhpvpnTextStyles(
    val appBarTitle: TextStyle = inter(19, FontWeight.SemiBold, 1.25f, -0.5f),
    val appBarEyebrow: TextStyle = inter(11, FontWeight.Bold, 1.3f, 1.6f),
    val eyebrow: TextStyle = inter(11, FontWeight.Bold, 1.4f, 1.6f),
    val heading: TextStyle = inter(29, FontWeight.SemiBold, 1.14f, -1f),
    val heroTitle: TextStyle = inter(32, FontWeight.SemiBold, 1.14f, -1f),
    val heroTitleCompact: TextStyle = inter(30, FontWeight.SemiBold, 1.14f, -1f),
    val heroSubtitle: TextStyle = inter(13, lineHeight = 1.5f),
    val duration: TextStyle = inter(16, spacing = 2f).copy(fontFeatureSettings = "tnum"),
    val title: TextStyle = inter(16, FontWeight.SemiBold, 1.4f),
    val intro: TextStyle = inter(14, lineHeight = 1.6f),
    val body: TextStyle = inter(14, lineHeight = 1.45f),
    val bodyStrong: TextStyle = inter(14, FontWeight.SemiBold, 1.45f),
    val rowValue: TextStyle = inter(13, FontWeight.SemiBold, 1.45f),
    val small: TextStyle = inter(12, lineHeight = 1.5f),
    val hint: TextStyle = inter(12, lineHeight = 1.6f),
    val sectionTitle: TextStyle = inter(13, FontWeight.SemiBold, 1.4f),
    val tag: TextStyle = inter(11, FontWeight.Bold, 1.3f, 0.2f),
    val tagSmall: TextStyle = inter(10, FontWeight.Bold, 1.3f, 0.2f),
    val metricLabel: TextStyle = inter(12, lineHeight = 1.4f),
    val metricValue: TextStyle = inter(24, FontWeight.SemiBold, 1.2f, -0.7f),
    val metricDetail: TextStyle = inter(11, lineHeight = 1.4f),
    val action: TextStyle = inter(14, FontWeight.SemiBold, 1.3f),
    val navLabel: TextStyle = inter(10, FontWeight.SemiBold, 1.3f),
    val fieldLabel: TextStyle = inter(12, lineHeight = 1.4f),
    val fieldInput: TextStyle = inter(16, lineHeight = 1.4f),
    val notice: TextStyle = inter(12, lineHeight = 1.6f),
    val noticeTitle: TextStyle = inter(13, FontWeight.Bold, 1.4f),
    val stepCaption: TextStyle = inter(11, lineHeight = 1.4f),
    val check: TextStyle = inter(13, lineHeight = 1.4f),
    val checkDetail: TextStyle = inter(11, lineHeight = 1.4f),
    val checkBullet: TextStyle = inter(13, FontWeight.Bold, 1.2f),
    val tab: TextStyle = inter(13, lineHeight = 1.4f),
    val tabActive: TextStyle = inter(13, FontWeight.SemiBold, 1.4f),
    val peerTitle: TextStyle = inter(14, FontWeight.SemiBold, 1.4f),
    val peerDetail: TextStyle = inter(11, lineHeight = 1.4f),
    val pager: TextStyle = inter(12, lineHeight = 1.4f),
    val dialogTitle: TextStyle = inter(24, FontWeight.SemiBold, 1.25f, -1f),
    val dialogBody: TextStyle = inter(14, lineHeight = 1.6f),
    val symbol: TextStyle = inter(18, lineHeight = 1.2f),
    val mono: TextStyle = TextStyle(fontFamily = MonoFamily, fontSize = 12.sp, lineHeight = 1.6.em),
    val code: TextStyle = TextStyle(fontFamily = MonoFamily, fontSize = 12.sp, lineHeight = 1.8.em),
    val codeInput: TextStyle = TextStyle(fontFamily = MonoFamily, fontSize = 13.sp, lineHeight = 1.7.em),
)

/** Material 3 typography for the stock components (dialogs, menus, snackbars). */
fun phpvpnTypography(styles: PhpvpnTextStyles) = Typography(
    displaySmall = styles.heroTitle,
    headlineLarge = styles.heroTitle,
    headlineMedium = styles.heading,
    headlineSmall = styles.dialogTitle,
    titleLarge = styles.appBarTitle,
    titleMedium = styles.title,
    titleSmall = styles.sectionTitle,
    bodyLarge = styles.fieldInput,
    bodyMedium = styles.body,
    bodySmall = styles.small,
    labelLarge = styles.action,
    labelMedium = styles.tag,
    labelSmall = styles.navLabel,
)
