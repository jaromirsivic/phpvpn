package phpvpn.android.bridge.controller

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.IpcOutcome
import phpvpn.android.bridge.ipc.IpcTimeouts
import phpvpn.android.bridge.ipc.MonotonicClock
import phpvpn.android.bridge.ipc.SecretText

/** A failed configuration action; [configPath] is the authoritative path when the service sent it. */
data class EditorFailure(val code: String, val configPath: String? = null, val timedOut: Boolean = false)

/** Non-blocking notices of the editing state (screen 39 and failed side requests). */
sealed interface EditorNotice {
    /** The service rejected the save before publication; the draft is unchanged (39). */
    data class Rejected(val code: ErrorCode, val validationDescription: String) : EditorNotice

    /** The save request could not be sent; nothing was written. */
    data object NotSent : EditorNotice

    /** get-default-config failed; the draft is unchanged. */
    data class ResetFailed(val failure: EditorFailure) : EditorNotice

    /** The save check could not read the current configuration. */
    data class CheckFailed(val failure: EditorFailure) : EditorNotice
}

/**
 * One open editor document. [draft] is what the user edits; [original] is the
 * text that was loaded. Both are secrets and are wiped when the editor closes.
 */
class EditorSession(
    val configPath: String,
    val revisionToken: String?,
    val availability: ConfigAvailability,
    val original: SecretText,
    draft: SecretText,
    /** True when this session opened the file that create-default-config wrote now. */
    val created: Boolean,
) {
    var draft: SecretText = draft
        internal set

    val dirty: Boolean get() = !draft.contentEquals(original)

    internal fun wipe() = run {
        original.wipe()
        draft.wipe()
    }

    override fun toString(): String = "EditorSession(availability=$availability, created=$created)"
}

/** Editor states (screens 23, 24, 33, 36, 36a, 39, 42, 46, 25 for the saved result). */
sealed interface EditorState {
    data object Closed : EditorState

    data object Loading : EditorState

    /** 36a: `availability == missing`; nothing exists and nothing is written without Yes. */
    data class ConfirmCreate(val configPath: String) : EditorState

    data object Creating : EditorState

    data class Editing(val session: EditorSession, val notice: EditorNotice? = null) : EditorState

    /** get-default-config in flight; the result replaces only the draft (24, 42). */
    data class Resetting(val session: EditorSession) : EditorState

    data class Saving(val session: EditorSession) : EditorState

    /** Confirmed publication (continue to 25). */
    data class Saved(val configPath: String) : EditorState

    /** 33: the outcome is unknown; no automatic retry, editing is frozen. */
    data class SaveUnknown(val session: EditorSession, val notice: EditorNotice? = null) : EditorState

    data class Checking(val session: EditorSession) : EditorState

    /**
     * 46: [matched] means the current file is exactly the submitted draft, a
     * confirmed write. Otherwise the file differs (or is missing): the draft is
     * kept and only an explicit [EditorController.repeatSave] writes again.
     */
    data class SaveChecked(val session: EditorSession, val matched: Boolean) : EditorState

    /** 36: the document cannot be edited (unreadable, encoding_error, too_large). */
    data class Unavailable(val availability: ConfigAvailability, val reasonCode: String?, val configPath: String) : EditorState

    data class Failed(val failure: EditorFailure) : EditorState
}

/**
 * Edit Config of the desktop trays on Android (client.md "Edit Config: skutečný
 * obsah, vytvoření a Reset", tray_application.cpp, editor_window.cpp):
 * get-config-document first; `missing` asks 36a before create-default-config;
 * Reset reads get-default-config into the draft only; Save is set-config with
 * the native save classification; an unknown result is checked against the
 * actual document and a conflict is never overwritten silently.
 */
