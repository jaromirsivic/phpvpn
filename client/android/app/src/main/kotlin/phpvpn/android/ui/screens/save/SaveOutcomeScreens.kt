package phpvpn.android.ui.screens.save

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import kotlinx.coroutines.launch
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
 * 33 Save result unknown: the draft is kept and frozen; "Verify save" asks the
 * origin's [SaveOutcomeSource] once and never writes. A decided result moves
 * to 46; an undecided check stays here with its reason.
 */
@Composable
fun SaveUnknownScreen(
    source: SaveOutcomeSource?,
    draftChanges: Int?,
    onBack: () -> Unit,
    onInspect: (() -> Unit)?,
    onDecided: (SaveVerification) -> Unit,
) {
    val scope = rememberCoroutineScope()
    var checking by remember { mutableStateOf(false) }
    var undecided by remember { mutableStateOf(false) }
    ScreenScaffold(
        topBar = { TopBar(stringResource(R.string.save_unknown_title), onBack = onBack) },
        footer = {
            ActionButton(
                stringResource(if (checking) R.string.save_unknown_checking else R.string.save_unknown_verify),
                {
                    val owner = source ?: return@ActionButton
                    checking = true
                    scope.launch {
                        val result = owner.verify()
                        checking = false
                        undecided = result is SaveVerification.Unknown
                        if (!undecided) onDecided(result)
                    }
                },
                Modifier.testTag(TAG_VERIFY_SAVE),
                style = ActionStyle.Primary,
                enabled = source != null && !checking,
            )
        },
    ) {
        Eyebrow(stringResource(R.string.save_unknown_eyebrow))
        Heading(stringResource(R.string.save_unknown_heading))
        Intro(stringResource(R.string.save_unknown_intro))
        if (source == null) {
            Notice(stringResource(R.string.save_unknown_nothing), kind = NoticeKind.Info)
            return@ScreenScaffold
        }
        if (undecided) {
            Notice(
                stringResource(R.string.save_unknown_check_failed_text),
                Modifier.testTag(TAG_CHECK_FAILED),
                kind = NoticeKind.Error,
                title = stringResource(R.string.save_unknown_check_failed_title),
            )
        } else {
            Notice(stringResource(R.string.save_unknown_notice_text), kind = NoticeKind.Warn, title = stringResource(R.string.save_unknown_notice_title))
        }
        Card {
            InfoRow(
                stringResource(R.string.save_unknown_draft),
                draftChanges?.toString() ?: stringResource(R.string.save_unknown_draft_value),
            )
            InfoRow(stringResource(R.string.save_unknown_editing), stringResource(R.string.save_unknown_editing_value), divider = true)
        }
        if (onInspect != null) ActionButton(stringResource(R.string.save_unknown_inspect), onInspect)
    }
}

/** Which flow's wording 46 uses. */
enum class SaveCheckedOrigin { Contracts, Editor }

/**
 * 46 Save checked. A confirmed write is a success that leads on to 25, never
 * a retry. A draft that is not the current state is kept and written again
 * only by the explicit "Save draft again": a conflict never overwrites
 * silently.
 */
@Composable
fun SaveCheckedScreen(
    origin: SaveCheckedOrigin,
    verification: SaveVerification?,
    draftChanges: Int?,
    saving: Boolean,
    canSaveAgain: Boolean,
    onBack: () -> Unit,
    onContinue: () -> Unit,
    onSaveAgain: () -> Unit,
    onInspect: (() -> Unit)?,
) {
    val confirmed = verification is SaveVerification.Confirmed
    ScreenScaffold(
        topBar = { TopBar(stringResource(R.string.save_checked_title), onBack = onBack) },
        footer = {
            if (confirmed) {
                ActionButton(stringResource(R.string.save_checked_continue), onContinue, Modifier.testTag(TAG_CHECKED_CONTINUE), style = ActionStyle.Primary)
            } else {
                ActionButton(
                    stringResource(if (saving) R.string.config_saving else R.string.save_checked_again),
                    onSaveAgain,
                    Modifier.testTag(TAG_SAVE_AGAIN),
                    style = ActionStyle.Primary,
                    enabled = canSaveAgain && !saving,
                )
                if (onInspect != null) ActionButton(stringResource(R.string.action_inspect_configuration), onInspect, style = ActionStyle.Ghost)
            }
        },
    ) {
        Eyebrow(stringResource(if (origin == SaveCheckedOrigin.Contracts) R.string.save_checked_eyebrow else R.string.save_checked_eyebrow_editor))
        Heading(stringResource(if (confirmed) R.string.save_checked_confirmed_heading else R.string.save_checked_heading))
        Notice(
            stringResource(if (confirmed) R.string.save_checked_confirmed_text else R.string.save_checked_notice_text),
            Modifier.testTag(TAG_CHECKED_NOTICE),
            kind = if (confirmed) NoticeKind.Success else NoticeKind.Warn,
            title = stringResource(R.string.save_checked_notice_title),
        )
        Card {
            InfoRow(
                stringResource(R.string.save_unknown_draft),
                draftChanges?.toString() ?: stringResource(R.string.save_unknown_draft_value),
            )
            InfoRow(stringResource(R.string.save_checked_check), stringResource(R.string.save_checked_check_value), divider = true)
        }
        if (!confirmed) Intro(stringResource(R.string.save_checked_intro))
    }
}

const val TAG_SAVED_PRIMARY = "saved-primary"
const val TAG_VERIFY_SAVE = "save-verify"
const val TAG_CHECK_FAILED = "save-check-failed"
const val TAG_CHECKED_CONTINUE = "save-checked-continue"
const val TAG_SAVE_AGAIN = "save-again"
const val TAG_CHECKED_NOTICE = "save-checked-notice"
