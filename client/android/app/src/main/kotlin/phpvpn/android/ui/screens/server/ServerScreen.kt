package phpvpn.android.ui.screens.server

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.ui.semantics.semantics
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import java.util.Locale
import phpvpn.android.BuildConfig
import phpvpn.android.R
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.errors.ErrorTexts
import phpvpn.android.errors.ErrorStrings
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.BottomNav
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.CodeBlock
import phpvpn.android.ui.components.Eyebrow
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Hint
import phpvpn.android.ui.components.MainDestination
import phpvpn.android.ui.components.MainTopBar
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.Pager
import phpvpn.android.ui.components.RowDivider
import phpvpn.android.ui.components.ScreenScaffold
import phpvpn.android.ui.components.SectionTitle
import phpvpn.android.ui.components.Tabs
import phpvpn.android.ui.components.Tag
import phpvpn.android.ui.components.TagKind
import phpvpn.android.ui.components.ToggleRow
import phpvpn.android.ui.navigation.ServerTab
import phpvpn.android.ui.screens.common.currentLocale
import phpvpn.android.ui.state.ServerClientState
import phpvpn.android.ui.state.ServerInfoRow
import phpvpn.android.ui.state.ServerUiState
import phpvpn.android.ui.state.ServerVariant
import phpvpn.android.ui.state.ServerViewModel
import phpvpn.android.ui.state.UiFormat
import phpvpn.android.ui.theme.PhpvpnTokens

object ServerTags {
    const val LOADING = "server-loading"
    const val LIST = "server-28"
    const val RAW = "server-29"
    const val EMPTY = "server-44"
    const val STALE = "server-45-stale"
    const val FAILED = "server-45-failed"
}

/**
 * Server destination: who is online (28) and the raw server data (29) of the
 * same snapshot, the valid empty list (44) and a failed refresh (45). The tab
 * is screen state seeded from the route (`server?tab=raw`).
 */
@Composable
fun ServerScreen(viewModel: ServerViewModel, initialTab: ServerTab, onSelectMain: (MainDestination) -> Unit) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    var tab by rememberSaveable { mutableStateOf(initialTab) }
    ScreenScaffold(
        topBar = { MainTopBar() },
        bottomBar = { BottomNav(MainDestination.Server, onSelectMain) },
    ) {
        val host = state.serverHost?.uppercase(Locale.ROOT)
        Eyebrow(
            when {
                state.variant == ServerVariant.STALE -> stringResource(R.string.server_stale_eyebrow)
                host != null -> stringResource(R.string.server_eyebrow, host)
                else -> stringResource(R.string.server_eyebrow_plain)
            },
        )
        Heading(stringResource(if (tab == ServerTab.Online) R.string.server_heading else R.string.server_raw_heading))
        Tabs(
            listOf(stringResource(R.string.server_tab_online), stringResource(R.string.server_tab_raw)),
            tab.ordinal,
            { tab = ServerTab.entries[it] },
        )
        when (state.variant) {
            ServerVariant.LOADING -> Notice(stringResource(R.string.server_loading), Modifier.testTag(ServerTags.LOADING))
            ServerVariant.FAILED -> Failed(state, viewModel)
            else -> if (tab == ServerTab.Online) Online(state, viewModel) else Raw(state, viewModel)
        }
    }
}

/** 45 without earlier data: the explanation and Retry, never a cached list. */
@Composable
private fun Failed(state: ServerUiState, viewModel: ServerViewModel) {
    val code = state.failure?.code
    Notice(
        stringResource(ErrorStrings.resource(ErrorTexts.messageKey(code))),
        Modifier.testTag(ServerTags.FAILED),
        kind = NoticeKind.Error,
        title = stringResource(R.string.server_failed_title),
    )
    if (code == ErrorCode.SERVER_VERSION_INCOMPATIBLE.wireName) {
        Hint(stringResource(R.string.connection_error_client_version, BuildConfig.VERSION_NAME))
    }
    Hint(stringResource(R.string.server_failed_hint))
    ActionButton(stringResource(R.string.server_stale_retry), viewModel::refresh, style = ActionStyle.Primary, enabled = !state.loading)
}

/** 45 with data: the last success stays, marked stale with its refresh time. */
@Composable
private fun StaleNotice(state: ServerUiState) {
    val time = state.refreshedAtUtcMs?.let { UiFormat.time(it, currentLocale()) } ?: stringResource(R.string.value_missing)
    Notice(
        stringResource(R.string.server_stale_notice_text, time),
        Modifier.testTag(ServerTags.STALE),
        kind = NoticeKind.Warn,
        title = stringResource(R.string.server_stale_notice_title),
    )
}

