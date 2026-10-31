package phpvpn.android.ui.state

import java.math.BigInteger
import java.time.ZoneOffset
import java.util.Locale
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class UiFormatTest {
    private val czech = Locale.forLanguageTag("cs-CZ")

    @Test
    fun bytesKeepTheTrayRoundingWithTheLocaleSeparator() {
        assertEquals("18.42 MB", UiFormat.bytes(BigInteger.valueOf(18_420_000), Locale.US))
        assertEquals("18,42 MB", UiFormat.bytes(BigInteger.valueOf(18_420_000), czech))
        assertEquals("999 B", UiFormat.bytes(BigInteger.valueOf(999), czech))
        assertEquals("29.01 MB", UiFormat.bytes(BigInteger.valueOf(29_010_000), Locale.US))
        // The 65-bit total of two uint64 counters still formats.
        val max = BigInteger(ULong.MAX_VALUE.toString())
        assertTrue(UiFormat.bytes(max + max, Locale.US).endsWith(" PB"))
    }

    @Test
    fun integersAreExactAndGrouped() {
        assertEquals("14,826", UiFormat.integer(14_826uL, Locale.US))
        assertEquals("18,446,744,073,709,551,615", UiFormat.integer(ULong.MAX_VALUE, Locale.US))
        val czechText = UiFormat.integer(1_284uL, czech)
        assertEquals("1284", czechText.filter { it.isDigit() })
        assertTrue(czechText.length == 5)
    }

    @Test
    fun elapsedNeverGoesNegative() {
        assertEquals(UiFormat.Elapsed(0, 24, 18), UiFormat.elapsed(0, (24 * 60 + 18) * 1000L))
        assertEquals(UiFormat.Elapsed(1, 5, 0), UiFormat.elapsed(0, 3_900_000))
        assertEquals(UiFormat.Elapsed(0, 0, 0), UiFormat.elapsed(10_000, 0))
    }

    @Test
    fun timesShowTheDateOnlyForOtherDays() {
        val noon = 1_767_268_800_000L // 2026-01-01T12:00:00Z
        val today = UiFormat.time(noon, Locale.US, ZoneOffset.UTC, nowUtcMs = noon + 60_000)
        assertTrue(today, today.startsWith("12:00"))
        val earlier = UiFormat.time(noon, Locale.US, ZoneOffset.UTC, nowUtcMs = noon + 2 * 86_400_000L)
        assertTrue(earlier, earlier.contains("26"))
        val seconds = UiFormat.timeWithSeconds(noon + 2_000, Locale.US, ZoneOffset.UTC, nowUtcMs = noon)
        assertTrue(seconds, seconds.contains("12:00:02"))
    }
}
