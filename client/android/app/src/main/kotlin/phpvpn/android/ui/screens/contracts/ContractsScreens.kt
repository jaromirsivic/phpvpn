package phpvpn.android.ui.screens.contracts

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import phpvpn.android.R
import phpvpn.android.bridge.controller.ContractsStage
import phpvpn.android.bridge.controller.ContractsUiState
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.Eyebrow
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Hint
import phpvpn.android.ui.components.Intro
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.Pager
import phpvpn.android.ui.components.PeerRow
import phpvpn.android.ui.components.ScreenScaffold
import phpvpn.android.ui.components.SecretField
import phpvpn.android.ui.components.TagKind
import phpvpn.android.ui.components.ToggleRow
import phpvpn.android.ui.components.TopBar
import phpvpn.android.ui.screens.SecureWindow
import phpvpn.android.ui.screens.errorMessage

/** Callbacks of the contracts list (19, 41). */
class ContractsListActions(
    val onBack: () -> Unit,
    val onDiscard: () -> Unit,
    val onOpenPage: (Int) -> Unit,
    val onOpenContract: (String) -> Unit,
    val onVerifySave: () -> Unit,
    val onOpenWizard: () -> Unit,
    val onOpenEditor: () -> Unit,
)

/**
 * 19 Communication contracts and 41 More contracts: every candidate address
 * of `vpnCidr` except the local IP, [CONTRACTS_PAGE_SIZE] per page. The draft
 * belongs to the target IPs in the shared model, so a page switch keeps it.
 */
@Composable
fun ContractsListScreen(vm: ContractsViewModel, ui: ContractsUiState, page: Int, actions: ContractsListActions) {
    // ui.revision changes with every model change, so the model is read again.
    val rows = remember(ui.revision) { vm.rows() }
    val pages = contractsPageCount(rows.size)
    val current = page.coerceIn(1, pages)
    val dirtyCount = rows.count { it.dirty }
    val ready = ContractsViewModel.editing(ui.stage)
    val saving = ui.stage == ContractsStage.SAVING
    ScreenScaffold(
        topBar = { TopBar(stringResource(if (current == 1) R.string.contracts_title else R.string.contracts_more_title), onBack = actions.onBack) },
        footer = if (rows.isEmpty()) null else {
            {
                ActionButton(
                    when {
                        saving -> stringResource(R.string.config_saving)
                        dirtyCount == 0 -> stringResource(R.string.contracts_save_none)
                        else -> pluralStringResource(R.plurals.contracts_save_changes, dirtyCount, dirtyCount)
                    },
                    vm::save,
                    Modifier.testTag(TAG_SAVE),
                    style = ActionStyle.Primary,
                    enabled = ready && dirtyCount > 0,
                )
                if (current == 1) ActionButton(stringResource(R.string.action_discard_changes), actions.onDiscard, style = ActionStyle.Ghost, enabled = !saving)
            }
        },
    ) {
        when {
            ui.stage == ContractsStage.LOADING -> Hint(stringResource(R.string.contracts_loading))
            rows.isEmpty() -> ContractsUnavailable(ui, actions)
            else -> {
                Eyebrow(stringResource(R.string.contracts_eyebrow, vm.localIp))
                Heading(stringResource(R.string.contracts_heading))
                if (current == 1) Intro(stringResource(R.string.contracts_intro))
                val firstBlocking = rows.firstOrNull { it.blocking }
                vm.rejection?.let { problem ->
                    Notice(
                        if (problem.notSent) stringResource(R.string.config_save_not_sent)
                        else errorMessage(problem.code.wireName) + " " + stringResource(R.string.contracts_draft_kept),
                        kind = NoticeKind.Error,
                        title = stringResource(R.string.contracts_rejected_title),
                    )
                }
                when {
                    ui.stage == ContractsStage.SAVE_UNKNOWN -> {
                        Notice(stringResource(R.string.contracts_unknown_text), kind = NoticeKind.Warn, title = stringResource(R.string.save_unknown_notice_title))
                        ActionButton(stringResource(R.string.save_unknown_verify), actions.onVerifySave)
                    }
                    firstBlocking != null -> Notice(
                        stringResource(R.string.contract_invalid_notice_text, firstBlocking.remoteIp),
                        kind = NoticeKind.Error,
                        title = stringResource(R.string.contract_invalid_notice_title),
                    )
                    dirtyCount > 0 -> Notice(
                        stringResource(if (current == 1) R.string.contracts_unsaved_text else R.string.contracts_more_kept),
                        kind = NoticeKind.Warn,
                        title = pluralStringResource(R.plurals.contracts_unsaved_changes, dirtyCount, dirtyCount),
                    )
                }
                val first = (current - 1) * CONTRACTS_PAGE_SIZE
                val visible = rows.subList(first, minOf(first + CONTRACTS_PAGE_SIZE, rows.size))
                Card {
                    visible.forEachIndexed { index, row ->
                        PeerRow(
                            address = row.remoteIp,
                            detail = stringResource(
                                when {
                                    row.blocking -> R.string.contracts_peer_key_invalid
                                    row.dirty -> R.string.contracts_peer_unsaved
                                    row.enabled -> R.string.contracts_peer_key_configured
                                    else -> R.string.contracts_peer_disabled
                                },
                            ),
                            tagText = stringResource(
                                when {
                                    row.blocking -> R.string.contract_invalid_title
                                    row.enabled -> R.string.tag_enabled
                                    else -> R.string.tag_disabled
                                },
                            ),
                            onClick = { actions.onOpenContract(row.remoteIp) },
                            modifier = Modifier.testTag(TAG_ROW_PREFIX + row.remoteIp),
                            tagKind = when {
                                row.blocking -> TagKind.Error
                                row.enabled -> TagKind.Success
                                else -> TagKind.Neutral
                            },
                            divider = index > 0,
                        )
                    }
                }
                Pager(
                    pluralStringResource(R.plurals.contracts_page_range, rows.size, first + 1, first + visible.size, rows.size),
                    previousLabel = stringResource(R.string.contracts_page_previous),
                    onPrevious = if (current > 1) ({ actions.onOpenPage(current - 1) }) else null,
                    nextLabel = stringResource(R.string.contracts_page_next),
                    onNext = if (current < pages) ({ actions.onOpenPage(current + 1) }) else null,
                )
            }
        }
    }
}

