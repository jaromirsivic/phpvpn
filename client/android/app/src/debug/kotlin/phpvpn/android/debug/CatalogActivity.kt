package phpvpn.android.debug

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.enableLightEdgeToEdge
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionRow
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.BottomNav
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.CardVariant
import phpvpn.android.ui.components.CheckRow
import phpvpn.android.ui.components.CheckState
import phpvpn.android.ui.components.CodeBlock
import phpvpn.android.ui.components.ConnectionChecks
import phpvpn.android.ui.components.DialogCard
import phpvpn.android.ui.components.EntryRow
import phpvpn.android.ui.components.Eyebrow
import phpvpn.android.ui.components.FooterActions
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Hero
import phpvpn.android.ui.components.Hint
import phpvpn.android.ui.components.InfoRow
import phpvpn.android.ui.components.Intro
import phpvpn.android.ui.components.MainDestination
import phpvpn.android.ui.components.MainTopBar
import phpvpn.android.ui.components.MetaLabel
import phpvpn.android.ui.components.Metric
import phpvpn.android.ui.components.MetricGrid
import phpvpn.android.ui.components.MonoText
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.Pager
import phpvpn.android.ui.components.PeerRow
import phpvpn.android.ui.components.ScreenScaffold
import phpvpn.android.ui.components.SecretField
import phpvpn.android.ui.components.SectionTitle
import phpvpn.android.ui.components.StateIcon
import phpvpn.android.ui.components.Stepper
import phpvpn.android.ui.components.SymbolIcon
import phpvpn.android.ui.components.SymbolText
import phpvpn.android.ui.components.Tabs
import phpvpn.android.ui.components.Tag
import phpvpn.android.ui.components.TagKind
import phpvpn.android.ui.components.TextInput
import phpvpn.android.ui.components.Title
import phpvpn.android.ui.components.ToggleRow
import phpvpn.android.ui.components.TopBar
import phpvpn.android.ui.theme.PhpvpnTheme

/**
 * Debug-only component catalog (WP07 "Hotovo, když"): every component of
 * ui.components in every state, on the design's page background, at the
 * device width. `--es section <name>` shows a single section, e.g.
 * `adb shell am start -n <app>/phpvpn.android.debug.CatalogActivity --es section notice`.
 * Sample values (addresses, sizes) are data, the texts come from resources.
 */
class CatalogActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        enableLightEdgeToEdge()
        super.onCreate(savedInstanceState)
        val only = intent.getStringExtra("section")
        setContent {
            PhpvpnTheme {
                ScreenScaffold(topBar = { TopBar(stringResource(R.string.debug_catalog_title)) }) {
                    CatalogSection.entries.filter { only == null || it.key == only }.forEach { section ->
                        Eyebrow(stringResource(section.title))
                        section.content(this)
                    }
                }
            }
        }
    }
}

private enum class CatalogSection(val key: String, @param:StringRes val title: Int, val content: @Composable ColumnScope.() -> Unit) {
    TextStyles("text", R.string.debug_section_text, { TextSection() }),
    CardSection("card", R.string.debug_section_card, { CardSample() }),
    TagSection("tag", R.string.debug_section_tag, { TagSample() }),
    HeroSection("hero", R.string.debug_section_hero, { HeroSample() }),
    RowSection("row", R.string.debug_section_info_row, { RowSample() }),
    MetricSection("metric", R.string.debug_section_metric, { MetricSample() }),
    CheckSection("check", R.string.debug_section_check, { CheckSample() }),
    StepperSection("stepper", R.string.debug_section_stepper, { (1..4).forEach { Stepper(it) } }),
    NoticeSection("notice", R.string.debug_section_notice, { NoticeSample() }),
    ActionSection("actions", R.string.debug_section_actions, { ActionSample() }),
    FooterSection("footer", R.string.debug_section_footer, { FooterSample() }),
    FieldSection("fields", R.string.debug_section_fields, { FieldSample() }),
    PeerSection("peer", R.string.debug_section_peer, { PeerSample() }),
    PagerSection("pager", R.string.debug_section_pager, { PagerSample() }),
    CodeSection("code", R.string.debug_section_code, { CodeSample() }),
    TabsSection("tabs", R.string.debug_section_tabs, { TabsSample() }),
    BottomNavSection("nav", R.string.debug_section_bottom_nav, { BottomNavSample() }),
    TopBarSection("topbar", R.string.debug_section_top_bar, { TopBarSample() }),
    DialogSection("dialog", R.string.debug_section_dialog, { DialogSample() }),
}

@Composable
private fun TextSection() {
    Eyebrow(stringResource(R.string.contracts_eyebrow, "10.80.0.12"))
    Heading(stringResource(R.string.contracts_heading))
    Intro(stringResource(R.string.contracts_intro))
    Title(stringResource(R.string.traffic_http_bodies))
    SectionTitle(
        stringResource(
            R.string.server_summary,
            pluralStringResource(R.plurals.server_online_count, 2, 2),
            pluralStringResource(R.plurals.server_valid_contracts, 1, 1),
        ),
    ) { Hint("9:41") }
    Hint(stringResource(R.string.server_online_hint))
}

