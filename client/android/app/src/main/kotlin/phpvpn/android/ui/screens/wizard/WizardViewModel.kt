package phpvpn.android.ui.screens.wizard

import androidx.compose.runtime.Immutable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import phpvpn.android.bridge.controller.WizardController
import phpvpn.android.bridge.controller.WizardStep
import phpvpn.android.bridge.controller.WizardUiState
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.CheckpointResult
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.ConnectionPhase
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.HttpDiagnostic
import phpvpn.android.bridge.ipc.IpcTimeouts
import phpvpn.android.bridge.ipc.OperationKind
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.ipc.WizardError
import phpvpn.android.bridge.ipc.WizardMode
import phpvpn.android.bridge.ipc.WizardSession
import phpvpn.android.bridge.ipc.WizardView
import phpvpn.android.ui.navigation.SaveOrigin
import phpvpn.android.ui.screens.save.SaveOutcomePhase
import phpvpn.android.ui.screens.save.SaveOutcomeRegistry
import phpvpn.android.ui.screens.save.SaveOutcomeSource
import phpvpn.android.ui.screens.save.SaveOutcomeState

/** Where the Wizard screens must go after a state change of the session. */
enum class WizardNavTarget { GATE, URL, SECRET, IDENTITY, FINISH, ERROR, EXPIRED, SAVE_UNKNOWN, SAVED }

/** One navigation request; [serial] keeps two equal targets apart. */
data class WizardNavEvent(val target: WizardNavTarget, val serial: Long)

/** What 17 shows after a confirmed Finish (no secrets). */
data class WizardSavedSummary(val serverUrl: String, val virtualIp: String, val vpnCidr: String)

/** Hands the summary from the closing Wizard to 17, whose graph entry outlives the ViewModel. */
object WizardSavedStore {
    @Volatile
    var latest: WizardSavedSummary? = null
}

/**
 * Everything the Wizard screens render. Secrets are [SecretText] (redacted
 * in [toString]); the screens reveal them only for the text field itself.
 */
@Immutable
data class WizardScreenState(
    val step: WizardStep = WizardStep.LOADING,
    val mode: WizardMode? = null,
    val availability: ConfigAvailability? = null,
    val busy: Boolean = false,
    val committing: Boolean = false,
    val commitChecking: Boolean = false,
    val disconnecting: Boolean = false,
    val resetting: Boolean = false,
    val offlineKnown: Boolean = false,
    val vpnState: ConnectionState? = null,
    val vpnPhase: ConnectionPhase? = null,
    val checks: List<CheckpointResult> = List(3) { CheckpointResult.PENDING },
    /** Kind of the open or last attempt: the step an error belongs to. */
    val attemptKind: OperationKind? = null,
    val error: WizardError? = null,
    val localProblem: LocalProblem? = null,
    /** Field highlighted for the current error (serverUrl, serverSecret, virtualIp, accessKey). */
    val invalidField: String? = null,
    val httpDiagnostic: HttpDiagnostic? = null,
    val suggestedIp: String? = null,
    val discardedRecordCount: ULong? = null,
    val dirty: Boolean = false,
    val serverUrl: String = "",
    val httpHeaders: String = "",
    val secretText: SecretText = SecretText.empty(),
    val literalSecret: Boolean = false,
    val useDefaultSecret: Boolean = false,
    val secretIsDefault: Boolean = false,
    val virtualIp: String = "",
    val autoDetectIp: Boolean = false,
    val accessKey: SecretText = SecretText.empty(),
    val useDefaultKey: Boolean = false,
    val vpnCidr: String? = null,
    val createContracts: Boolean = true,
    val contractsAllowed: Boolean = false,
    val existingContracts: ULong = 0uL,
    val missingContracts: Int = 0,
    val contractsToAdd: Int = 0,
) {
    /** The draft may change: no check, save, reset, disconnect or close is running. */
    val editable: Boolean
        get() = !busy && !committing && !resetting && !disconnecting &&
            step in setOf(WizardStep.SERVER_URL, WizardStep.SERVER_SECRET, WizardStep.IDENTITY, WizardStep.CONTRACTS)
}