/** The configuration is not a valid document, or reading it failed (client.md, Communication contracts). */
@Composable
private fun ContractsUnavailable(ui: ContractsUiState, actions: ContractsListActions) {
    Heading(stringResource(R.string.contracts_unavailable_heading))
    val text = when (ui.unavailable) {
        ConfigAvailability.MISSING -> stringResource(R.string.contracts_unavailable_missing)
        ConfigAvailability.REPAIRABLE -> stringResource(R.string.contracts_unavailable_repairable)
        ConfigAvailability.INVALID -> stringResource(R.string.contracts_unavailable_invalid)
        else -> errorMessage(ui.failureCode ?: ui.unavailable?.let(::availabilityCode) ?: ErrorCode.INTERNAL.wireName)
    }
    Notice(text, kind = NoticeKind.Warn)
    ui.configPath?.let { Hint(stringResource(R.string.config_path_line, it)) }
    when (ui.unavailable) {
        ConfigAvailability.MISSING, ConfigAvailability.REPAIRABLE -> {
            ActionButton(stringResource(R.string.config_action_open_wizard), actions.onOpenWizard, style = ActionStyle.Primary)
            ActionButton(stringResource(R.string.config_action_open_editor), actions.onOpenEditor)
        }
        ConfigAvailability.INVALID -> ActionButton(stringResource(R.string.config_action_open_editor), actions.onOpenEditor, style = ActionStyle.Primary)
        else -> Unit
    }
}

internal fun availabilityCode(availability: ConfigAvailability): String = when (availability) {
    ConfigAvailability.UNREADABLE -> ErrorCode.CONFIG_UNREADABLE.wireName
    ConfigAvailability.ENCODING_ERROR -> ErrorCode.CONFIG_ENCODING_ERROR.wireName
    ConfigAvailability.TOO_LARGE -> ErrorCode.CONFIG_TOO_LARGE.wireName
    ConfigAvailability.MISSING -> ErrorCode.CONFIG_NOT_FOUND.wireName
    else -> ErrorCode.CONFIG_INVALID_VALUE.wireName
}

