package phpvpn.android.bridge.repository

import java.math.BigInteger
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import phpvpn.android.bridge.model.StatisticsField
import phpvpn.android.bridge.model.StatisticsFreshness
import phpvpn.android.bridge.model.StatisticsModel
import phpvpn.android.bridge.model.TrayStatus

/**
 * Traffic destination (screens 26, 27, 43): the ten values of one valid
 * snapshot. [values] is null before the first snapshot (43, never zero); a
 * failed read keeps the previous values and marks them [StatisticsFreshness.LAST_KNOWN] (27).
 */
data class StatisticsState(
    val freshness: StatisticsFreshness,
    val values: Map<StatisticsField, BigInteger>?,
    val connected: Boolean,
    val connectionStartedAtUtcMs: Long?,
)

/** Derived from [ConnectionRepository.status]; it polls only while collected. */
class StatisticsRepository(connection: ConnectionRepository, scope: CoroutineScope) {
    private val model = StatisticsModel()

    val statistics: StateFlow<StatisticsState> = connection.status.map { status ->
        synchronized(model) {
            when (status) {
                ConnectionStatus.Loading -> Unit
                is ConnectionStatus.Available -> model.update(status.status, unavailable = false)
                is ConnectionStatus.Unavailable -> model.update(TrayStatus(), unavailable = true)
            }
            snapshot()
        }
    }.stateIn(scope, SharingStarted.WhileSubscribed(), synchronized(model) { snapshot() })

    private fun snapshot() = StatisticsState(
        freshness = model.freshness,
        values = if (model.valid) StatisticsField.entries.associateWith { model.rawValue(it)!! } else null,
        connected = model.connected,
        connectionStartedAtUtcMs = model.connectionStartedAtUtcMs,
    )
}
