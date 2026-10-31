package phpvpn.android.ui.screens.password

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.foundation.layout.fillMaxWidth
import phpvpn.android.R
import phpvpn.android.bridge.controller.PasswordKeyError
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionRow
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.Eyebrow
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Intro
import phpvpn.android.ui.components.MetaLabel
import phpvpn.android.ui.components.MonoText
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.ScreenScaffold
import phpvpn.android.ui.components.SecretField
import phpvpn.android.ui.components.TopBar
import phpvpn.android.ui.screens.SecureWindow

/**
 * 21 Key from password: SHA-256 of the exact password, Default (the
 * canonical pair of the contract) and Random. Use key applies the key to the
 * captured contract only, and only while that contract is enabled.
 */
@Composable
fun PasswordKeyScreen(
    vm: PasswordKeyViewModel,
    remoteIp: String,
    contractId: String?,
    targetEnabled: Boolean,
    stale: Boolean,
    onBack: () -> Unit,
    onUse: () -> Unit,
) {
    SecureWindow()
    ScreenScaffold(
        topBar = { TopBar(stringResource(R.string.password_key_title), onBack = onBack) },
        footer = {
            ActionButton(
                stringResource(R.string.password_key_use),
                onUse,
                Modifier.testTag(TAG_USE_KEY),
                style = ActionStyle.Primary,
                enabled = vm.valid && targetEnabled && contractId != null && !stale,
            )
        },
    ) {
        Eyebrow(stringResource(R.string.password_key_eyebrow, remoteIp))
        Heading(stringResource(R.string.password_key_title))
        Intro(stringResource(R.string.password_key_intro))
        SecretField(
            value = vm.password,
            onValueChange = { vm.update(it) },
            label = stringResource(R.string.password_key_field),
            modifier = Modifier.testTag(TAG_PASSWORD_FIELD),
            error = when (vm.error) {
                PasswordKeyError.NONE -> null
                PasswordKeyError.TOO_LONG -> stringResource(R.string.password_key_too_long)
                PasswordKeyError.INVALID_UTF16 -> stringResource(R.string.password_key_invalid_utf16)
                PasswordKeyError.UNSUPPORTED_CONTROL -> stringResource(R.string.password_key_control)
                PasswordKeyError.CRYPTO_FAILED -> stringResource(R.string.password_key_crypto)
            },
        )
        ActionRow {
            ActionButton(
                stringResource(R.string.password_key_default),
                { contractId?.let(vm::useDefault) },
                Modifier.weight(1f).testTag(TAG_DEFAULT),
                enabled = contractId != null,
            )
            ActionButton(stringResource(R.string.password_key_random), vm::useRandom, Modifier.weight(1f).testTag(TAG_RANDOM))
        }
        if (vm.randomFailed) Notice(stringResource(R.string.password_key_random_failed), kind = NoticeKind.Error)
        Card {
            MetaLabel(stringResource(R.string.password_key_preview))
            MonoText(vm.keyPreview.ifEmpty { stringResource(R.string.value_missing) }, Modifier.fillMaxWidth().testTag(TAG_KEY_PREVIEW))
        }
        when {
            stale || contractId == null -> Notice(stringResource(R.string.password_key_stale), kind = NoticeKind.Error)
            !targetEnabled -> Notice(stringResource(R.string.password_key_target_disabled), kind = NoticeKind.Warn)
            else -> Notice(
                stringResource(R.string.password_key_notice_text),
                kind = NoticeKind.Info,
                title = stringResource(R.string.password_key_notice_title),
            )
        }
    }
}

const val TAG_USE_KEY = "password-use-key"
const val TAG_PASSWORD_FIELD = "password-field"
const val TAG_DEFAULT = "password-default"
const val TAG_RANDOM = "password-random"
const val TAG_KEY_PREVIEW = "password-key-preview"
