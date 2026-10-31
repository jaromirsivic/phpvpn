package phpvpn.android.ui.screens.connection

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import phpvpn.android.BuildConfig
import phpvpn.android.R
import phpvpn.android.bridge.errors.ConnectionErrorCode
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.ConnectionPhase
import phpvpn.android.bridge.model.TrafficCounters
import phpvpn.android.errors.ErrorStrings
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.BottomNav
import phpvpn.android.ui.components.CheckState
import phpvpn.android.ui.components.ConnectionChecks
import phpvpn.android.ui.components.EntryRow
import phpvpn.android.ui.components.Eyebrow
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Hero
import phpvpn.android.ui.components.Hint
import phpvpn.android.ui.components.Intro
import phpvpn.android.ui.components.MainDestination
import phpvpn.android.ui.components.MainTopBar
import phpvpn.android.ui.components.Metric
import phpvpn.android.ui.components.MetricGrid
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.ScreenScaffold
import phpvpn.android.ui.components.StateIcon
import phpvpn.android.ui.components.Tag
import phpvpn.android.ui.components.TagKind
import phpvpn.android.ui.components.TopBar
import phpvpn.android.ui.screens.common.ServerCard
import phpvpn.android.ui.screens.common.currentLocale
import phpvpn.android.ui.screens.common.durationText
import phpvpn.android.ui.screens.common.rememberNowUtcMs
import phpvpn.android.ui.state.ConfigSummary
import phpvpn.android.ui.state.ConnectionEffect
import phpvpn.android.ui.state.ConnectionScreenModel
import phpvpn.android.ui.state.ConnectionUiState
import phpvpn.android.ui.state.ConnectionViewModel
import phpvpn.android.ui.state.DisconnectReason
import phpvpn.android.ui.state.LastKnown
import phpvpn.android.ui.state.UiFormat
import phpvpn.android.ui.state.VpnConsent
import phpvpn.android.ui.state.rememberVpnConsentLauncher
import phpvpn.android.bridge.ipc.ConfigAvailability

/** Where the Connection destination leads; the graph supplies the back stack rules. */
class ConnectionNavigation(
    val selectMain: (MainDestination) -> Unit,
    val openChecks: () -> Unit,
    val openPermissionDenied: () -> Unit,
    val openConfigAttention: () -> Unit,
    val openHttpDetails: () -> Unit,
    val openWizardUrl: () -> Unit,
)

/** Test tags of the variants, so UI tests assert the state and not only the words. */
object ConnectionTags {
    const val LOADING = "connection-loading"
    const val CONNECTED = "connection-01"
    const val DISCONNECTED = "connection-02"
    const val CONNECTING = "connection-04"
    const val RECONNECTING = "connection-05"
    const val FAILED = "connection-06"
    const val DISCONNECTING = "connection-07"
    const val UNAVAILABLE = "connection-08"
    const val UNCONFIRMED = "connection-38"
    const val PERMISSION = "connection-37"
}

/**
 * Connection destination: one route that renders 01, 02, 04, 05, 06, 07, 08,
 * 38 and the permission error from [ConnectionViewModel]. Connect asks for the
 * system VPN consent through [consent] first (03 → 04 or 37).
 */
@Composable
fun ConnectionScreen(viewModel: ConnectionViewModel, consent: VpnConsent, navigation: ConnectionNavigation) {
    val model by viewModel.screen.collectAsStateWithLifecycle()
    val launcher = rememberVpnConsentLauncher { granted -> viewModel.onConsentResult(granted) }
    LaunchedEffect(viewModel) {
        viewModel.effects.collect { effect ->
            when (effect) {
                ConnectionEffect.RequestConsent -> if (!consent.request(launcher)) viewModel.onConsentResult(true)
                ConnectionEffect.PermissionDenied -> navigation.openPermissionDenied()
                ConnectionEffect.ConfigAttention -> navigation.openConfigAttention()
            }
        }
    }
    when (val state = model.state) {
        ConnectionUiState.DisconnectUnconfirmed -> DisconnectUnconfirmedScreen(viewModel, model)
        else -> MainConnectionScreen(state, model, viewModel, navigation)
    }
}