@Composable
private fun Online(state: ServerUiState, viewModel: ServerViewModel) {
    val model = state.model ?: return
    val stale = state.variant == ServerVariant.STALE
    if (stale) StaleNotice(state)
    if (state.variant == ServerVariant.EMPTY) {
        Card(Modifier.testTag(ServerTags.EMPTY)) {
            Text(
                stringResource(R.string.server_empty_heading),
                Modifier.fillMaxWidth(),
                color = PhpvpnTokens.colors.ink,
                style = PhpvpnTokens.text.title,
                textAlign = TextAlign.Center,
            )
            Text(
                stringResource(R.string.server_empty_text),
                Modifier.fillMaxWidth(),
                color = PhpvpnTokens.colors.muted,
                style = PhpvpnTokens.text.body,
                textAlign = TextAlign.Center,
            )
        }
        RefreshControls(state, viewModel)
        Notice(stringResource(R.string.server_empty_notice_text), title = stringResource(R.string.server_empty_notice_title))
        return
    }
    if (!stale) {
        val summary = stringResource(
            R.string.server_summary,
            pluralStringResource(R.plurals.server_online_count, model.onlineClients, model.onlineClients),
            pluralStringResource(R.plurals.server_valid_contracts, model.validOnlineClients, model.validOnlineClients),
        )
        SectionTitle(summary, Modifier.testTag(ServerTags.LIST)) {
            state.refreshedAtUtcMs?.let { Hint(UiFormat.time(it, currentLocale())) }
        }
    }
    Card {
        Column {
            state.pageRows.forEachIndexed { index, row ->
                if (index > 0) RowDivider()
                PeerStatusRow(row, model.localAddress, stale)
            }
        }
    }
    if (state.pageCount > 1) {
        Pager(
            stringResource(R.string.wp08_page_of, state.page + 1, state.pageCount),
            previousLabel = stringResource(R.string.wp08_page_previous),
            onPrevious = if (state.page > 0) ({ viewModel.showPage(state.page - 1) }) else null,
            nextLabel = stringResource(R.string.wp08_page_next),
            onNext = if (state.page < state.pageCount - 1) ({ viewModel.showPage(state.page + 1) }) else null,
        )
    }
    if (stale) {
        ActionButton(stringResource(R.string.server_stale_retry), viewModel::refresh, style = ActionStyle.Primary, enabled = !state.loading)
    } else {
        RefreshControls(state, viewModel)
        Hint(stringResource(R.string.server_online_hint))
    }
}

/** Auto refresh (5 s after the previous refresh) and Refresh now, as in the desktop window. */
@Composable
private fun RefreshControls(state: ServerUiState, viewModel: ServerViewModel) {
    Card {
        ToggleRow(stringResource(R.string.server_auto_refresh), state.autoRefresh, viewModel::setAutoRefresh)
    }
    ActionButton(stringResource(R.string.action_refresh_now), viewModel::refresh, enabled = !state.loading)
}

/**
 * One remote client: presence and contract validity are separate facts
 * (28). A stale snapshot never claims "online now" (45).
 */
@Composable
private fun PeerStatusRow(row: ServerInfoRow, localAddress: String, stale: Boolean) {
    val colors = PhpvpnTokens.colors
    val locale = currentLocale()
    val online = row.state != ServerClientState.OFFLINE
    Column(
        Modifier
            .fillMaxWidth()
            .padding(vertical = 14.dp)
            .semantics(mergeDescendants = true) {},
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(row.peer, Modifier.weight(1f), color = colors.ink, style = PhpvpnTokens.text.peerTitle)
            when {
                stale && online -> Tag(stringResource(R.string.tag_last_status_online), kind = TagKind.Neutral, small = true)
                !online -> Tag(stringResource(R.string.tag_offline), kind = TagKind.Neutral, small = true)
                row.state == ServerClientState.VALID -> Tag(stringResource(R.string.tag_online), small = true)
                else -> Tag(stringResource(R.string.tag_online), kind = TagKind.Warn, small = true)
            }
        }
        Text(stringResource(R.string.server_contract_with, localAddress), color = colors.muted, style = PhpvpnTokens.text.small)
        if (!stale && row.state == ServerClientState.VALID) {
            Text(stringResource(R.string.server_contract_valid), color = colors.green, style = PhpvpnTokens.text.small)
        } else if (!stale && row.state == ServerClientState.INVALID) {
            Text(stringResource(R.string.server_contract_invalid), color = colors.red, style = PhpvpnTokens.text.small)
        }
        Text(
            stringResource(R.string.server_last_seen, UiFormat.timeWithSeconds(row.lastSeenMs, locale)),
            color = colors.muted,
            style = PhpvpnTokens.text.small,
        )
    }
}

/** 29: the authenticated plaintext of the same snapshot as 28, keys never renamed. */
@Composable
private fun Raw(state: ServerUiState, viewModel: ServerViewModel) {
    val model = state.model ?: return
    if (state.variant == ServerVariant.STALE) StaleNotice(state)
    Notice(
        stringResource(R.string.server_raw_notice_text),
        Modifier.testTag(ServerTags.RAW),
        title = stringResource(R.string.server_raw_notice_title),
    )
    CodeBlock(state.rawText.ifEmpty { model.infoPlaintext })
    ActionButton(stringResource(R.string.action_refresh_now), viewModel::refresh, enabled = !state.loading)
}