/**
 * 20 Contract details and 22 Invalid key (same route `contracts/{ip}`). The
 * detail edits the shared draft directly; Back with changes asks 32 and
 * Discard restores the row as it was when the detail opened, keeping every
 * other change. A disabled contract has a read-only key. 22 is shown when the
 * contract blocks saving (enabled with an invalid key).
 */
@Composable
fun ContractDetailScreen(
    vm: ContractsViewModel,
    ui: ContractsUiState,
    remoteIp: String,
    invalidMode: Boolean,
    onBack: () -> Unit,
    onApply: () -> Unit,
    onPassword: () -> Unit,
) {
    SecureWindow()
    val detail = remember(ui.revision, remoteIp) { vm.detail(remoteIp) }
    var keyField by remember(remoteIp) { mutableStateOf(vm.keyText(remoteIp)) }
    var refused by remember(remoteIp) { mutableStateOf(false) }
    // A key applied from 21 changes the model underneath; the field follows it.
    LaunchedEffect(ui.revision, remoteIp) {
        val current = vm.keyText(remoteIp)
        if (current != keyField) keyField = current
    }
    ScreenScaffold(
        topBar = { TopBar(stringResource(if (invalidMode) R.string.contract_invalid_title else R.string.contract_detail_title), onBack = onBack) },
        footer = {
            ActionButton(
                stringResource(if (invalidMode) R.string.contract_invalid_back else R.string.contract_detail_apply),
                onApply,
                Modifier.testTag(TAG_APPLY),
                style = ActionStyle.Primary,
            )
        },
    ) {
        if (detail == null) {
            Hint(stringResource(R.string.contracts_loading))
            return@ScreenScaffold
        }
        if (invalidMode) {
            Eyebrow(stringResource(R.string.password_key_eyebrow, remoteIp))
            Heading(stringResource(R.string.contract_invalid_heading))
            Notice(
                stringResource(R.string.contract_invalid_notice_text, vm.firstBlocking ?: remoteIp),
                kind = NoticeKind.Error,
                title = stringResource(R.string.contract_invalid_notice_title),
            )
        } else {
            Eyebrow(stringResource(R.string.contract_detail_eyebrow, detail.localIp, remoteIp))
            Heading(stringResource(R.string.contract_detail_heading, remoteIp))
            Card {
                ToggleRow(
                    stringResource(R.string.contract_detail_enable),
                    detail.enabled,
                    { vm.setEnabled(remoteIp, it) },
                    Modifier.testTag(TAG_ENABLE),
                    enabled = detail.editable,
                )
            }
        }
        val keyEditable = detail.editable && detail.enabled
        SecretField(
            value = keyField,
            onValueChange = { text ->
                // The model refuses a change over the total key budget; the field keeps the old value.
                refused = !vm.setKey(remoteIp, text)
                if (!refused) keyField = text
            },
            label = stringResource(R.string.contract_detail_key_field),
            modifier = Modifier.testTag(TAG_KEY),
            hint = when {
                refused -> stringResource(R.string.contract_detail_key_rejected)
                // The error replaces the format hint (22), as in the design.
                detail.keyErrorVisible -> null
                !detail.enabled -> stringResource(R.string.contract_detail_key_disabled_hint)
                else -> stringResource(R.string.contract_detail_key_hint)
            },
            error = if (detail.keyErrorVisible) stringResource(R.string.contract_invalid_error, detail.keyLength) else null,
            enabled = keyEditable,
            monospace = true,
        )
        ActionButton(stringResource(R.string.contract_detail_from_password), onPassword, Modifier.testTag(TAG_PASSWORD), enabled = keyEditable)
        if (!invalidMode) {
            Notice(
                stringResource(R.string.contract_detail_notice_text),
                kind = NoticeKind.Info,
                title = stringResource(R.string.contract_detail_notice_title),
            )
            Hint(stringResource(R.string.contract_detail_draft_hint))
        }
    }
}

const val TAG_SAVE = "contracts-save"
const val TAG_ROW_PREFIX = "contract-row-"
const val TAG_APPLY = "contract-apply"
const val TAG_ENABLE = "contract-enable"
const val TAG_KEY = "contract-key"
const val TAG_PASSWORD = "contract-password"