@Composable
private fun MainConnectionScreen(
    state: ConnectionUiState,
    model: ConnectionScreenModel,
    viewModel: ConnectionViewModel,
    navigation: ConnectionNavigation,
) {
    val pending = model.commandPending
    val footer: (@Composable ColumnScope.() -> Unit)? = when (state) {
        is ConnectionUiState.Connected, is ConnectionUiState.Reconnecting -> {
            { ActionButton(stringResource(R.string.action_disconnect_vpn), viewModel::disconnect, enabled = !pending) }
        }
        is ConnectionUiState.Disconnected -> {
            { ActionButton(stringResource(R.string.action_connect_vpn), viewModel::connect, style = ActionStyle.Primary, enabled = !pending) }
        }
        is ConnectionUiState.Connecting -> {
            { ActionButton(stringResource(R.string.connecting_cancel), viewModel::disconnect, enabled = !pending) }
        }
        is ConnectionUiState.Failed -> {
            { ActionButton(stringResource(R.string.connection_error_try_again), viewModel::connect, style = ActionStyle.Primary, enabled = !pending) }
        }
        ConnectionUiState.Disconnecting -> {
            { ActionButton(stringResource(R.string.disconnecting_progress), {}, enabled = false) }
        }
        is ConnectionUiState.Unavailable -> {
            { ActionButton(stringResource(R.string.unavailable_check_again), viewModel::checkStatus, style = ActionStyle.Primary) }
        }
        ConnectionUiState.PermissionMissing -> {
            { ActionButton(stringResource(R.string.permission_denied_retry), viewModel::connect, style = ActionStyle.Primary, enabled = !pending) }
        }
        ConnectionUiState.Loading, ConnectionUiState.DisconnectUnconfirmed -> null
    }
    ScreenScaffold(
        topBar = { MainTopBar() },
        footer = footer,
        bottomBar = { BottomNav(MainDestination.Connection, navigation.selectMain) },
    ) {
        when (state) {
            ConnectionUiState.Loading -> Loading()
            is ConnectionUiState.Connected -> Connected(state, model.config, navigation)
            is ConnectionUiState.Disconnected -> Disconnected(state, model, navigation)
            is ConnectionUiState.Connecting -> Connecting(state)
            is ConnectionUiState.Reconnecting -> Reconnecting(state, model.config)
            is ConnectionUiState.Failed -> Failed(state, navigation)
            ConnectionUiState.Disconnecting -> Disconnecting(model.config)
            is ConnectionUiState.Unavailable -> Unavailable(state, navigation)
            ConnectionUiState.PermissionMissing -> PermissionContent(Modifier.testTag(ConnectionTags.PERMISSION))
            ConnectionUiState.DisconnectUnconfirmed -> Unit
        }
    }
}

@Composable
private fun Loading() {
    Hero(
        StateIcon.Brand,
        stringResource(R.string.connection_loading_heading),
        Modifier.testTag(ConnectionTags.LOADING),
        subtitle = stringResource(R.string.connection_loading_subtitle),
    )
}

/** 01: confirmed connected; duration from connectionStartedAt, counters from the status. */
@Composable
private fun Connected(state: ConnectionUiState.Connected, config: ConfigSummary?, navigation: ConnectionNavigation) {
    val now = rememberNowUtcMs()
    val started = state.startedAtUtcMs
    Hero(
        StateIcon.Connected,
        stringResource(R.string.connected_heading),
        Modifier.testTag(ConnectionTags.CONNECTED),
        subtitle = started?.let { stringResource(R.string.connected_subtitle, durationText(it, now)) },
        compact = true,
        badge = { Tag(stringResource(R.string.tag_connected)) },
    )
    ServerCard(config)
    TrafficMetrics(state.counters)
    EntryRow(
        stringResource(R.string.connected_checks_entry),
        navigation.openChecks,
        detail = stringResource(R.string.connected_checks_entry_detail),
    )
}

/** The two VPN tiles of 01 and 26: IPv4 bytes and packets per direction. */
@Composable
internal fun TrafficMetrics(counters: TrafficCounters) {
    val locale = currentLocale()
    MetricGrid(
        listOf(
            Metric(
                stringResource(R.string.metric_received),
                UiFormat.bytes(counters.ipv4BytesReceived, locale),
                packets(counters.ipv4PacketsReceived),
            ),
            Metric(
                stringResource(R.string.metric_sent),
                UiFormat.bytes(counters.ipv4BytesSent, locale),
                packets(counters.ipv4PacketsSent),
            ),
        ),
    )
}

@Composable
private fun packets(count: ULong): String {
    val quantity = if (count > Int.MAX_VALUE.toULong()) Int.MAX_VALUE else count.toInt()
    return pluralStringResource(R.plurals.metric_ipv4_packets, quantity, UiFormat.integer(count, currentLocale()))
}

