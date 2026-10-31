package phpvpn.android.ui.screens.editor

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import phpvpn.android.bridge.controller.EditorController
import phpvpn.android.bridge.controller.EditorNotice
import phpvpn.android.bridge.controller.EditorSession
import phpvpn.android.bridge.controller.EditorState
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.ui.navigation.SaveOrigin
import phpvpn.android.ui.screens.ConfigFlowDependencies
import phpvpn.android.ui.screens.save.SaveOutcomePhase
import phpvpn.android.ui.screens.save.SaveOutcomeRegistry
import phpvpn.android.ui.screens.save.SaveOutcomeSource
import phpvpn.android.ui.screens.save.SaveOutcomeState

/** One-shot navigation requests of the editor flow. */
sealed interface EditorNavigation {
    /** get-config-document said `missing`: ask 36a with the service's [configPath]. */
    data class Missing(val configPath: String) : EditorNavigation

    /** Confirmed save → 25. */
    data object Saved : EditorNavigation

    /** Unknown save → 33. */
    data object Unknown : EditorNavigation
}

/**
 * State holder of Edit Config on Android (23, 24, 39, 42, 36a and the editor
 * side of 33 and 46) over the WP06 [EditorController]: get-config-document
 * first, `missing` → 36a → create-default-config, Reset → get-default-config
 * into the draft only, Save → set-config with the native classification, an
 * unknown result is checked against the actual document and never retried
 * automatically.
 *
 * It lives as long as the navigation host because 36a is a dialog over 00,
 * 10 and 36 outside the editor graph; [close] wipes the session when the
 * editor is left.
 */
class EditorViewModel(deps: ConfigFlowDependencies) : ViewModel() {
    private val controller = EditorController(deps.ipcClient, deps.classifier, deps.clock)

    val state: StateFlow<EditorState> = controller.state

    /**
     * Text of the editor field. Compose text fields keep a String; it is the
     * same best-effort secret handling as the JSON parser copies (SecretText).
     */
    var text by mutableStateOf("")
        private set

    /** Bumps whenever the session draft changed, so [dirty] is read again. */
    var draftRevision by mutableIntStateOf(0)
        private set

    /** A save the service rejected before publication: screen 39 until "Back to editor". */
    var rejection by mutableStateOf<EditorNotice.Rejected?>(null)
        private set

    /** Result of 36a Yes: true created now, false opened a file that appeared meanwhile. */
    var createdNow by mutableStateOf<Boolean?>(null)
        private set

    /** Result of the last Reset (24): true loaded into the draft (42), false failed. */
    var resetLoaded by mutableStateOf<Boolean?>(null)
        private set

    private val _navigation = MutableStateFlow<EditorNavigation?>(null)

    /** One-shot navigation requests of the editor flow. */
    val navigation: StateFlow<EditorNavigation?> = _navigation.asStateFlow()

    val dirty: Boolean
        get() {
            @Suppress("UNUSED_EXPRESSION")
            draftRevision
            return session()?.dirty ?: false
        }

    /** Opens the editor (from 00, 10, 36, 33): no-op while a session is open. */
    fun open() {
        // A new session starts without a pending save outcome (a confirmed one was shown already).
        if (session() == null) outcome(SaveOutcomePhase.IDLE)
        viewModelScope.launch {
            // A 36a that was dismissed without an answer created nothing; ask again.
            if (controller.state.value is EditorState.ConfirmCreate) controller.answerCreate(false)
            controller.open()
            syncText()
            (controller.state.value as? EditorState.ConfirmCreate)?.let { _navigation.value = EditorNavigation.Missing(it.configPath) }
        }
    }

    /** Answer of 36a. */
    fun answerCreate(yes: Boolean) {
        viewModelScope.launch {
            if (yes && controller.state.value !is EditorState.ConfirmCreate) {
                // 36a reached directly (deep link): confirm that the file is still missing first.
                controller.open()
            }
            controller.answerCreate(yes)
            syncText()
            val opened = controller.state.value as? EditorState.Editing
            if (yes && opened != null) createdNow = opened.session.created
        }
    }

    fun edit(newText: String) {
        if (controller.state.value !is EditorState.Editing) return
        text = newText
        viewModelScope.launch {
            controller.edit(SecretText.of(newText))
            draftRevision++
        }
    }

    /** 24 → 42: the default template replaces only the draft. */
    fun reset() {
        resetLoaded = null
        viewModelScope.launch {
            controller.reset()
            syncText()
            val editing = controller.state.value as? EditorState.Editing
            resetLoaded = editing != null && editing.notice !is EditorNotice.ResetFailed
        }
    }

    fun consumeReset() {
        resetLoaded = null
    }

    fun save() {
        rejection = null
        viewModelScope.launch {
            controller.save()
            afterSave()
        }
    }