/**
 * State holder of one Configuration Wizard session (11 to 18, 40, 47 and the
 * Wizard side of 30, 31 and 33), scoped to the Wizard navigation graph.
 *
 * It owns the [WizardController] (IPC workflow: lease and keepalive,
 * attempts, the gate, Finish, the receipt) and the [WizardDraft] (field
 * rules ported from wizard_model.cpp), tracks the dirty flag of the
 * Unsaved changes pattern (32), turns state changes into one navigation
 * request at a time ([navigation]) and implements the Wizard side of the
 * shared save outcome screens ([saveOutcome]).
 *
 * Main thread only. Closing sends one best-effort wizard-close (1 s) on the
 * background scope, because the ViewModel scope ends with the graph.
 */
class WizardViewModel(private val deps: WizardDependencies) : ViewModel() {
    private val controller = WizardController(deps.client, viewModelScope, deps.clock, deps.ids)
    private val draft = WizardDraft()

    private val _state = MutableStateFlow(WizardScreenState())
    val state: StateFlow<WizardScreenState> = _state.asStateFlow()

    private val _navigation = MutableStateFlow<WizardNavEvent?>(null)
    val navigation: StateFlow<WizardNavEvent?> = _navigation.asStateFlow()

    private val _saveState = MutableStateFlow(SaveOutcomeState())

    /** The Wizard side of 33 (see SaveOutcomeSource.kt). */
    val saveOutcome: SaveOutcomeSource = object : SaveOutcomeSource {
        override val origin: SaveOrigin = SaveOrigin.Wizard
        override val state: StateFlow<SaveOutcomeState> = _saveState.asStateFlow()

        override fun verify(): Boolean = controller.verifyCommit().also { sync() }

        // The Wizard never repeats a write from 33/46; Finish on 16 is a new commit.
        override fun repeatSave(): Boolean = false
    }

    private val unregisterSave = SaveOutcomeRegistry.register(saveOutcome)
    private var lastView: WizardView? = null
    private var lastStep = WizardStep.CLOSED
    private var lastFailure: Pair<String, ULong>? = null
    private var lastSuggestion: String? = null
    private var resetFromView: WizardView? = null
    private var resetRequested = false
    private var dirty = false
    private var localProblem: LocalProblem? = null
    private var notSavedAfterUnknown = false
    private var navigationSerial = 0L
    private var closeSent = false
    private var left = false

    init {
        viewModelScope.launch { controller.state.collect { onController(it) } }
        controller.open()
    }

    // ------------------------------------------------------------ field edits

    fun setUrl(text: String) = edit { draft.setUrl(text) }

    fun setSecret(text: String) = edit { draft.setSecretEditor(SecretText.of(text)) }

    fun setDefaultSecret(checked: Boolean) = edit { draft.setDefaultSecret(checked) }

    fun setIp(text: String) = edit { draft.setIp(text) }

    fun setAutoDetectIp(checked: Boolean) = edit { draft.setAutoDetectIp(checked, controller.state.value.suggestedIp) }

    fun setAccessKey(text: String) = edit { draft.setAccessKey(SecretText.of(text)) }

    fun setDefaultKey(checked: Boolean) = edit { draft.setDefaultKey(checked) }

    fun setCreateContracts(checked: Boolean): Boolean {
        if (!currentEditable() || !draft.setCreateContracts(checked)) return false
        changed()
        sync()
        return true
    }

    /**
     * "Use in wizard" on 13: the shared validator accepts the block (canonical
     * CRLF) into the draft; a changed value invalidates every check. A
     * rejected block leaves the draft untouched.
     */
    fun acceptHeaders(text: String): HeaderCheck {
        if (!currentEditable()) return HeaderCheck.Invalid(HeaderProblem.BUSY, 1)
        val before = draft.httpHeaders
        val check = draft.setHttpHeaders(text)
        if (check is HeaderCheck.Valid && before != draft.httpHeaders) {
            changed()
            controller.invalidateFrom(OperationKind.URL)
            sync()
        }
        return check
    }

    /**
     * The text of 13 while it is open (LF line ends). It is not part of the
     * draft until "Use in wizard" accepts it; Cancel drops only this text.
     * Kept here, not in the saved instance state, because headers may hold
     * cookies.
     */
    var headersEdit: String? by mutableStateOf(null)
        private set

