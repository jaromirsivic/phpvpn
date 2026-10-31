package phpvpn.android.ui.screens.settings

import android.provider.Settings as AndroidSettings
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import phpvpn.android.R
import phpvpn.android.ui.components.BottomNav
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.EntryRow
import phpvpn.android.ui.components.Eyebrow
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.MainDestination
import phpvpn.android.ui.components.MainTopBar
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.ScreenScaffold
import phpvpn.android.ui.components.SymbolIcon
import phpvpn.android.ui.components.SymbolText
import phpvpn.android.ui.screens.common.openSystemPage
import phpvpn.android.ui.state.AlwaysOnState
import phpvpn.android.ui.state.SettingsViewModel
import phpvpn.android.ui.state.WizardEntry

/** Where the hub leads; the graph owns the back stack rules (NavActions.kt). */
class SettingsNavigation(
    val selectMain: (MainDestination) -> Unit,
    val openWizardGate: () -> Unit,
    val openWizardUrl: () -> Unit,
    val openContracts: () -> Unit,
    val openEditor: () -> Unit,
    val openAbout: () -> Unit,
    val openExit: () -> Unit,
)

const val SETTINGS_TAG = "settings-10"

/**
 * Settings hub (10): wizard (through 11 unless confirmed disconnected),
 * contracts, editor, About, Exit and the Android background settings. No
 * profiles, countries or accounts (design rule).
 */
@Composable
fun SettingsScreen(viewModel: SettingsViewModel, navigation: SettingsNavigation) {
    val alwaysOn by viewModel.alwaysOn.collectAsStateWithLifecycle()
    val context = LocalContext.current
    ScreenScaffold(
        topBar = { MainTopBar() },
        bottomBar = { BottomNav(MainDestination.Settings, navigation.selectMain) },
    ) {
        Eyebrow(stringResource(R.string.settings_eyebrow), Modifier.testTag(SETTINGS_TAG))
        Heading(stringResource(R.string.settings_title))
        Card {
            EntryRow(
                stringResource(R.string.settings_wizard),
                {
                    when (viewModel.wizardEntry()) {
                        WizardEntry.URL -> navigation.openWizardUrl()
                        WizardEntry.GATE -> navigation.openWizardGate()
                    }
                },
                detail = stringResource(R.string.settings_wizard_detail),
                symbol = { SymbolText(stringResource(R.string.symbol_wizard)) },
            )
            EntryRow(
                stringResource(R.string.entry_communication_contracts),
                navigation.openContracts,
                detail = stringResource(R.string.settings_contracts_detail),
                divider = true,
                symbol = { SymbolIcon(painterResource(R.drawable.ic_swap)) },
            )
        }
        Card {
            EntryRow(
                stringResource(R.string.entry_configuration_editor),
                navigation.openEditor,
                detail = stringResource(R.string.settings_editor_detail),
                symbol = { SymbolText(stringResource(R.string.symbol_editor)) },
            )
            EntryRow(
                stringResource(R.string.settings_about),
                navigation.openAbout,
                detail = stringResource(R.string.settings_about_detail),
                divider = true,
                symbol = { SymbolText(stringResource(R.string.symbol_about)) },
            )
        }
        Notice(stringResource(R.string.settings_notice_text), title = stringResource(R.string.settings_notice_title))
        Card {
            EntryRow(
                stringResource(R.string.settings_battery),
                { context.openSystemPage(AndroidSettings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS) },
                detail = stringResource(R.string.settings_battery_detail),
            )
            EntryRow(
                stringResource(R.string.settings_always_on),
                { context.openSystemPage(AndroidSettings.ACTION_VPN_SETTINGS) },
                detail = alwaysOnText(alwaysOn),
                divider = true,
            )
        }
        if (alwaysOn?.lockdown == true) {
            Notice(
                stringResource(R.string.settings_lockdown_text),
                kind = NoticeKind.Warn,
                title = stringResource(R.string.settings_lockdown_title),
            )
        }
        EntryRow(stringResource(R.string.settings_exit), navigation.openExit, detail = stringResource(R.string.settings_exit_detail))
    }
}

/** Always-on as Android reported it to the service; unknown is said, never shown as Off. */
@Composable
internal fun alwaysOnText(state: AlwaysOnState?): String = when {
    state == null -> stringResource(R.string.settings_always_on_unknown)
    state.alwaysOn -> stringResource(R.string.settings_always_on_on)
    else -> stringResource(R.string.settings_always_on_off)
}
