package phpvpn.android.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/**
 * Wizard progress (`.step-caption` + `.stepper`): "PHONE SETUP", "Step n of
 * 4" and one bar per step. Announced as a single "Step n of 4" node.
 */
@Composable
fun Stepper(step: Int, modifier: Modifier = Modifier, total: Int = 4) {
    require(step in 1..total) { "step $step outside 1..$total" }
    val colors = PhpvpnTokens.colors
    val stepText = stringResource(R.string.wizard_step_of, step, total)
    Column(
        modifier
            .fillMaxWidth()
            .clearAndSetSemantics {
                contentDescription = stepText
                progressBarRangeInfo = ProgressBarRangeInfo(step.toFloat(), 0f..total.toFloat(), total - 1)
            },
    ) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(
                stringResource(R.string.wizard_step_caption),
                Modifier.weight(1f),
                color = colors.muted,
                style = PhpvpnTokens.text.stepCaption,
            )
            Text(stepText, color = colors.muted, style = PhpvpnTokens.text.stepCaption)
        }
        Row(
            Modifier
                .fillMaxWidth()
                .padding(top = 18.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            repeat(total) { index ->
                Box(
                    Modifier
                        .weight(1f)
                        .height(4.dp)
                        .background(if (index < step) colors.blue else colors.stepperTrack, RoundedCornerShape(4.dp)),
                )
            }
        }
    }
}

@ComponentPreviews
@Composable
private fun StepperPreview() = PreviewSurface {
    (1..4).forEach { Stepper(it) }
}