    private suspend fun afterSave(repeat: Boolean = false) {
        when (val current = controller.state.value) {
            is EditorState.Saved -> if (repeat) confirmed() else _navigation.value = EditorNavigation.Saved
            is EditorState.SaveUnknown -> {
                outcome(SaveOutcomePhase.UNKNOWN, path = current.session.configPath)
                if (!repeat) _navigation.value = EditorNavigation.Unknown
            }
            is EditorState.Editing -> {
                rejection = current.notice as? EditorNotice.Rejected
                // A repeat from 46 that provably did not write: the draft is back in the editor.
                if (repeat) outcome(SaveOutcomePhase.NOT_SAVED, errorCode = notSavedCode(current.notice), path = current.session.configPath)
            }
            else -> Unit
        }
    }

    /** "Back to editor" on 39. */
    fun dismissRejection() {
        rejection = null
    }

    fun consumeNavigation() {
        _navigation.value = null
    }

    // ------------------------------------------------------------------ 33 and 46 (SaveOutcomeSource)

    private val _saveState = MutableStateFlow(SaveOutcomeState())

    /**
     * The editor side of 33 and 46 (WP09 contract, SaveOutcomeSource.kt),
     * registered for the lifetime of this holder; IDLE while no save outcome
     * is pending. [verify] reads the actual document once and never writes;
     * [repeatSave] writes the kept draft again only after a check found a
     * different or missing file.
     */
    val saveOutcome: SaveOutcomeSource = object : SaveOutcomeSource {
        override val origin: SaveOrigin = SaveOrigin.Editor
        override val state: StateFlow<SaveOutcomeState> = _saveState.asStateFlow()

        override fun verify(): Boolean {
            val unknown = controller.state.value as? EditorState.SaveUnknown ?: return false
            if (_saveState.value.phase != SaveOutcomePhase.UNKNOWN) return false
            outcome(SaveOutcomePhase.CHECKING, path = unknown.session.configPath)
            viewModelScope.launch {
                controller.checkSaveResult()
                when (val current = controller.state.value) {
                    is EditorState.SaveChecked ->
                        if (current.matched) confirmed() else outcome(SaveOutcomePhase.CONFLICT, canRepeat = true, path = current.session.configPath)
                    is EditorState.SaveUnknown -> outcome(
                        SaveOutcomePhase.UNKNOWN,
                        errorCode = (current.notice as? EditorNotice.CheckFailed)?.failure?.code ?: ErrorCode.CONFIG_READ_FAILED.wireName,
                        path = current.session.configPath,
                    )
                    else -> outcome(SaveOutcomePhase.UNKNOWN)
                }
            }
            return true
        }

        override fun repeatSave(): Boolean {
            val checked = controller.state.value as? EditorState.SaveChecked ?: return false
            if (checked.matched || !_saveState.value.canRepeat) return false
            rejection = null
            outcome(SaveOutcomePhase.CONFLICT, canRepeat = false, path = checked.session.configPath)
            viewModelScope.launch {
                controller.repeatSave()
                afterSave(repeat = true)
            }
            return true
        }
    }

    private val unregisterSaveOutcome = SaveOutcomeRegistry.register(saveOutcome)

    private fun outcome(phase: SaveOutcomePhase, canRepeat: Boolean = false, errorCode: String? = null, path: String? = null) {
        _saveState.value = SaveOutcomeState(phase = phase, canRepeat = canRepeat, errorCode = errorCode, configPath = path)
    }

    /** The write is proven (check matched or repeat confirmed): success, the session is wiped, 46 leads on to 25. */
    private suspend fun confirmed() {
        val path = session()?.configPath ?: (controller.state.value as? EditorState.Saved)?.configPath
        controller.close()
        text = ""
        draftRevision++
        outcome(SaveOutcomePhase.CONFIRMED, path = path)
    }

    private fun notSavedCode(notice: EditorNotice?): String = when (notice) {
        is EditorNotice.Rejected -> notice.code.wireName
        EditorNotice.NotSent -> ErrorCode.IPC_UNAVAILABLE.wireName
        else -> ErrorCode.INTERNAL.wireName
    }

    /** Back, Cancel or Discard: wipes the session. A file created through 36a stays. */
    fun close() {
        text = ""
        rejection = null
        createdNow = null
        resetLoaded = null
        outcome(SaveOutcomePhase.IDLE)
        _navigation.value = null
        viewModelScope.launch {
            controller.close()
            draftRevision++
        }
    }

    private fun syncText() {
        session()?.let { session -> text = session.draft.reveal() }
        draftRevision++
    }

    private fun session(): EditorSession? = when (val current = controller.state.value) {
        is EditorState.Editing -> current.session
        is EditorState.Resetting -> current.session
        is EditorState.Saving -> current.session
        is EditorState.SaveUnknown -> current.session
        is EditorState.Checking -> current.session
        is EditorState.SaveChecked -> current.session
        else -> null
    }

    override fun onCleared() {
        unregisterSaveOutcome()
        // viewModelScope is already cancelled; the close itself does not suspend while idle.
        CoroutineScope(Dispatchers.Unconfined).launch { controller.close() }
    }
}