@Composable
private fun CardSample() {
    Card {
        MetaLabel(stringResource(R.string.server_card_label))
        Intro(SAMPLE_SERVER_URL)
        InfoRow(stringResource(R.string.row_virtual_ip), "10.80.0.12")
        InfoRow(stringResource(R.string.row_vpn_network), "10.80.0.0/24", divider = true)
    }
    Card(variant = CardVariant.Blue) { Title(stringResource(R.string.welcome_steps_title)) }
}

@Composable
private fun TagSample() {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Tag(stringResource(R.string.tag_connected))
        Tag(stringResource(R.string.tag_disconnected), kind = TagKind.Neutral)
        Tag(stringResource(R.string.tag_connecting), kind = TagKind.Warn)
        Tag(stringResource(R.string.check_state_fail), kind = TagKind.Error)
        Tag(stringResource(R.string.tag_enabled), small = true)
        Tag(stringResource(R.string.tag_disabled), kind = TagKind.Neutral, small = true)
        Tag(stringResource(R.string.tag_online), kind = TagKind.Warn, small = true)
    }
}

@Composable
private fun HeroSample() {
    Hero(
        StateIcon.Connected,
        stringResource(R.string.connected_heading),
        subtitle = stringResource(R.string.connected_subtitle, stringResource(R.string.duration_min_sec, 24, 18)),
        compact = true,
        badge = { Tag(stringResource(R.string.tag_connected)) },
    )
    Hero(
        StateIcon.Connecting,
        stringResource(R.string.connecting_heading),
        subtitle = stringResource(R.string.connecting_subtitle),
        badge = { Tag(stringResource(R.string.tag_connecting), kind = TagKind.Warn) },
    )
    Hero(
        StateIcon.Disconnected,
        stringResource(R.string.disconnected_heading),
        subtitle = stringResource(R.string.disconnected_subtitle),
        badge = { Tag(stringResource(R.string.tag_disconnected), kind = TagKind.Neutral) },
    )
    Hero(
        StateIcon.Brand,
        stringResource(R.string.welcome_heading),
        subtitle = stringResource(R.string.welcome_intro),
        badge = { Eyebrow(stringResource(R.string.welcome_eyebrow)) },
    )
}

@Composable
private fun RowSample() {
    Card {
        InfoRow(stringResource(R.string.welcome_step_1), stringResource(R.string.welcome_step_1_value))
        InfoRow(stringResource(R.string.gate_current_status), divider = true) { Tag(stringResource(R.string.tag_connected_short)) }
        InfoRow(stringResource(R.string.traffic_empty_bytes), stringResource(R.string.value_missing_pair), divider = true)
    }
    Card {
        EntryRow(
            stringResource(R.string.settings_wizard),
            {},
            detail = stringResource(R.string.settings_wizard_detail),
            symbol = { SymbolText(stringResource(R.string.symbol_wizard)) },
        )
        EntryRow(
            stringResource(R.string.entry_communication_contracts),
            {},
            detail = stringResource(R.string.settings_contracts_detail),
            divider = true,
            symbol = { SymbolIcon(painterResource(R.drawable.ic_swap)) },
        )
        EntryRow(
            stringResource(R.string.entry_configuration_editor),
            {},
            detail = stringResource(R.string.settings_editor_detail),
            divider = true,
            symbol = { SymbolText(stringResource(R.string.symbol_editor)) },
        )
    }
    EntryRow(stringResource(R.string.connected_checks_entry), {}, detail = stringResource(R.string.connected_checks_entry_detail))
}

@Composable
private fun MetricSample() {
    MetricGrid(
        listOf(
            Metric(stringResource(R.string.metric_received), "18.42 MB", pluralStringResource(R.plurals.metric_ipv4_packets, 14826, "14,826")),
            Metric(stringResource(R.string.metric_sent), "4.16 MB", pluralStringResource(R.plurals.metric_ipv4_packets, 8204, "8,204")),
        ),
    )
}

@Composable
private fun CheckSample() {
    ConnectionChecks(listOf(CheckState.Done, CheckState.Done, CheckState.Pending, CheckState.Off))
    Card {
        Column { CheckRow(stringResource(R.string.check_server_url), CheckState.Fail) }
    }
}

@Composable
private fun NoticeSample() {
    Notice(stringResource(R.string.wizard_url_notice_text), title = stringResource(R.string.wizard_url_notice_title))
    Notice(stringResource(R.string.contracts_unsaved_text), kind = NoticeKind.Warn, title = pluralStringResource(R.plurals.contracts_unsaved_changes, 2, 2))
    Notice(stringResource(R.string.connection_error_notice_text), kind = NoticeKind.Error, title = stringResource(R.string.connection_error_notice_title))
    Notice("vpn.example.com", kind = NoticeKind.Success, title = stringResource(R.string.wizard_secret_url_verified))
}

