package phpvpn.android.ui.screens.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import phpvpn.android.BuildConfig
import phpvpn.android.R
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.CodeBlock
import phpvpn.android.ui.components.Hero
import phpvpn.android.ui.components.InfoRow
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.ScreenScaffold
import phpvpn.android.ui.components.StateIcon
import phpvpn.android.ui.components.TopBar
import phpvpn.android.ui.state.AlwaysOnState

const val ABOUT_TAG = "about-34"

/**
 * About (34): only real product metadata. The client version is the build's
 * (BuildConfig, from client/CMakeLists.txt), the native core reports its own
 * version, the Inter licence is the bundled OFL text. The design's "visual
 * concept" note is mockup-only and is not shown.
 */
@Composable
fun AboutScreen(clientVersion: () -> String, alwaysOn: AlwaysOnState?, onBack: () -> Unit) {
    // The native library may be missing in a broken install; say so instead of crashing.
    val nativeVersion = remember { runCatching(clientVersion).getOrNull() }
    var licenceShown by rememberSaveable { mutableStateOf(false) }
    ScreenScaffold(topBar = { TopBar(stringResource(R.string.about_title), onBack = onBack) }) {
        Hero(
            StateIcon.Brand,
            stringResource(R.string.brand_name),
            Modifier.testTag(ABOUT_TAG),
            subtitle = stringResource(R.string.about_tagline),
            compact = true,
        )
        Card {
            InfoRow(stringResource(R.string.about_platform), stringResource(R.string.about_platform_value))
            InfoRow(stringResource(R.string.about_version), BuildConfig.VERSION_NAME, divider = true)
            InfoRow(
                stringResource(R.string.about_native_version),
                nativeVersion ?: stringResource(R.string.about_value_unavailable),
                divider = true,
            )
            InfoRow(stringResource(R.string.about_build_type), BuildConfig.BUILD_TYPE, divider = true)
            InfoRow(stringResource(R.string.about_protocol), stringResource(R.string.brand_name), divider = true)
            InfoRow(stringResource(R.string.settings_always_on), alwaysOnText(alwaysOn), divider = true)
        }
        if (alwaysOn?.lockdown == true) {
            Notice(
                stringResource(R.string.settings_lockdown_text),
                kind = NoticeKind.Warn,
                title = stringResource(R.string.settings_lockdown_title),
            )
        }
        Card {
            InfoRow(stringResource(R.string.about_font), stringResource(R.string.about_font_licence))
            ActionButton(
                stringResource(if (licenceShown) R.string.about_licence_hide else R.string.about_licence_show),
                { licenceShown = !licenceShown },
            )
        }
        if (licenceShown) CodeBlock(rememberInterLicence())
    }
}

/** The bundled OFL text, read off the main thread (StrictMode in debug builds). */
@Composable
private fun rememberInterLicence(): String {
    val context = LocalContext.current
    var text by remember { mutableStateOf("") }
    LaunchedEffect(context) {
        text = withContext(Dispatchers.IO) {
            context.resources.openRawResource(R.raw.inter_license).use { it.readBytes().toString(Charsets.UTF_8) }
        }
    }
    return text
}