/** 02: one current configuration; the reason when Android ended the connection. */
@Composable
private fun Disconnected(state: ConnectionUiState.Disconnected, model: ConnectionScreenModel, navigation: ConnectionNavigation) {
    Hero(
        StateIcon.Disconnected,
        stringResource(R.string.disconnected_heading),
        Modifier.testTag(ConnectionTags.DISCONNECTED),
        subtitle = stringResource(R.string.disconnected_subtitle),
        badge = { Tag(stringResource(R.string.tag_disconnected), kind = TagKind.Neutral) },
    )
    when (state.reason) {
        DisconnectReason.REVOKED -> Notice(
            stringResource(R.string.disconnected_revoked_text),
            kind = NoticeKind.Warn,
            title = stringResource(R.string.disconnected_revoked_title),
        )
        DisconnectReason.PROCESS_RESTARTED -> Notice(
            stringResource(R.string.disconnected_restarted_text),
            kind = NoticeKind.Warn,
            title = stringResource(R.string.disconnected_restarted_title),
        )
        null -> Unit
    }
    model.refusedConnect?.let { key ->
        Notice(stringResource(ErrorStrings.resource(key)), kind = NoticeKind.Error, title = stringResource(R.string.connect_refused_title))
    }
    ServerCard(model.config)
    val config = model.config
    when {
        config == null -> Unit
        config.availability == ConfigAvailability.VALID -> Notice(
            stringResource(R.string.disconnected_notice_text),
            title = stringResource(R.string.disconnected_notice_title),
        )
        else -> EntryRow(
            stringResource(R.string.attention_title),
            navigation.openConfigAttention,
            detail = stringResource(R.string.disconnected_attention_detail),
        )
    }
    EntryRow(
        stringResource(R.string.disconnected_edit_configuration),
        { navigation.selectMain(MainDestination.Settings) },
        detail = stringResource(R.string.disconnected_edit_configuration_detail),
    )
}

/** 04: the four checks; the screen can be left, progress is also in the notification. */
@Composable
private fun Connecting(state: ConnectionUiState.Connecting) {
    Hero(
        StateIcon.Connecting,
        stringResource(R.string.connecting_heading),
        Modifier.testTag(ConnectionTags.CONNECTING),
        subtitle = stringResource(R.string.connecting_subtitle),
        badge = { Tag(stringResource(R.string.tag_connecting), kind = TagKind.Warn) },
    )
    if (state.statusUnavailable) {
        val error = ConnectionErrorCode.STATUS_UNAVAILABLE
        Notice(
            stringResource(ErrorStrings.resource(error.message)),
            kind = NoticeKind.Warn,
            title = stringResource(R.string.connecting_status_unavailable_title),
        )
    }
    ConnectionChecks(state.checks)
    Notice(stringResource(R.string.connecting_notice_text), title = stringResource(R.string.connecting_notice_title))
}

/** 05: distinct from 01; recovery is not a confirmed connection. */
@Composable
private fun Reconnecting(state: ConnectionUiState.Reconnecting, config: ConfigSummary?) {
    Hero(
        StateIcon.Connecting,
        stringResource(R.string.reconnecting_heading),
        Modifier.testTag(ConnectionTags.RECONNECTING),
        subtitle = stringResource(R.string.reconnecting_subtitle),
        badge = { Tag(stringResource(R.string.tag_reconnecting), kind = TagKind.Warn) },
    )
    Notice(
        stringResource(R.string.reconnecting_notice_text),
        kind = NoticeKind.Warn,
        title = stringResource(R.string.reconnecting_notice_title),
    )
    ServerCard(config)
    val error = state.lastError ?: return
    var shown by rememberSaveable { mutableStateOf(false) }
    if (shown) {
        Notice(stringResource(ErrorStrings.resource(error)), kind = NoticeKind.Error)
    } else {
        ActionButton(stringResource(R.string.reconnecting_last_error), { shown = true })
    }
}

/**
 * 06: texts only from resources by code. Rejected input and transport
 * failures read differently; the HTTP entry exists only with captured data.
 */
@Composable
private fun Failed(state: ConnectionUiState.Failed, navigation: ConnectionNavigation) {
    val error = state.error
    Eyebrow(
        stringResource(if (state.inputError) R.string.connection_error_eyebrow_input else R.string.connection_error_eyebrow),
        Modifier.testTag(ConnectionTags.FAILED),
    )
    Heading(stringResource(ErrorStrings.resource(error.panel)))
    Intro(stringResource(ErrorStrings.resource(error.action)))
    Notice(
        stringResource(ErrorStrings.resource(error.description)),
        kind = NoticeKind.Error,
        title = stringResource(if (state.inputError) R.string.connection_error_kind_input else R.string.connection_error_kind_transport),
    )
    if (error.code == ConnectionErrorCode.SERVER_VERSION_INCOMPATIBLE.wireName) {
        Hint(stringResource(R.string.connection_error_client_version, BuildConfig.VERSION_NAME))
    }
    ConnectionChecks(state.checks)
    if (state.diagnostic) {
        EntryRow(
            stringResource(R.string.entry_http_error_details),
            navigation.openHttpDetails,
            detail = stringResource(R.string.connection_error_http_detail),
        )
    }
    if (state.inputError) {
        EntryRow(
            stringResource(R.string.connection_error_check_configuration),
            navigation.openWizardUrl,
            detail = stringResource(R.string.connection_error_check_configuration_detail),
            divider = state.diagnostic,
        )
    } else {
        EntryRow(stringResource(R.string.connection_error_check_url), navigation.openWizardUrl, divider = state.diagnostic)
    }
}