@Composable
private fun ActionSample() {
    ActionButton(stringResource(R.string.wizard_verify_continue), {}, style = ActionStyle.Primary)
    ActionButton(stringResource(R.string.action_disconnect_vpn), {})
    ActionButton(stringResource(R.string.action_discard_changes), {}, style = ActionStyle.Danger)
    ActionButton(stringResource(R.string.wizard_cancel_setup), {}, style = ActionStyle.Ghost)
    ActionButton(stringResource(R.string.disconnecting_progress), {}, enabled = false)
    ActionRow {
        ActionButton(stringResource(R.string.password_key_default), {}, Modifier.weight(1f))
        ActionButton(stringResource(R.string.password_key_random), {}, Modifier.weight(1f))
    }
}

@Composable
private fun FooterSample() {
    FooterActions(padNavigationBar = false) {
        ActionButton(pluralStringResource(R.plurals.contracts_save_changes, 2, 2), {}, style = ActionStyle.Primary)
        ActionButton(stringResource(R.string.action_discard_changes), {}, style = ActionStyle.Ghost)
    }
}

@Composable
private fun FieldSample() {
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
    var defaultKey by rememberSaveable { mutableStateOf(false) }
    Card {
        TextInput("10.80.0.12", {}, stringResource(R.string.wizard_identity_ip_field), enabled = !suggest)
        ToggleRow(stringResource(R.string.wizard_identity_suggest_ip), suggest, { suggest = it })
        ToggleRow(stringResource(R.string.wizard_identity_default_key), defaultKey, { defaultKey = it })
    }
    TextInput(
        "{\n  \"masterSetup\": {\n    \"virtualIp\": \"10.80.0.12\"\n  }\n}",
        {},
        stringResource(R.string.editor_field_dirty),
        singleLine = false,
        minLines = 5,
        monospace = true,
    )
}

@Composable
private fun PeerSample() {
    Card {
        PeerRow("10.80.0.2", stringResource(R.string.contracts_peer_unsaved), stringResource(R.string.tag_enabled), {})
        PeerRow("10.80.0.3", stringResource(R.string.contracts_peer_key_configured), stringResource(R.string.tag_enabled), {}, divider = true)
        PeerRow(
            "10.80.0.4",
            stringResource(R.string.contracts_peer_disabled),
            stringResource(R.string.tag_disabled),
            {},
            tagKind = TagKind.Neutral,
            divider = true,
        )
    }
}

@Composable
private fun PagerSample() {
    Pager(
        pluralStringResource(R.plurals.contracts_page_range, 252, 1, 4, 252),
        nextLabel = stringResource(R.string.contracts_page_next),
        onNext = {},
    )
    Pager(
        pluralStringResource(R.plurals.contracts_page_range, 252, 5, 8, 252),
        previousLabel = stringResource(R.string.contracts_page_previous),
        onPrevious = {},
    )
}

@Composable
private fun CodeSample() {
    CodeBlock("HTTP/1.1 503 Service Unavailable\nContent-Type: text/plain\n\nService temporarily unavailable.")
    Card {
        MetaLabel(stringResource(R.string.password_key_preview))
        MonoText("a29f".repeat(16))
    }
}

@Composable
private fun TabsSample() {
    var server by rememberSaveable { mutableIntStateOf(0) }
    Tabs(listOf(stringResource(R.string.server_tab_online), stringResource(R.string.server_tab_raw)), server, { server = it })
    var http by rememberSaveable { mutableIntStateOf(1) }
    Tabs(listOf(stringResource(R.string.http_tab_request), stringResource(R.string.http_tab_response)), http, { http = it })
}

@Composable
private fun BottomNavSample() {
    MainDestination.entries.forEach { BottomNav(it, {}) }
}

@Composable
private fun TopBarSample() {
    MainTopBar()
    TopBar(stringResource(R.string.wizard_url_title), onBack = {})
    TopBar(stringResource(R.string.welcome_title))
}

@Composable
private fun DialogSample() {
    DialogCard(
        stringResource(R.string.discard_heading),
        Modifier.padding(vertical = 8.dp),
        eyebrow = stringResource(R.string.discard_eyebrow),
        paragraphs = listOf(stringResource(R.string.discard_text)),
    ) {
        ActionButton(stringResource(R.string.action_keep_editing), {}, style = ActionStyle.Primary)
        ActionButton(stringResource(R.string.action_discard_changes), {}, style = ActionStyle.Danger)
    }
}

/** Sample data of the design, not a text: the server card shows the configured URL. */
private const val SAMPLE_SERVER_URL = "https://vpn.example.com/phpvpn.php"
