package phpvpn.android.ui.screens.traffic

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import java.math.BigInteger
import phpvpn.android.R
import phpvpn.android.bridge.model.StatisticsField
import phpvpn.android.ui.components.BottomNav
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.Eyebrow
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Hint
import phpvpn.android.ui.components.InfoRow
import phpvpn.android.ui.components.Intro
import phpvpn.android.ui.components.MainDestination
import phpvpn.android.ui.components.MainTopBar
import phpvpn.android.ui.components.Metric
import phpvpn.android.ui.components.MetricGrid
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.ScreenScaffold
import phpvpn.android.ui.components.Title
import phpvpn.android.ui.screens.common.currentLocale
import phpvpn.android.ui.screens.common.durationText
import phpvpn.android.ui.screens.common.rememberNowUtcMs
import phpvpn.android.ui.state.TrafficUiState
import phpvpn.android.ui.state.TrafficViewModel
import phpvpn.android.ui.state.UiFormat

object TrafficTags {
    const val CURRENT = "traffic-26"
    const val STALE = "traffic-27"
    const val WAITING = "traffic-43"
}

/**
 * Traffic destination (26, 27, 43): the ten read-only statistics of the
 * status, grouped by layer. No speed, ping, history or chart (26); stale
 * values are marked as such (27); before the first snapshot only dashes (43).
 */
@Composable
fun TrafficScreen(viewModel: TrafficViewModel, onSelectMain: (MainDestination) -> Unit) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    ScreenScaffold(
        topBar = { MainTopBar() },
        bottomBar = { BottomNav(MainDestination.Traffic, onSelectMain) },
    ) {
        when (val current = state) {
            TrafficUiState.Waiting -> Waiting()
            is TrafficUiState.Values -> Values(current)
        }
    }
}

@Composable
private fun Waiting() {
    Eyebrow(stringResource(R.string.traffic_empty_eyebrow), Modifier.testTag(TrafficTags.WAITING))
    Heading(stringResource(R.string.traffic_heading))
    Notice(stringResource(R.string.traffic_empty_notice_text), title = stringResource(R.string.traffic_empty_notice_title))
    val pair = stringResource(R.string.value_missing_pair)
    val single = stringResource(R.string.value_missing)
    Card {
        InfoRow(stringResource(R.string.traffic_empty_bytes), pair)
        InfoRow(stringResource(R.string.traffic_empty_packets), pair, divider = true)
        InfoRow(stringResource(R.string.traffic_empty_http), pair, divider = true)
        InfoRow(stringResource(R.string.traffic_empty_http_total), single, divider = true)
        InfoRow(stringResource(R.string.traffic_waiting_polls), pair, divider = true)
        InfoRow(stringResource(R.string.traffic_total_polls), single, divider = true)
    }
}

@Composable
private fun Values(state: TrafficUiState.Values) {
    val locale = currentLocale()
    if (state.stale) {
        Eyebrow(stringResource(R.string.traffic_stale_eyebrow), Modifier.testTag(TrafficTags.STALE))
        Heading(stringResource(R.string.traffic_heading))
        Notice(
            stringResource(R.string.traffic_stale_notice_text),
            kind = NoticeKind.Warn,
            title = stringResource(R.string.traffic_stale_notice_title),
        )
    } else {
        Eyebrow(stringResource(R.string.traffic_eyebrow), Modifier.testTag(TrafficTags.CURRENT))
        Heading(stringResource(R.string.traffic_heading))
        val started = state.startedAtUtcMs
        Intro(
            when {
                state.connected && started != null ->
                    stringResource(R.string.traffic_intro, durationText(started, rememberNowUtcMs()))
                state.connected -> stringResource(R.string.traffic_intro_current)
                else -> stringResource(R.string.traffic_intro_not_connected)
            },
        )
    }
    MetricGrid(
        listOf(
            Metric(
                stringResource(R.string.metric_received),
                UiFormat.bytes(state[StatisticsField.BYTES_RECEIVED], locale),
                packets(state[StatisticsField.PACKETS_RECEIVED]),
            ),
            Metric(
                stringResource(R.string.metric_sent),
                UiFormat.bytes(state[StatisticsField.BYTES_SENT], locale),
                packets(state[StatisticsField.PACKETS_SENT]),
            ),
        ),
    )
    Card {
        Title(stringResource(R.string.traffic_http_bodies))
        Rows(
            state,
            R.string.traffic_requests to StatisticsField.REQUEST_BODY,
            R.string.traffic_responses to StatisticsField.RESPONSE_BODY,
            R.string.traffic_total to StatisticsField.TOTAL_SIZE,
        )
    }
    Card {
        if (!state.stale) Title(stringResource(R.string.traffic_polls))
        Rows(
            state,
            R.string.traffic_empty_polls to StatisticsField.EMPTY_POLLS,
            R.string.traffic_payload_polls to StatisticsField.POLLS_WITH_PAYLOAD,
            (if (state.stale) R.string.traffic_total_polls else R.string.traffic_total) to StatisticsField.TOTAL_POLLS,
        )
    }
    Hint(stringResource(R.string.traffic_hint))
}

@Composable
private fun Rows(state: TrafficUiState.Values, vararg rows: Pair<Int, StatisticsField>) {
    val locale = currentLocale()
    rows.forEachIndexed { index, (label, field) ->
        val value = state[field]
        InfoRow(
            stringResource(label),
            if (field.bytes) UiFormat.bytes(value, locale) else UiFormat.integer(value, locale),
            divider = index > 0,
        )
    }
}

@Composable
private fun packets(count: BigInteger): String {
    val quantity = if (count > BigInteger.valueOf(Int.MAX_VALUE.toLong())) Int.MAX_VALUE else count.toInt()
    return pluralStringResource(R.plurals.metric_ipv4_packets, quantity, UiFormat.integer(count, currentLocale()))
}
