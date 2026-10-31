package phpvpn.android.ui.screens.common

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelStoreOwner
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.repeatOnLifecycle
import androidx.lifecycle.viewmodel.compose.LocalViewModelStoreOwner
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import java.util.Locale
import kotlinx.coroutines.delay
import phpvpn.android.AppContainer
import phpvpn.android.R
import phpvpn.android.appContainer
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.InfoRow
import phpvpn.android.ui.components.MetaLabel
import phpvpn.android.ui.state.ConfigSummary
import phpvpn.android.ui.state.UiFormat
import phpvpn.android.ui.theme.PhpvpnTokens

/** A view model built from the process [AppContainer], scoped to [owner] (the back stack entry by default). */
@Composable
inline fun <reified VM : ViewModel> appViewModel(
    owner: ViewModelStoreOwner = checkNotNull(LocalViewModelStoreOwner.current),
    crossinline create: (AppContainer) -> VM,
): VM {
    val container = LocalContext.current.appContainer
    return viewModel(viewModelStoreOwner = owner, factory = viewModelFactory { initializer { create(container) } })
}

/** The locale of the current configuration (values-cs switches it). */
@Composable
fun currentLocale(): Locale = LocalConfiguration.current.locales[0] ?: Locale.getDefault()

/**
 * Wall clock that ticks once a second, only while the screen is at least
 * STARTED: the connected duration (01) needs no timer with the screen off.
 */
@Composable
fun rememberNowUtcMs(): Long {
    var now by remember { mutableLongStateOf(System.currentTimeMillis()) }
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    LaunchedEffect(lifecycle) {
        lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
            while (true) {
                now = System.currentTimeMillis()
                delay(1_000)
            }
        }
    }
    return now
}

/** "24 min 18 s" under an hour, "1 h 05 min" above. */
@Composable
fun durationText(startUtcMs: Long, nowUtcMs: Long): String {
    val elapsed = UiFormat.elapsed(startUtcMs, nowUtcMs)
    return if (elapsed.hours > 0) {
        stringResource(R.string.duration_h_min, elapsed.hours.toInt(), elapsed.minutes.toInt())
    } else {
        stringResource(R.string.duration_min_sec, elapsed.minutes.toInt(), elapsed.seconds.toInt())
    }
}

/**
 * The server card of 01, 02, 05, 07 and 48: configured server URL, virtual IP
 * and VPN network. Drawn only for the members the configuration really has.
 */
@Composable
fun ServerCard(config: ConfigSummary?) {
    if (config == null || !config.hasDetails) return
    Card {
        config.serverUrl?.let { url ->
            MetaLabel(stringResource(R.string.server_card_label))
            Text(url, color = PhpvpnTokens.colors.ink, style = PhpvpnTokens.text.body)
        }
        config.virtualIp?.let { InfoRow(stringResource(R.string.row_virtual_ip), it) }
        config.vpnCidr?.let { InfoRow(stringResource(R.string.row_vpn_network), it, divider = config.virtualIp != null) }
    }
}

/** Opens a system settings page; a device without it simply does nothing. */
fun Context.openSystemPage(action: String) {
    try {
        startActivity(Intent(action).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
    } catch (_: ActivityNotFoundException) {
        // Some vendors remove these pages; nothing else can be offered.
    }
}
