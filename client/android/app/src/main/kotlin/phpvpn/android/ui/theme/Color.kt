package phpvpn.android.ui.theme

import androidx.compose.runtime.Immutable
import androidx.compose.ui.graphics.Color

/**
 * Colour tokens of the design (assets/android-en/styles.css). This file and
 * the rest of the `theme` package are the only places with colour literals:
 * components read [PhpvpnTokens.colors], so a dark theme (R7) is just another
 * instance of this class.
 */
@Immutable
data class PhpvpnColors(
    // :root custom properties
    val ink: Color,
    val muted: Color,
    val line: Color,
    val blue: Color,
    val pale: Color,
    val surface: Color,
    val bg: Color,
    val green: Color,
    val red: Color,
    val amber: Color,
    // Text and fills on top of the brand blue (.action.primary color:white)
    val onBlue: Color,
    // a:focus-visible outline
    val focus: Color,
    // .card.blue border
    val paleBorder: Color,
    // .row + .row, .check + .check separators
    val divider: Color,
    // .chevron
    val chevron: Color,
    // .tag variants (background; text uses green / muted / amber / red)
    val tagSuccessBg: Color,
    val tagNeutralBg: Color,
    val tagWarnBg: Color,
    val tagErrorBg: Color,
    // .notice variants: background, text, border
    val noticeWarnBg: Color,
    val noticeWarnFg: Color,
    val noticeWarnBorder: Color,
    val noticeErrorBg: Color,
    val noticeErrorFg: Color,
    val noticeErrorBorder: Color,
    val noticeInfoBg: Color,
    val noticeInfoFg: Color,
    val noticeInfoBorder: Color,
    val noticeSuccessBg: Color,
    val noticeSuccessFg: Color,
    val noticeSuccessBorder: Color,
    // .action.danger
    val dangerBg: Color,
    val dangerBorder: Color,
    // .field input border and the .field.invalid background
    val fieldBorder: Color,
    val fieldInvalidBg: Color,
    // .switch track and border when off
    val switchTrackOff: Color,
    val switchBorderOff: Color,
    // .stepper i (not done)
    val stepperTrack: Color,
    // .check .bullet variants (done uses green text, pending blue, fail red)
    val checkDoneBg: Color,
    val checkPendingBg: Color,
    val checkOffBg: Color,
    val checkOffFg: Color,
    val checkFailBg: Color,
    // .code
    val codeBg: Color,
    val codeFg: Color,
    // .peer-symbol
    val symbolBg: Color,
    // .orb ring and its outline
    val orbRing: Color,
    val orbOutline: Color,
    // .dialog and .device shadow tint
    val shadow: Color,
    // .toast
    val toastBg: Color,
)

/** The only colour set of v1 (R7: light theme only). */
val LightPhpvpnColors = PhpvpnColors(
    ink = Color(0xFF142742),
    muted = Color(0xFF627086),
    line = Color(0xFFDCE3ED),
    blue = Color(0xFF1459DA),
    pale = Color(0xFFEAF1FF),
    surface = Color(0xFFFFFFFF),
    bg = Color(0xFFF5F7FB),
    green = Color(0xFF087858),
    red = Color(0xFFAA3040),
    amber = Color(0xFF8A5B0D),
    onBlue = Color(0xFFFFFFFF),
    focus = Color(0xFF2676EC),
    paleBorder = Color(0xFFD2E0FF),
    divider = Color(0xFFEDF0F5),
    chevron = Color(0xFF8392A8),
    tagSuccessBg = Color(0xFFE6F4ED),
    tagNeutralBg = Color(0xFFEDF0F6),
    tagWarnBg = Color(0xFFFFF0CE),
    tagErrorBg = Color(0xFFFFE8EB),
    noticeWarnBg = Color(0xFFFFF3DB),
    noticeWarnFg = Color(0xFF704C14),
    noticeWarnBorder = Color(0xFFF0DDB9),
    noticeErrorBg = Color(0xFFFFF0F1),
    noticeErrorFg = Color(0xFF912C3B),
    noticeErrorBorder = Color(0xFFF0CBD1),
    noticeInfoBg = Color(0xFFEAF1FF),
    noticeInfoFg = Color(0xFF214C92),
    noticeInfoBorder = Color(0xFFD5E2FF),
    noticeSuccessBg = Color(0xFFE8F5EF),
    noticeSuccessFg = Color(0xFF17654E),
    noticeSuccessBorder = Color(0xFFC7E5D9),
    dangerBg = Color(0xFFFFF6F7),
    dangerBorder = Color(0xFFF0CBD0),
    fieldBorder = Color(0xFFB9C6D9),
    fieldInvalidBg = Color(0xFFFFFAFB),
    switchTrackOff = Color(0xFFDCE3ED),
    switchBorderOff = Color(0xFF9DAEC3),
    stepperTrack = Color(0xFFDCE4F1),
    checkDoneBg = Color(0xFFE5F3ED),
    checkPendingBg = Color(0xFFE9EFFB),
    checkOffBg = Color(0xFFEDF0F5),
    checkOffFg = Color(0xFF8692A3),
    checkFailBg = Color(0xFFFFE6E9),
    codeBg = Color(0xFFEDF1F8),
    codeFg = Color(0xFF294565),
    symbolBg = Color(0xFFEDF2FA),
    orbRing = Color(0xFFE4EEFB),
    orbOutline = Color(0xFFD2E0F2),
    shadow = Color(0xFF1F355A),
    toastBg = Color(0xFF203652),
)
