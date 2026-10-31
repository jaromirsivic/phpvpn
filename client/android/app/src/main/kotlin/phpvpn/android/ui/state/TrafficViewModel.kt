package phpvpn.android.ui.state

import java.math.BigInteger
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import phpvpn.android.bridge.model.StatisticsField
import phpvpn.android.bridge.model.StatisticsFreshness
import phpvpn.android.bridge.repository.StatisticsRepository
import phpvpn.android.bridge.repository.StatisticsState

/** What the Traffic destination shows (26, 27, 43). */
sealed interface TrafficUiState {
    /** 43: no valid snapshot yet. Missing data is a dash, never zero. */
    data object Waiting : TrafficUiState

    /**
     * 26 ([stale] false) or 27 ([stale] true: the last values after a failed
     * read, clearly marked and never animated as live). All ten fields.
     */
    data class Values(
        val stale: Boolean,
        val connected: Boolean,
        val startedAtUtcMs: Long?,
        val values: Map<StatisticsField, BigInteger>,
    ) : TrafficUiState {
        operator fun get(field: StatisticsField): BigInteger = values.getValue(field)
    }

    companion object {
        fun from(state: StatisticsState): TrafficUiState {
            val values = state.values
            return if (state.freshness == StatisticsFreshness.WAITING || values == null) {
                Waiting
            } else {
                Values(state.freshness == StatisticsFreshness.LAST_KNOWN, state.connected, state.connectionStartedAtUtcMs, values)
            }
        }
    }
}

/** Traffic destination: the statistics port of WP06, polled only while collected. */
class TrafficViewModel(statistics: StatisticsRepository) : ViewModel() {
    val state: StateFlow<TrafficUiState> = statistics.statistics
        .map(TrafficUiState::from)
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(), TrafficUiState.from(statistics.statistics.value))
}
