package phpvpn.android.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.error
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/**
 * Labelled input (`.field`): label above, 50 dp bordered box, optional hint
 * and error text below. [monospace] switches to the JSON/hex style of
 * `.field textarea`; [error] draws the `.field.invalid` state.
 */
@Composable
fun TextInput(
    value: String,
    onValueChange: (String) -> Unit,
    label: String,
    modifier: Modifier = Modifier,
    hint: String? = null,
    error: String? = null,
    enabled: Boolean = true,
    singleLine: Boolean = true,
    minLines: Int = 1,
    monospace: Boolean = false,
    keyboardOptions: KeyboardOptions = KeyboardOptions.Default,
    visualTransformation: VisualTransformation = VisualTransformation.None,
) {
    val colors = PhpvpnTokens.colors
    val shape = PhpvpnTokens.shapes.field
    val interaction = remember { MutableInteractionSource() }
    val focused by interaction.collectIsFocusedAsState()
    val (borderWidth, borderColor) = when {
        error != null -> 2.dp to colors.red
        focused -> 2.dp to colors.focus
        else -> PhpvpnTokens.dimens.hairline to colors.fieldBorder
    }
    val textStyle = (if (monospace) PhpvpnTokens.text.codeInput else PhpvpnTokens.text.fieldInput).copy(color = colors.ink)
    Column(modifier.fillMaxWidth()) {
        Text(label, Modifier.padding(bottom = 7.dp), color = colors.muted, style = PhpvpnTokens.text.fieldLabel)
        BasicTextField(
            value = value,
            onValueChange = onValueChange,
            modifier = Modifier
                .fillMaxWidth()
                .alpha(if (enabled) 1f else 0.5f)
                .semantics {
                    contentDescription = label
                    if (error != null) error(error)
                },
            enabled = enabled,
            textStyle = textStyle,
            keyboardOptions = keyboardOptions,
            singleLine = singleLine,
            minLines = minLines,
            visualTransformation = visualTransformation,
            interactionSource = interaction,
            cursorBrush = SolidColor(colors.blue),
            decorationBox = { inner ->
                Box(
                    Modifier
                        .heightIn(min = 50.dp)
                        .background(if (error != null) colors.fieldInvalidBg else colors.surface, shape)
                        .border(borderWidth, borderColor, shape)
                        .padding(horizontal = 12.dp, vertical = 13.dp),
                    contentAlignment = Alignment.CenterStart,
                ) { inner() }
            },
        )
        if (hint != null) {
            Text(hint, Modifier.padding(top = 7.dp), color = colors.muted, style = PhpvpnTokens.text.hint)
        }
        if (error != null) {
            Text(
                error,
                Modifier
                    .padding(top = 7.dp)
                    .semantics { liveRegion = LiveRegionMode.Polite },
                color = colors.red,
                style = PhpvpnTokens.text.small,
            )
        }
    }
}

/**
 * Secret input (server secret, access key, payload key, password): hidden by
 * default with a Show/Hide button, no autofill, no keyboard suggestions or
 * learning. The revealed state is local to the composition and resets when
 * the screen leaves (the design reveals "temporarily").
 */
@Composable
fun SecretField(
    value: String,
    onValueChange: (String) -> Unit,
    label: String,
    modifier: Modifier = Modifier,
    hint: String? = null,
    error: String? = null,
    enabled: Boolean = true,
    monospace: Boolean = false,
    imeAction: ImeAction = ImeAction.Default,
) {
    ExcludeFromAutofill()
    var revealed by remember { mutableStateOf(false) }
    Column(modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(PhpvpnTokens.dimens.cardGap)) {
        TextInput(
            value = value,
            onValueChange = onValueChange,
            label = label,
            hint = hint,
            error = error,
            enabled = enabled,
            monospace = monospace,
            // KeyboardType.Password keeps IMEs from suggesting or learning the
            // value, also while it is shown in clear text.
            keyboardOptions = KeyboardOptions(
                capitalization = KeyboardCapitalization.None,
                autoCorrectEnabled = false,
                keyboardType = KeyboardType.Password,
                imeAction = imeAction,
            ),
            visualTransformation = if (revealed) VisualTransformation.None else PasswordVisualTransformation(),
        )
        val shownState = stringResource(if (revealed) R.string.common_show else R.string.common_secret_hidden)
        ActionButton(
            text = stringResource(if (revealed) R.string.common_hide else R.string.common_show),
            onClick = { revealed = !revealed },
            modifier = Modifier.semantics { stateDescription = shownState },
            style = ActionStyle.Ghost,
            enabled = enabled,
        )
    }
}

/** Label with a switch (`.switch`); the whole row toggles and is one 48 dp target. */
@Composable
fun ToggleRow(
    label: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    val colors = PhpvpnTokens.colors
    val interaction = remember { MutableInteractionSource() }
    Row(
        modifier
            .fillMaxWidth()
            .then(focusOutline(interaction, PhpvpnTokens.shapes.field))
            .toggleable(
                value = checked,
                interactionSource = interaction,
                indication = null,
                enabled = enabled,
                role = Role.Switch,
                onValueChange = onCheckedChange,
            )
            .heightIn(min = PhpvpnTokens.dimens.touchTarget)
            .alpha(if (enabled) 1f else 0.5f),
        horizontalArrangement = Arrangement.spacedBy(16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, Modifier.weight(1f), color = colors.ink, style = PhpvpnTokens.text.body)
        Switch(
            checked = checked,
            onCheckedChange = null,
            enabled = enabled,
            colors = SwitchDefaults.colors(
                checkedThumbColor = colors.surface,
                checkedTrackColor = colors.blue,
                checkedBorderColor = colors.blue,
                uncheckedThumbColor = colors.surface,
                uncheckedTrackColor = colors.switchTrackOff,
                uncheckedBorderColor = colors.switchBorderOff,
                disabledCheckedThumbColor = colors.surface,
                disabledCheckedTrackColor = colors.blue,
                disabledUncheckedThumbColor = colors.surface,
                disabledUncheckedTrackColor = colors.switchTrackOff,
                disabledUncheckedBorderColor = colors.switchBorderOff,
            ),
        )
    }
}

@ComponentPreviews
@Composable
private fun FieldsPreview() = PreviewSurface {
    var url by rememberSaveable { mutableStateOf("https://vpn.example.com/phpvpn.php") }
    TextInput(url, { url = it }, stringResource(R.string.wizard_url_field), hint = stringResource(R.string.wizard_url_hint))
    var key by rememberSaveable { mutableStateOf("a29f00") }
    TextInput(
        key,
        { key = it },
        stringResource(R.string.contract_detail_key_field),
        error = stringResource(R.string.contract_invalid_error, key.length),
        monospace = true,
    )
    var secret by rememberSaveable { mutableStateOf("sample-secret") }
    SecretField(secret, { secret = it }, stringResource(R.string.wizard_secret_field))
    var suggest by rememberSaveable { mutableStateOf(true) }
    Card {
        TextInput("10.80.0.12", {}, stringResource(R.string.wizard_identity_ip_field), enabled = !suggest)
        ToggleRow(stringResource(R.string.wizard_identity_suggest_ip), suggest, { suggest = it })
        ToggleRow(stringResource(R.string.wizard_identity_default_key), false, {})
    }
}
