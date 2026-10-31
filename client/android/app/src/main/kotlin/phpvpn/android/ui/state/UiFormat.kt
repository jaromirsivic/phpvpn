package phpvpn.android.ui.state

import java.math.BigInteger
import java.text.DecimalFormatSymbols
import java.text.NumberFormat
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.Locale
import phpvpn.android.bridge.model.StatisticsModel

/**
 * Locale formatting of numbers, sizes and times. Only digits, separators and
 * SI unit symbols are produced here; every word comes from resources.
 */
object UiFormat {
    /** Exact integer with the locale's grouping (14,826 / 14 826). */
    fun integer(value: BigInteger, locale: Locale): String = NumberFormat.getIntegerInstance(locale).format(value)

    fun integer(value: ULong, locale: Locale): String = integer(BigInteger(value.toString()), locale)

    /**
     * The tray rendering of a byte count (statistics_model.h: two decimals,
     * half up, B to PB) with the locale's decimal separator.
     */
    fun bytes(value: BigInteger, locale: Locale): String {
        val english = StatisticsModel.formatBytes(value)
        val separator = DecimalFormatSymbols.getInstance(locale).decimalSeparator
        return if (separator == '.') english else english.replace('.', separator)
    }

    fun bytes(value: ULong, locale: Locale): String = bytes(BigInteger(value.toString()), locale)

    /** Whole elapsed minutes and seconds (or hours and minutes), never negative. */
    data class Elapsed(val hours: Long, val minutes: Long, val seconds: Long)

    fun elapsed(startUtcMs: Long, nowUtcMs: Long): Elapsed {
        val total = ((nowUtcMs - startUtcMs) / 1000).coerceAtLeast(0)
        return Elapsed(total / 3600, (total % 3600) / 60, total % 60)
    }

    /** Short local time (9:41), with the date when it is not today. */
    fun time(utcMs: Long, locale: Locale, zone: ZoneId = ZoneId.systemDefault(), nowUtcMs: Long = System.currentTimeMillis()): String =
        format(utcMs, FormatStyle.SHORT, locale, zone, nowUtcMs)

    /** Local time with seconds (9:41:02), with the date when it is not today. */
    fun timeWithSeconds(utcMs: Long, locale: Locale, zone: ZoneId = ZoneId.systemDefault(), nowUtcMs: Long = System.currentTimeMillis()): String =
        format(utcMs, FormatStyle.MEDIUM, locale, zone, nowUtcMs)

    private fun format(utcMs: Long, style: FormatStyle, locale: Locale, zone: ZoneId, nowUtcMs: Long): String {
        val moment = Instant.ofEpochMilli(utcMs).atZone(zone)
        val today = LocalDate.ofInstant(Instant.ofEpochMilli(nowUtcMs), zone)
        val formatter = if (moment.toLocalDate() == today) {
            DateTimeFormatter.ofLocalizedTime(style)
        } else {
            DateTimeFormatter.ofLocalizedDateTime(FormatStyle.SHORT, style)
        }
        return moment.format(formatter.withLocale(locale))
    }
}