class EditorController(
    private val client: IpcClient,
    private val classifier: SaveResponseClassifier,
    private val clock: MonotonicClock = MonotonicClock.SYSTEM,
) {
    private val mutex = Mutex()
    private val _state = MutableStateFlow<EditorState>(EditorState.Closed)
    val state: StateFlow<EditorState> = _state.asStateFlow()

    private var windowGeneration = 0L
    private var draftGeneration = 0L
    private var operationGeneration = 0L

    /** Opens the editor from 10, 00 or 36. */
    suspend fun open() = mutex.withLock {
        if (_state.value !is EditorState.Closed && _state.value !is EditorState.Failed &&
            _state.value !is EditorState.Unavailable && _state.value !is EditorState.Saved
        ) return@withLock
        windowGeneration++
        _state.value = EditorState.Loading
        when (val outcome = client.getConfigDocument()) {
            is IpcOutcome.Success -> {
                val result = outcome.value
                val document = result.document
                _state.value = when {
                    result.availability == ConfigAvailability.MISSING -> EditorState.ConfirmCreate(result.configPath)
                    result.availability.readable && document != null -> EditorState.Editing(
                        EditorSession(result.configPath, result.revisionToken, result.availability, document, document.copy(), created = false),
                    )
                    else -> {
                        document?.wipe()
                        EditorState.Unavailable(result.availability, result.reasonCode, result.configPath)
                    }
                }
            }
            is IpcOutcome.ServiceError -> {
                // Unreadable, encoding and size failures are errors with the authoritative path (36).
                val availability = when (outcome.errorCode) {
                    ErrorCode.CONFIG_UNREADABLE -> ConfigAvailability.UNREADABLE
                    ErrorCode.CONFIG_ENCODING_ERROR -> ConfigAvailability.ENCODING_ERROR
                    ErrorCode.CONFIG_TOO_LARGE -> ConfigAvailability.TOO_LARGE
                    else -> null
                }
                val path = outcome.wizard?.configPath
                _state.value = if (availability != null && path != null) {
                    EditorState.Unavailable(availability, outcome.code, path)
                } else {
                    EditorState.Failed(failure(outcome))
                }
            }
            else -> _state.value = EditorState.Failed(failure(outcome))
        }
    }

    /** Answer of dialog 36a. No creates nothing and closes; Yes writes the default file now. */
    suspend fun answerCreate(yes: Boolean) = mutex.withLock {
        if (_state.value !is EditorState.ConfirmCreate) return@withLock
        if (!yes) {
            _state.value = EditorState.Closed
            return@withLock
        }
        _state.value = EditorState.Creating
        when (val outcome = client.createDefaultConfig()) {
            is IpcOutcome.Success -> {
                val result = outcome.value
                // created:false opens the document that appeared meanwhile, never overwritten.
                _state.value = EditorState.Editing(
                    EditorSession(
                        result.configPath, result.revisionToken, result.availability, result.document,
                        result.document.copy(), created = result.created,
                    ),
                )
            }
            else -> _state.value = EditorState.Failed(failure(outcome))
        }
    }

    /** Replaces the draft with the user's text; takes ownership of [text]. */
    suspend fun edit(text: SecretText) = mutex.withLock {
        val editing = _state.value as? EditorState.Editing
        if (editing == null) {
            text.wipe()
            return@withLock
        }
        editing.session.draft.wipe()
        editing.session.draft = text
        draftGeneration++
        _state.value = EditorState.Editing(editing.session)
    }

    /** Reset (24/42): the shared default template replaces the draft only; nothing is written. */
    suspend fun reset() = mutex.withLock {
        val editing = _state.value as? EditorState.Editing ?: return@withLock
        val session = editing.session
        _state.value = EditorState.Resetting(session)
        when (val outcome = client.getDefaultConfig()) {
            is IpcOutcome.Success -> {
                session.draft.wipe()
                session.draft = outcome.value.document
                draftGeneration++
                _state.value = EditorState.Editing(session)
            }
            else -> _state.value = EditorState.Editing(session, EditorNotice.ResetFailed(failure(outcome)))
        }
    }

    /** Save from editing (23) or the explicit repeat after a differing check (46). */
    suspend fun save() = mutex.withLock {
        val session = (_state.value as? EditorState.Editing)?.session ?: return@withLock
        saveLocked(session)
    }

    /** Explicit user decision after 46 found a different file: write the kept draft again. */
    suspend fun repeatSave() = mutex.withLock {
        val checked = _state.value as? EditorState.SaveChecked ?: return@withLock
        if (checked.matched) return@withLock
        saveLocked(checked.session)
    }

    /** 33 → 46: reads the actual document; never writes. */
    suspend fun checkSaveResult() = mutex.withLock {
        val unknown = _state.value as? EditorState.SaveUnknown ?: return@withLock
        val session = unknown.session
        _state.value = EditorState.Checking(session)
        when (val outcome = client.getConfigDocument()) {
            is IpcOutcome.Success -> {
                val result = outcome.value
                val document = result.document
                try {
                    _state.value = when {
                        result.availability.readable && document != null ->
                            EditorState.SaveChecked(session, matched = document.contentEquals(session.draft))
                        result.availability == ConfigAvailability.MISSING -> EditorState.SaveChecked(session, matched = false)
                        else -> EditorState.SaveUnknown(
                            session,
                            EditorNotice.CheckFailed(EditorFailure(result.reasonCode ?: result.availability.wireName, result.configPath)),
                        )
                    }
                } finally {
                    document?.wipe()
                }
            }
            else -> _state.value = EditorState.SaveUnknown(session, EditorNotice.CheckFailed(failure(outcome)))
        }
    }

    /** Back or Cancel: closes locally. A file created through 36a is never deleted. */
    suspend fun close() = mutex.withLock {
        sessionOf(_state.value)?.wipe()
        windowGeneration++
        _state.value = EditorState.Closed
    }

    private suspend fun saveLocked(session: EditorSession) {
        val now = clock.nowMs()
        val operation = SaveOperation(windowGeneration, draftGeneration, ++operationGeneration, now + IpcTimeouts.EDITOR_SAVE_DEADLINE_MS)
        _state.value = EditorState.Saving(session)
        val exchange = client.setConfigRaw(session.draft, IpcTimeouts.EDITOR_SAVE_DEADLINE_MS)
        val result = try {
            classifier.classify(operation, exchange.requestId, exchange.response, exchange.writeAttempted, exchange.completedAtMs)
        } finally {
            exchange.response?.fill(0)
        }
        _state.value = when (result.outcome) {
            SaveOutcome.CONFIRMED -> {
                val path = session.configPath
                session.wipe()
                EditorState.Saved(path)
            }
            SaveOutcome.REJECTED -> EditorState.Editing(session, EditorNotice.Rejected(result.error, result.validationDescription))
            SaveOutcome.NOT_SENT -> EditorState.Editing(session, EditorNotice.NotSent)
            SaveOutcome.UNKNOWN -> EditorState.SaveUnknown(session)
        }
    }

    private fun sessionOf(state: EditorState): EditorSession? = when (state) {
        is EditorState.Editing -> state.session
        is EditorState.Resetting -> state.session
        is EditorState.Saving -> state.session
        is EditorState.SaveUnknown -> state.session
        is EditorState.Checking -> state.session
        is EditorState.SaveChecked -> state.session
        else -> null
    }

    private fun failure(outcome: IpcOutcome<*>): EditorFailure = when (outcome) {
        is IpcOutcome.ServiceError -> EditorFailure(outcome.code, outcome.wizard?.configPath)
        IpcOutcome.TimedOut -> EditorFailure(ErrorCode.TIMEOUT.wireName, timedOut = true)
        is IpcOutcome.Rejected -> EditorFailure(outcome.code.wireName)
        is IpcOutcome.Success -> EditorFailure(ErrorCode.INTERNAL.wireName)
    }
}
