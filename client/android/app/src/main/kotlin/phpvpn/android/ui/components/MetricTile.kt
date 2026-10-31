package phpvpn.android.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/** One statistic (`.metric`): label, large value and a detail line. */
@Composable
fun MetricTile(label: String, value: String, modifier: Modifier = Modifier, detail: String? = null) {
    val shape = PhpvpnTokens.shapes.metric
    Column(
        modifier
            .background(PhpvpnTokens.colors.surface, shape)
            .border(PhpvpnTokens.dimens.hairline, PhpvpnTokens.colors.line, shape)
            .padding(17.dp)
            .semantics(mergeDescendants = true) { },
    ) {
        Text(label, color = PhpvpnTokens.colors.muted, style = PhpvpnTokens.text.metricLabel)
        Text(value, Modifier.padding(top = 8.dp), color = PhpvpnTokens.colors.ink, style = PhpvpnTokens.text.metricValue)
        if (detail != null) {
            Text(detail, Modifier.padding(top = 7.dp), color = PhpvpnTokens.colors.muted, style = PhpvpnTokens.text.metricDetail)
        }
    }
}

/** Data of one [MetricTile] for [MetricGrid]. */
@Immutable
data class Metric(val label: String, val value: String, val detail: String? = null)

/**
 * Two-column grid of metrics (`.metrics`). Falls back to one column when the
 * text no longer fits, e.g. at 320 dp with large font scaling, instead of
 * clipping the values.
 */
@Composable
fun MetricGrid(metrics: List<Metric>, modifier: Modifier = Modifier) {
    BoxWithConstraints(modifier.fillMaxWidth()) {
        val fontScale = LocalDensity.current.fontScale
        val columns = if (maxWidth / fontScale >= 260.dp) 2 else 1
        Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
            metrics.chunked(columns).forEach { row ->
                Row(Modifier.height(IntrinsicSize.Min), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    row.forEach { MetricTile(it.label, it.value, Modifier.weight(1f).fillMaxHeight(), it.detail) }
                    repeat(columns - row.size) { Column(Modifier.weight(1f)) {} }
                }
            }
        }
    }
}

@ComponentPreviews
@Composable
private fun MetricPreview() = PreviewSurface {
    MetricGrid(
        listOf(
            Metric(
                stringResource(R.string.metric_received),
                "18.42 MB",
                pluralStringResource(R.plurals.metric_ipv4_packets, 14826, "14,826"),
            ),
            Metric(
                stringResource(R.string.metric_sent),
                "4.16 MB",
                pluralStringResource(R.plurals.metric_ipv4_packets, 8204, "8,204"),
            ),
        ),
    )
}
