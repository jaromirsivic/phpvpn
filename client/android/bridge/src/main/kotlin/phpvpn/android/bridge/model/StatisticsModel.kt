package phpvpn.android.bridge.model

import java.math.BigInteger
import java.util.Locale
import phpvpn.android.bridge.ipc.ConnectionState

/** The ten Statistics values in UX07 order (statistics_model.h `statisticsFields`). */
enum class StatisticsField(val counterIndex: Int, val bytes: Boolean, val addCounterIndex: Int? = null) {
    EMPTY_POLLS(1, false),
    POLLS_WITH_PAYLOAD(2, false),
    TOTAL_POLLS(0, false),
    REQUEST_BODY(3, true),
    RESPONSE_BODY(4, true),
    TOTAL_SIZE(3, true, 4),
    PACKETS_SENT(5, false),
    BYTES_SENT(7, true),
    PACKETS_RECEIVED(6, false),
    BYTES_RECEIVED(8, true),
}

/** Freshness of the shown values: 43 waiting, 26 current, 27 last known. */
enum class StatisticsFreshness { WAITING, CURRENT, LAST_KNOWN }

/**
 * Port of `tray::StatisticsModel`: keeps the last valid snapshot and marks it
 * stale when the service status is unavailable. Missing data is never zero.
 */
class StatisticsModel {
    var counters: TrafficCounters = TrafficCounters()
        private set
    var connectionStartedAtUtcMs: Long? = null
        private set
    var valid = false
        private set
    var stale = false
        private set
    var connected = false
        private set

    fun update(snapshot: TrayStatus, unavailable: Boolean) {
        if (snapshot.valid) {
            counters = snapshot.counters
            connected = snapshot.state == ConnectionState.CONNECTED
            connectionStartedAtUtcMs = snapshot.connectionStartedAtUtcMs
            valid = true
        }
        stale = unavailable || !snapshot.valid
    }

    val freshness: StatisticsFreshness
        get() = when {
            !valid -> StatisticsFreshness.WAITING
            stale -> StatisticsFreshness.LAST_KNOWN
            else -> StatisticsFreshness.CURRENT
        }

    /** Exact value of [field]; Total size is the 65-bit sum of both body counters. */
    fun rawValue(field: StatisticsField): BigInteger? {
        if (!valid) return null
        val first = BigInteger(counters[field.counterIndex].toString())
        return field.addCounterIndex?.let { first + BigInteger(counters[it].toString()) } ?: first
    }

    /** English rendering of the tray ("-" without data); the UI may localise numbers itself. */
    fun value(field: StatisticsField): String {
        val raw = rawValue(field) ?: return "-"
        return if (field.bytes) formatBytes(raw) else englishInteger(raw)
    }

    companion object {
        private val units = listOf(
            1_000L to "kB", 1_000_000L to "MB", 1_000_000_000L to "GB", 1_000_000_000_000L to "TB",
            1_000_000_000_000_000L to "PB",
        )

        fun englishInteger(value: BigInteger): String = String.format(Locale.ROOT, "%,d", value)

        /** Port of `formatStatisticsBytes`: two decimals, half up, at most PB. */
        fun formatBytes(value: BigInteger): String {
            if (value < BigInteger.valueOf(1000)) return "$value B"
            var unit = units.size - 1
            if (value.bitLength() <= 64) {
                unit = 0
                while (unit + 1 < units.size && value >= BigInteger.valueOf(units[unit + 1].first)) unit++
            }
            var rounded: BigInteger
            while (true) {
                val divisor = BigInteger.valueOf(units[unit].first)
                val (quotient, remainder) = value.divideAndRemainder(divisor).let { it[0] to it[1] }
                val hundredth = divisor.divide(BigInteger.valueOf(100))
                val fractional = remainder.divide(hundredth)
                val rest = remainder.mod(hundredth)
                rounded = quotient * BigInteger.valueOf(100) + fractional +
                    (if (rest * BigInteger.TWO >= hundredth) BigInteger.ONE else BigInteger.ZERO)
                if (rounded < BigInteger.valueOf(100_000) || unit + 1 == units.size) break
                unit++
            }
            val whole = rounded.divide(BigInteger.valueOf(100))
            val fraction = rounded.mod(BigInteger.valueOf(100)).toInt()
            return "$whole.${if (fraction < 10) "0" else ""}$fraction ${units[unit].second}"
        }
    }
}
