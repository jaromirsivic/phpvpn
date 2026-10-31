package phpvpn.android.ui.screens.save

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import phpvpn.android.R
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.Eyebrow
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Hero
import phpvpn.android.ui.components.Hint
import phpvpn.android.ui.components.Intro
import phpvpn.android.ui.components.InfoRow
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.ScreenScaffold
import phpvpn.android.ui.components.StateIcon
import phpvpn.android.ui.components.Tag
import phpvpn.android.ui.components.TopBar
import phpvpn.android.ui.navigation.SaveOrigin
import phpvpn.android.ui.screens.errorMessage

/**
 * 25 Changes saved. The save is confirmed; the active VPN keeps its snapshot.
 * Connected: Reconnect (Disconnect completes before the one Connect) or use
 * on the next connection. Not connected: Connect or not now.
 */
@Composable
fun SavedScreen(vm: SavedViewModel, onDone: () -> Unit) {
    val state = vm.connectionState
    val busy = vm.phase == ReconnectPhase.DISCONNECTING
    ScreenScaffold(
        topBar = { TopBar(stringResource(R.string.saved_title), onBack = onDone) },
        footer = {
            when (state) {
                SavedConnection.ACTIVE -> {
                    ActionButton(
                        stringResource(if (busy) R.string.saved_reconnecting else R.string.saved_reconnect),
                        vm::reconnect,
                        Modifier.testTag(TAG_SAVED_PRIMARY),
                        style = ActionStyle.Primary,
                        enabled = !busy,
                    )
                    ActionButton(stringResource(R.string.saved_next_connection), onDone, style = ActionStyle.Ghost, enabled = !busy)
                }
                SavedConnection.INACTIVE -> {
                    ActionButton(stringResource(R.string.action_connect_vpn), vm::connect, Modifier.testTag(TAG_SAVED_PRIMARY), style = ActionStyle.Primary)
                    ActionButton(stringResource(R.string.saved_later), onDone, style = ActionStyle.Ghost)
                }
                else -> ActionButton(stringResource(R.string.saved_back_to_connection), onDone, Modifier.testTag(TAG_SAVED_PRIMARY), style = ActionStyle.Primary)
            }
        },
    ) {
        Hero(
            StateIcon.Connected,
            stringResource(R.string.saved_heading),
            subtitle = when (state) {
                SavedConnection.ACTIVE -> stringResource(R.string.saved_subtitle)
                SavedConnection.INACTIVE -> stringResource(R.string.saved_connect_subtitle)
                else -> null
            },
            compact = true,
            badge = { Tag(stringResource(R.string.tag_saved)) },
        )
        when (state) {
            SavedConnection.CHECKING -> Hint(stringResource(R.string.saved_checking))
            SavedConnection.ACTIVE -> Notice(stringResource(R.string.saved_notice_text), kind = NoticeKind.Warn, title = stringResource(R.string.saved_notice_title))
            SavedConnection.INACTIVE -> Notice(stringResource(R.string.saved_connect_notice_text), kind = NoticeKind.Info, title = stringResource(R.string.saved_connect_notice_title))
            SavedConnection.BUSY -> Notice(stringResource(R.string.saved_busy_text), kind = NoticeKind.Info)
            SavedConnection.UNAVAILABLE -> Notice(stringResource(R.string.saved_status_unavailable), kind = NoticeKind.Warn)
        }
        if (vm.phase == ReconnectPhase.FAILED) Notice(stringResource(R.string.saved_reconnect_failed), kind = NoticeKind.Error)
        Card {
            InfoRow(stringResource(R.string.saved_status), stringResource(R.string.saved_status_confirmed))
            if (state == SavedConnection.ACTIVE) {
                InfoRow(stringResource(R.string.saved_active_connection), stringResource(R.string.saved_previous_configuration), divider = true)
            } else if (state == SavedConnection.INACTIVE) {
                InfoRow(stringResource(R.string.saved_active_connection), stringResource(R.string.saved_not_connected), divider = true)
            }
        }
    }
}

/**
 * The shared outcome of an uncertain save for every [origin] (WP09
 * contract, SaveOutcomeSource.kt). It renders the phase, not the route:
 * UNKNOWN and CHECKING are 33 Save result unknown, NOT_SAVED and CONFLICT
 * are 46 Save checked, CONFIRMED is the short success before the caller's
 * [phpvpn.android.ui.screens.save.followSaveOutcome] leaves (25, or 17 for
 * the Wizard). [source] null means nothing can be verified here (process
 * recreated, origin closed): no check, no claim, only the inspection.
 */
@Composable
fun SaveOutcomeScreen(
    origin: SaveOrigin,
    source: SaveOutcomeSource?,
    state: SaveOutcomeState,
    onBack: () -> Unit,
    onInspect: (() -> Unit)?,
) {
    when (state.phase) {
        SaveOutcomePhase.NOT_SAVED, SaveOutcomePhase.CONFLICT, SaveOutcomePhase.CONFIRMED ->
            SaveCheckedContent(origin, source, state, onBack, onInspect)
        else -> SaveUnknownContent(source, state, onBack, onInspect)
    }
}