    /** The accepted headers as 13 shows them, to compare with [headersEdit]. */
    val headersAccepted: String get() = HeaderRules.displayText(draft.httpHeaders)

    fun updateHeadersEdit(text: String) {
        headersEdit = text
    }

    fun endHeadersEdit() {
        headersEdit = null
    }

    // ------------------------------------------------------------ actions

    /** "Verify and continue" on 12, 14 and 15: local validation, then one new attempt. */
    fun verify(): Boolean {
        val current = controller.state.value
        if (!currentEditable()) return false
        val step = when (current.step) {
            WizardStep.SERVER_URL -> DraftStep.SERVER_URL
            WizardStep.SERVER_SECRET -> DraftStep.SERVER_SECRET
            WizardStep.IDENTITY -> DraftStep.IDENTITY
            else -> return false
        }
        val problem = draft.validate(step, current.suggestedIp)
        if (problem != null) {
            localProblem = problem
            controller.dismissHttpDiagnostic()
            sync()
            return false
        }
        localProblem = null
        val fields = draft.toFields()
        return try {
            controller.verify(fields)
        } finally {
            fields.wipe()
            sync()
        }
    }

    /** "Finish and save" on 16. */
    fun finish(): Boolean {
        if (controller.state.value.step != WizardStep.CONTRACTS || !currentEditable()) return false
        notSavedAfterUnknown = false
        val fields = draft.toFields()
        return try {
            controller.finish(fields, draft.shouldAddDefaultContracts)
        } finally {
            fields.wipe()
            sync()
        }
    }

    /** Back on 14, 15 and 16: the previous step with the draft kept, no network. */
    fun back(): Boolean = controller.back().also { sync() }

    /** 11: one disconnect and one resume, or only the resume when already disconnected. */
    fun disconnectAndContinue(): Boolean = controller.disconnectAndContinue().also { sync() }

    /** 40 "Start over": new defaults from the service; the file changes only after Finish. */
    fun reset(): Boolean {
        // Marked before the call: on the main dispatcher the reply can be applied
        // (and collected) before controller.reset() even returns.
        resetRequested = true
        resetFromView = controller.state.value.view
        if (!controller.reset()) {
            resetRequested = false
            resetFromView = null
            return false
        }
        localProblem = null
        sync()
        return true
    }

    /** "Try again" after a failed wizard-open (12 load error). */
    fun reopen(): Boolean {
        val step = controller.state.value.step
        if (step != WizardStep.LOAD_ERROR || controller.state.value.view != null) return false
        return controller.open().also { sync() }
    }

    /** 33 on 16: the receipt check of the unknown save. */
    fun verifyCommit(): Boolean = saveOutcome.verify()

    /** The HTTP detail was invalidated for good (not merely closed). */
    fun dismissHttpDiagnostic() {
        controller.dismissHttpDiagnostic()
        sync()
    }

    /**
     * Cancel, Discard (32) or Keep connected (11): one best-effort
     * wizard-close and the local end of the session. Refused while a Finish
     * is being committed, because a running publication cannot be cancelled.
     */
    fun leave(): Boolean {
        val current = controller.state.value
        if (current.committing) return false
        closeBestEffort(current)
        left = true
        controller.endSession()
        draft.wipe()
        headersEdit = null
        sync()
        return true
    }

    /** Consumes [event] once; false when another screen already took it. */
    fun consumeNavigation(event: WizardNavEvent): Boolean {
        if (_navigation.value != event) return false
        _navigation.value = null
        return true
    }

    override fun onCleared() {
        unregisterSave()
        val current = controller.state.value
        if (!current.committing) closeBestEffort(current)
        controller.endSession()
        draft.wipe()
        headersEdit = null
    }

    // ------------------------------------------------------------ internals

    private inline fun edit(change: () -> OperationKind?): Boolean {
        if (!currentEditable()) return false
        val kind = change()
        changed()
        if (kind != null) controller.invalidateFrom(kind)
        sync()
        return true
    }

    private fun changed() {
        dirty = true
        localProblem = null
        notSavedAfterUnknown = false
    }

