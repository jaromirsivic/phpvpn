package phpvpn.android.ui.screens.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import kotlinx.coroutines.launch
import phpvpn.android.R
import phpvpn.android.errors.ErrorStrings
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.EntryRow
import phpvpn.android.ui.components.Eyebrow
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Hero
import phpvpn.android.ui.components.Hint
import phpvpn.android.ui.components.InfoRow
import phpvpn.android.ui.components.MetaLabel
import phpvpn.android.ui.components.MonoText
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.ScreenScaffold
import phpvpn.android.ui.components.StateIcon
import phpvpn.android.ui.components.Title
import phpvpn.android.ui.components.TopBar
import phpvpn.android.ui.state.AttentionUiState
import phpvpn.android.ui.state.ConfigStateViewModel
import phpvpn.android.ui.state.EditorEntry

const val ATTENTION_TAG = "attention-36"
const val WELCOME_TAG = "welcome-00"

/** Targets of 00 and 36; 36a (CreateDefaultConfig) and the editor belong to WP10. */
class ConfigNavigation(
    val openWizardUrl: () -> Unit,
    val openEditor: () -> Unit,
    val openCreateDefault: (String) -> Unit,
    val openAttention: () -> Unit,
)

/**
 * 36: the actual classification of `get-config-state`. Repair is offered
 * only for a readable document; a missing file leads to the wizard first and
 * to "create default and edit" through 36a; nothing is overwritten here.
 */
@Composable
fun ConfigAttentionScreen(viewModel: ConfigStateViewModel, navigation: ConfigNavigation, onBack: () -> Unit) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    ScreenScaffold(topBar = { TopBar(stringResource(R.string.attention_title), onBack = onBack) }) {
        Eyebrow(stringResource(R.string.attention_eyebrow), Modifier.testTag(ATTENTION_TAG))
        Heading(stringResource(R.string.attention_heading))
        when (val current = state) {
            AttentionUiState.Loading -> Unit
            is AttentionUiState.Missing -> {
                Notice(stringResource(R.string.attention_missing_text), title = stringResource(R.string.attention_missing_title))
                Card {
                    EntryRow(
                        stringResource(R.string.attention_wizard),
                        navigation.openWizardUrl,
                        detail = stringResource(R.string.attention_wizard_detail),
                    )
                    EntryRow(
                        stringResource(R.string.attention_create_default),
                        { navigation.openCreateDefault(current.configPath) },
                        divider = true,
                    )
                }
                ConfigPath(current.configPath)
            }
            is AttentionUiState.Unreadable -> {
                Notice(
                    stringResource(ErrorStrings.resource(current.message)),
                    kind = NoticeKind.Error,
                    title = stringResource(R.string.attention_unreadable_title),
                )
                Card {
                    EntryRow(
                        stringResource(R.string.attention_defaults),
                        navigation.openWizardUrl,
                        detail = stringResource(R.string.attention_defaults_detail),
                    )
                }
                ConfigPath(current.configPath)
                Hint(stringResource(R.string.attention_intro))
            }
            is AttentionUiState.Invalid -> {
                Notice(
                    current.reason?.let { stringResource(ErrorStrings.resource(it)) } ?: stringResource(R.string.attention_notice_text),
                    kind = NoticeKind.Error,
                    title = stringResource(R.string.attention_notice_title),
                )
                Card {
                    EntryRow(
                        stringResource(R.string.attention_repair),
                        navigation.openEditor,
                        detail = stringResource(R.string.attention_repair_detail),
                    )
                    EntryRow(
                        stringResource(R.string.attention_defaults),
                        navigation.openWizardUrl,
                        detail = stringResource(R.string.attention_defaults_detail),
                        divider = true,
                    )
                }
                ConfigPath(current.configPath)
                Hint(stringResource(R.string.attention_intro))
            }
            is AttentionUiState.Valid -> {
                Notice(
                    stringResource(R.string.attention_valid_text),
                    kind = NoticeKind.Success,
                    title = stringResource(R.string.attention_valid_title),
                )
                ConfigPath(current.configPath)
            }
            is AttentionUiState.Unknown -> {
                Notice(
                    stringResource(ErrorStrings.resource(current.message)),
                    kind = NoticeKind.Error,
                    title = stringResource(R.string.attention_unknown_title),
                )
                ActionButton(stringResource(R.string.wp08_check_again), viewModel::reload, style = ActionStyle.Primary)
            }
        }
    }
}

@Composable
private fun ConfigPath(path: String) {
    Card {
        MetaLabel(stringResource(R.string.attention_path))
        MonoText(path)
    }
}

/**
 * 00: first launch while no configuration exists. Entry to the wizard or the
 * editor; the editor entry reads get-config-state once and goes through 36a
 * when the file is missing. No accounts, no server directory.
 */
@Composable
fun WelcomeScreen(viewModel: ConfigStateViewModel, navigation: ConfigNavigation) {
    val scope = rememberCoroutineScope()
    ScreenScaffold(
        topBar = { TopBar(stringResource(R.string.welcome_title)) },
        footer = {
            ActionButton(stringResource(R.string.welcome_set_up), navigation.openWizardUrl, style = ActionStyle.Primary)
            ActionButton(
                stringResource(R.string.welcome_open_editor),
                {
                    scope.launch {
                        when (val entry = viewModel.editorEntry()) {
                            is EditorEntry.CreateDefault -> navigation.openCreateDefault(entry.configPath)
                            EditorEntry.Editor -> navigation.openEditor()
                            EditorEntry.Attention -> navigation.openAttention()
                        }
                    }
                },
                style = ActionStyle.Ghost,
            )
        },
    ) {
        Hero(
            StateIcon.Brand,
            stringResource(R.string.welcome_heading),
            Modifier.testTag(WELCOME_TAG),
            subtitle = stringResource(R.string.welcome_intro),
            badge = { Eyebrow(stringResource(R.string.welcome_eyebrow)) },
        )
        Card {
            Title(stringResource(R.string.welcome_steps_title))
            InfoRow(stringResource(R.string.welcome_step_1), stringResource(R.string.welcome_step_1_value))
            InfoRow(stringResource(R.string.welcome_step_2), stringResource(R.string.welcome_step_2_value), divider = true)
            InfoRow(stringResource(R.string.welcome_step_3), stringResource(R.string.welcome_step_3_value), divider = true)
            InfoRow(stringResource(R.string.welcome_step_4), stringResource(R.string.welcome_step_4_value), divider = true)
        }
        Hint(stringResource(R.string.welcome_admin_hint))
    }
}
