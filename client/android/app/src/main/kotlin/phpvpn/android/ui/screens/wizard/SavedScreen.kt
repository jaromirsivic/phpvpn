package phpvpn.android.ui.screens.wizard

import android.app.Activity
import android.content.Context
import android.net.VpnService
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.navigation.NavController
import kotlinx.coroutines.launch
import phpvpn.android.R
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.EntryRow
import phpvpn.android.ui.components.Hero
import phpvpn.android.ui.components.Hint
import phpvpn.android.ui.components.InfoRow
import phpvpn.android.ui.components.MainDestination
import phpvpn.android.ui.components.MetaLabel
import phpvpn.android.ui.components.ScreenScaffold
import phpvpn.android.ui.components.StateIcon
import phpvpn.android.ui.components.Tag
import phpvpn.android.ui.components.TopBar
import phpvpn.android.ui.navigation.ConnectionPermission
import phpvpn.android.ui.navigation.navigateToMain
import phpvpn.android.ui.navigation.openContracts
import phpvpn.android.ui.theme.PhpvpnTokens

/**
 * 17 after the Wizard graph closed: its own state (the summary handed over
 * by the confirmed Finish). Saving never connects; Connect VPN is an
 * explicit choice that still needs the system consent (03) and the checks
 * of the Connect itself (04).
 */
@Composable
fun WizardSavedRoute(nav: NavController) {
    val dependencies = rememberWizardDependencies()
    val context = LocalContext.current
    val latest = WizardSavedStore.latest
    val serverUrl = rememberSaveable { latest?.serverUrl.orEmpty() }
    val virtualIp = rememberSaveable { latest?.virtualIp.orEmpty() }
    val vpnCidr = rememberSaveable { latest?.vpnCidr.orEmpty() }
    val connect = {
        val connection = dependencies.connection
        dependencies.backgroundScope.launch { connection?.connect() }
        nav.navigateToMain(MainDestination.Connection)
    }
    val consent = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            connect()
        } else {
            // Denied consent is not a server error (37).
            nav.navigateToMain(MainDestination.Connection)
            nav.navigate(ConnectionPermission)
        }
    }
    SavedScreen(
        serverUrl = serverUrl,
        virtualIp = virtualIp,
        vpnCidr = vpnCidr,
        onBack = { nav.popBackStack() },
        onReviewContracts = { nav.openContracts() },
        onConnect = {
            val intent = consentIntent(context)
            if (intent == null) connect() else consent.launch(intent)
        },
        onLater = { nav.navigateToMain(MainDestination.Connection) },
    )
}

/** `VpnService.prepare`: null when the consent already exists (03 is skipped). */
private fun consentIntent(context: Context) = VpnService.prepare(context)

/** 17 Configuration saved. */
@Composable
fun SavedScreen(
    serverUrl: String,
    virtualIp: String,
    vpnCidr: String,
    onBack: () -> Unit,
    onReviewContracts: () -> Unit,
    onConnect: () -> Unit,
    onLater: () -> Unit,
) {
    ScreenScaffold(
        topBar = { TopBar(stringResource(R.string.wizard_saved_title), onBack = onBack) },
        footer = {
            ActionButton(stringResource(R.string.action_connect_vpn), onConnect, style = ActionStyle.Primary)
            ActionButton(stringResource(R.string.wizard_saved_later), onLater, style = ActionStyle.Ghost)
        },
    ) {
        Hero(
            StateIcon.Brand,
            stringResource(R.string.wizard_saved_heading),
            subtitle = stringResource(R.string.wizard_saved_subtitle),
            badge = { Tag(stringResource(R.string.tag_saved)) },
        )
        if (serverUrl.isNotEmpty()) {
            Card {
                MetaLabel(stringResource(R.string.server_card_label))
                Text(serverUrl, color = PhpvpnTokens.colors.ink, style = PhpvpnTokens.text.body)
                InfoRow(stringResource(R.string.row_virtual_ip), virtualIp)
                InfoRow(stringResource(R.string.row_vpn_network), vpnCidr, divider = true)
            }
        }
        EntryRow(
            stringResource(R.string.wizard_saved_review),
            onClick = onReviewContracts,
            detail = stringResource(R.string.wizard_saved_review_detail),
        )
        Hint(stringResource(R.string.wp09_saved_no_connect))
    }
}