    private fun currentEditable(): Boolean {
        val current = controller.state.value
        return !left && !current.busy && !current.committing && !current.resetting && !current.closing &&
            !current.disconnecting && current.step in EDITABLE_STEPS
    }

    private fun onController(s: WizardUiState) {
        val view = s.view
        if (view != null && view !== lastView) {
            val previous = lastView
            val newSession = previous == null || previous.sessionId != view.sessionId ||
                previous.serviceInstanceId != view.serviceInstanceId
            val reset = resetRequested && !newSession
            draft.load(view, preserveFields = !newSession && !reset, newSession = newSession)
            when {
                newSession -> dirty = false
                reset -> dirty = true
            }
            resetRequested = false
            resetFromView = null
            lastView = view
            lastSuggestion = null
            localProblem = null
        } else if (view == null) {
            lastView = null
        }
        val now = controller.state.value
        if (resetRequested && !now.resetting && now.view === resetFromView) {
            // The reset was refused or failed: the session and the draft stay.
            resetRequested = false
            resetFromView = null
        }

        val suggestion = s.suggestedIp
        if (suggestion == null) {
            lastSuggestion = null
        } else if (suggestion != lastSuggestion && s.checks[OperationKind.SECRET.ordinal] == CheckpointResult.SUCCESS) {
            lastSuggestion = suggestion
            if (draft.applySuggestion(suggestion)) dirty = true
        }

        val attempt = s.attempt
        if (attempt != null && attempt.closed && s.error != null &&
            s.checks[attempt.kind.ordinal] == CheckpointResult.FAILED
        ) {
            val key = (view?.sessionId ?: "") to attempt.sequence
            if (key != lastFailure) {
                lastFailure = key
                navigate(WizardNavTarget.ERROR)
            }
        }

        if (s.step != lastStep) {
            val previous = lastStep
            lastStep = s.step
            onStepChanged(previous, s)
        }
        publish()
    }

    private fun onStepChanged(previous: WizardStep, s: WizardUiState) {
        when (s.step) {
            WizardStep.DISCONNECT_GATE -> navigate(WizardNavTarget.GATE)
            WizardStep.SERVER_URL, WizardStep.LOAD_ERROR -> navigate(WizardNavTarget.URL)
            WizardStep.SERVER_SECRET -> navigate(WizardNavTarget.SECRET)
            WizardStep.IDENTITY -> navigate(WizardNavTarget.IDENTITY)
            WizardStep.CONTRACTS -> {
                // A failed receipt is an authoritative "not saved": back to 16 with the draft.
                if (previous == WizardStep.COMMIT_UNKNOWN) notSavedAfterUnknown = true
                navigate(WizardNavTarget.FINISH)
            }
            WizardStep.SESSION_EXPIRED -> navigate(WizardNavTarget.EXPIRED)
            WizardStep.COMMIT_UNKNOWN -> navigate(WizardNavTarget.SAVE_UNKNOWN)
            WizardStep.FINISHED -> {
                WizardSavedStore.latest = WizardSavedSummary(draft.serverUrl, draft.virtualIp, draft.vpnCidr.orEmpty())
                dirty = false
                navigate(WizardNavTarget.SAVED)
            }
            WizardStep.LOADING, WizardStep.CLOSED -> Unit
        }
    }

    private fun navigate(target: WizardNavTarget) {
        if (left) return
        _navigation.value = WizardNavEvent(target, ++navigationSerial)
    }

    private fun closeBestEffort(s: WizardUiState) {
        if (closeSent) return
        val view = s.view ?: return
        // A confirmed Finish already sent its close; an expired session has nothing to close.
        if (s.step == WizardStep.FINISHED || s.step == WizardStep.SESSION_EXPIRED || s.step == WizardStep.CLOSED) return
        closeSent = true
        val session = WizardSession(view.serviceInstanceId, view.sessionId)
        val client = deps.client
        deps.backgroundScope.launch { client.wizardClose(session, IpcTimeouts.WIZARD_CLOSE_MS) }
    }

    /** Applies the current controller state at once (the collector may still be behind). */
    private fun sync() = onController(controller.state.value)