/** 33: the draft is kept and frozen; "Verify save" runs the origin's one check and never writes. */
@Composable
private fun SaveUnknownContent(source: SaveOutcomeSource?, state: SaveOutcomeState, onBack: () -> Unit, onInspect: (() -> Unit)?) {
    val checking = state.phase == SaveOutcomePhase.CHECKING
    ScreenScaffold(
        topBar = { TopBar(stringResource(R.string.save_unknown_title), onBack = onBack) },
        footer = {
            ActionButton(
                stringResource(if (checking) R.string.save_unknown_checking else R.string.save_unknown_verify),
                { source?.verify() },
                Modifier.testTag(TAG_VERIFY_SAVE),
                style = ActionStyle.Primary,
                enabled = source != null && state.phase == SaveOutcomePhase.UNKNOWN,
            )
        },
    ) {
        Eyebrow(stringResource(R.string.save_unknown_eyebrow))
        Heading(stringResource(R.string.save_unknown_heading))
        Intro(stringResource(R.string.save_unknown_intro))
        when {
            source == null || state.phase == SaveOutcomePhase.IDLE ->
                Notice(stringResource(R.string.save_unknown_nothing), kind = NoticeKind.Info)
            state.serviceRestarted -> Notice(
                stringResource(R.string.wp09_finish_restarted),
                kind = NoticeKind.Warn,
                title = stringResource(R.string.save_unknown_notice_title),
            )
            state.errorCode != null -> Notice(
                errorMessage(state.errorCode) + " " + stringResource(R.string.save_unknown_check_failed_text),
                Modifier.testTag(TAG_CHECK_FAILED),
                kind = NoticeKind.Error,
                title = stringResource(R.string.save_unknown_check_failed_title),
            )
            else -> Notice(stringResource(R.string.save_unknown_notice_text), kind = NoticeKind.Warn, title = stringResource(R.string.save_unknown_notice_title))
        }
        state.configPath?.let { if (state.serviceRestarted) Hint(stringResource(R.string.config_path_line, it)) }
        Card {
            InfoRow(
                stringResource(R.string.save_unknown_draft),
                state.draftChanges?.toString() ?: stringResource(R.string.save_unknown_draft_value),
            )
            InfoRow(stringResource(R.string.save_unknown_editing), stringResource(R.string.save_unknown_editing_value), divider = true)
        }
        if (onInspect != null) ActionButton(stringResource(R.string.save_unknown_inspect), onInspect)
    }
}

/**
 * 46: a confirmed write is success (CONFIRMED, leaving for 25), never Retry.
 * NOT_SAVED: the write provably did not happen, the draft is kept with the
 * reason. CONFLICT: the current configuration is not the draft; "Save draft
 * again" writes only on this explicit choice and only while canRepeat.
 */
@Composable
private fun SaveCheckedContent(
    origin: SaveOrigin,
    source: SaveOutcomeSource?,
    state: SaveOutcomeState,
    onBack: () -> Unit,
    onInspect: (() -> Unit)?,
) {
    val confirmed = state.phase == SaveOutcomePhase.CONFIRMED
    ScreenScaffold(
        topBar = { TopBar(stringResource(R.string.save_checked_title), onBack = onBack) },
        footer = if (confirmed) null else {
            {
                ActionButton(
                    stringResource(if (state.phase == SaveOutcomePhase.CONFLICT && !state.canRepeat) R.string.config_saving else R.string.save_checked_again),
                    { source?.repeatSave() },
                    Modifier.testTag(TAG_SAVE_AGAIN),
                    style = ActionStyle.Primary,
                    enabled = source != null && state.canRepeat,
                )
                if (onInspect != null) ActionButton(stringResource(R.string.action_inspect_configuration), onInspect, style = ActionStyle.Ghost)
            }
        },
    ) {
        Eyebrow(
            stringResource(
                when (origin) {
                    SaveOrigin.Contracts -> R.string.save_checked_eyebrow
                    SaveOrigin.Editor -> R.string.save_checked_eyebrow_editor
                    SaveOrigin.Wizard -> R.string.wizard_eyebrow
                },
            ),
        )
        Heading(stringResource(if (confirmed) R.string.save_checked_confirmed_heading else R.string.save_checked_heading))
        Notice(
            when {
                confirmed -> stringResource(R.string.save_checked_confirmed_text)
                state.phase == SaveOutcomePhase.NOT_SAVED && state.errorCode != null ->
                    errorMessage(state.errorCode) + " " + stringResource(R.string.contracts_draft_kept)
                else -> stringResource(R.string.save_checked_notice_text)
            },
            Modifier.testTag(TAG_CHECKED_NOTICE),
            kind = when (state.phase) {
                SaveOutcomePhase.CONFIRMED -> NoticeKind.Success
                SaveOutcomePhase.NOT_SAVED -> NoticeKind.Error
                else -> NoticeKind.Warn
            },
            title = stringResource(if (state.phase == SaveOutcomePhase.NOT_SAVED) R.string.contracts_rejected_title else R.string.save_checked_notice_title),
        )
        Card {
            InfoRow(
                stringResource(R.string.save_unknown_draft),
                state.draftChanges?.toString() ?: stringResource(R.string.save_unknown_draft_value),
            )
            InfoRow(stringResource(R.string.save_checked_check), stringResource(R.string.save_checked_check_value), divider = true)
        }
        if (state.phase == SaveOutcomePhase.CONFLICT) Intro(stringResource(R.string.save_checked_intro))
    }
}

const val TAG_SAVED_PRIMARY = "saved-primary"
const val TAG_VERIFY_SAVE = "save-verify"
const val TAG_CHECK_FAILED = "save-check-failed"
const val TAG_SAVE_AGAIN = "save-again"
const val TAG_CHECKED_NOTICE = "save-checked-notice"