/** 07: waits for the confirmed disconnected state. */
@Composable
private fun Disconnecting(config: ConfigSummary?) {
    Hero(
        StateIcon.Connecting,
        stringResource(R.string.disconnecting_heading),
        Modifier.testTag(ConnectionTags.DISCONNECTING),
        subtitle = stringResource(R.string.disconnecting_subtitle),
        badge = { Tag(stringResource(R.string.tag_disconnecting), kind = TagKind.Warn) },
    )
    Notice(stringResource(R.string.disconnecting_notice_text), title = stringResource(R.string.disconnecting_notice_title))
    ServerCard(config)
}

/** 08: neither state is confirmed; the last known state is only a hint. */
@Composable
private fun Unavailable(state: ConnectionUiState.Unavailable, navigation: ConnectionNavigation) {
    Hero(
        StateIcon.Disconnected,
        stringResource(R.string.unavailable_heading),
        Modifier.testTag(ConnectionTags.UNAVAILABLE),
        subtitle = stringResource(R.string.unavailable_subtitle),
        badge = { Tag(stringResource(R.string.tag_status_unknown), kind = TagKind.Warn) },
    )
    Notice(lastKnownText(state.lastKnown), kind = NoticeKind.Warn, title = stringResource(R.string.unavailable_notice_title))
    if (state.lastKnown != null) {
        EntryRow(
            stringResource(R.string.unavailable_last_statistics),
            { navigation.selectMain(MainDestination.Traffic) },
            detail = stringResource(R.string.unavailable_last_statistics_detail),
        )
    }
}

@Composable
private fun lastKnownText(lastKnown: LastKnown?): String {
    if (lastKnown == null) return stringResource(R.string.unavailable_last_known_none)
    val status = lastKnown.status
    val locale = currentLocale()
    return when {
        status.state == ConnectionState.CONNECTED -> stringResource(
            R.string.unavailable_last_known_connected,
            UiFormat.time(status.connectionStartedAtUtcMs ?: lastKnown.seenAtUtcMs, locale),
        )
        status.state == ConnectionState.CONNECTING || status.state == ConnectionState.RECOVERING ||
            status.phase == ConnectionPhase.STOPPING ->
            stringResource(R.string.unavailable_last_known_connecting, UiFormat.time(lastKnown.seenAtUtcMs, locale))
        else -> stringResource(R.string.unavailable_last_known_disconnected, UiFormat.time(lastKnown.seenAtUtcMs, locale))
    }
}

/** 38: the VPN may still be active; no fake confirmation. Back is "Check status". */
@Composable
private fun DisconnectUnconfirmedScreen(viewModel: ConnectionViewModel, model: ConnectionScreenModel) {
    BackHandler(onBack = viewModel::leaveUnconfirmed)
    ScreenScaffold(
        topBar = { TopBar(stringResource(R.string.disconnect_unknown_title), onBack = viewModel::leaveUnconfirmed) },
        footer = {
            ActionButton(
                stringResource(R.string.disconnect_unknown_retry),
                viewModel::disconnect,
                style = ActionStyle.Primary,
                enabled = !model.commandPending,
            )
            ActionButton(stringResource(R.string.disconnect_unknown_check), viewModel::leaveUnconfirmed, style = ActionStyle.Ghost)
        },
    ) {
        Hero(
            StateIcon.Connecting,
            stringResource(R.string.disconnect_unknown_heading),
            Modifier.testTag(ConnectionTags.UNCONFIRMED),
            subtitle = stringResource(R.string.disconnect_unknown_subtitle),
            badge = { Tag(stringResource(R.string.tag_status_unknown), kind = TagKind.Warn) },
        )
        Notice(
            stringResource(R.string.disconnect_unknown_notice_text),
            kind = NoticeKind.Warn,
            title = stringResource(R.string.disconnect_unknown_notice_title),
        )
    }
}

/** The body of 37, shared by the route and by the `tunnel_permission_missing` status. */
@Composable
internal fun PermissionContent(modifier: Modifier = Modifier) {
    Hero(
        StateIcon.Disconnected,
        stringResource(R.string.permission_denied_heading),
        modifier,
        subtitle = stringResource(R.string.permission_denied_subtitle),
        badge = { Tag(stringResource(R.string.tag_disconnected), kind = TagKind.Neutral) },
    )
    Notice(stringResource(R.string.permission_denied_notice_text), title = stringResource(R.string.permission_denied_notice_title))
}

/** Checks for screen 48 when no status exists yet: all waiting, never passed. */
internal val NO_CHECKS: List<CheckState> = List(4) { CheckState.Off }