    private fun publish() {
        val s = controller.state.value
        val view = s.view
        _state.value = WizardScreenState(
            step = s.step,
            mode = view?.mode,
            availability = view?.availability,
            busy = s.busy,
            committing = s.committing,
            commitChecking = s.commitChecking,
            disconnecting = s.disconnecting,
            resetting = s.resetting,
            offlineKnown = s.offlineKnown,
            vpnState = view?.vpnState,
            vpnPhase = view?.vpnPhase,
            checks = s.checks,
            attemptKind = s.attempt?.kind,
            error = s.error,
            localProblem = localProblem,
            invalidField = invalidField(s),
            httpDiagnostic = s.httpDiagnostic,
            suggestedIp = s.suggestedIp,
            discardedRecordCount = s.discardedRecordCount,
            dirty = dirty && !left,
            serverUrl = draft.serverUrl,
            httpHeaders = draft.httpHeaders,
            secretText = draft.secretEditorText,
            literalSecret = draft.literalSecret,
            useDefaultSecret = draft.useDefaultSecret,
            secretIsDefault = draft.secretIsDefault(),
            virtualIp = draft.virtualIp,
            autoDetectIp = draft.autoDetectIp,
            accessKey = draft.accessKeyText,
            useDefaultKey = draft.useDefaultKey,
            vpnCidr = draft.vpnCidr,
            createContracts = draft.createContracts,
            contractsAllowed = draft.contractsAllowed,
            existingContracts = draft.contractCountOriginal,
            missingContracts = draft.missingContractCount,
            contractsToAdd = draft.contractsToAdd,
        )
        _saveState.value = saveState(s)
    }

    /** Presentation follows only errors already evaluated by the workflow (wizard_window.cpp render). */
    private fun invalidField(s: WizardUiState): String? {
        localProblem?.let { return it.field }
        val failure = s.error ?: return null
        val code = failure.code
        return when {
            (code == ErrorCode.CONFIG_INVALID_URL.wireName || code == ErrorCode.SERVER_VERSION_INCOMPATIBLE.wireName) &&
                s.step == WizardStep.SERVER_URL -> FIELD_URL
            code == ErrorCode.CONFIG_INVALID_VALUE.wireName && s.step == WizardStep.SERVER_SECRET -> FIELD_SECRET
            (code == ErrorCode.CONFIG_INVALID_ADDRESS.wireName || code == ErrorCode.WIZARD_CONTRACT_SELF_CONFLICT.wireName) &&
                s.step == WizardStep.IDENTITY -> FIELD_IP
            code == ErrorCode.CONFIG_INVALID_KEY.wireName && s.step == WizardStep.IDENTITY -> FIELD_KEY
            code == ErrorCode.CRYPTO_TAG_MISMATCH.wireName && failure.field == FIELD_SECRET -> FIELD_SECRET
            code == ErrorCode.SERVER_UNAUTHORIZED.wireName && (failure.field == FIELD_SECRET || failure.field == FIELD_KEY) ->
                failure.field
            else -> null
        }
    }

    private fun saveState(s: WizardUiState): SaveOutcomeState = when {
        s.step == WizardStep.COMMIT_UNKNOWN -> SaveOutcomeState(
            phase = if (s.commitChecking) SaveOutcomePhase.CHECKING else SaveOutcomePhase.UNKNOWN,
            errorCode = s.error?.code,
            serviceRestarted = s.restartState != null,
            configAvailability = s.restartState?.availability,
            configPath = s.restartState?.configPath,
        )
        s.step == WizardStep.FINISHED -> SaveOutcomeState(phase = SaveOutcomePhase.CONFIRMED)
        notSavedAfterUnknown && s.step == WizardStep.CONTRACTS ->
            SaveOutcomeState(phase = SaveOutcomePhase.NOT_SAVED, errorCode = s.error?.code)
        else -> SaveOutcomeState()
    }

    companion object {
        const val FIELD_URL = "serverUrl"
        const val FIELD_SECRET = "serverSecret"
        const val FIELD_IP = "virtualIp"
        const val FIELD_KEY = "accessKey"

        private val EDITABLE_STEPS = setOf(WizardStep.SERVER_URL, WizardStep.SERVER_SECRET, WizardStep.IDENTITY, WizardStep.CONTRACTS)
    }
}
