package phpvpn.android.bridge.controller

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.AccessPayload
import phpvpn.android.bridge.ipc.CheckpointResult
import phpvpn.android.bridge.ipc.CommitState
import phpvpn.android.bridge.ipc.ConfigStateResult
import phpvpn.android.bridge.ipc.ConnectionPhase
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.HttpDiagnostic
import phpvpn.android.bridge.ipc.IdSource
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.IpcOutcome
import phpvpn.android.bridge.ipc.IpcTimeouts
import phpvpn.android.bridge.ipc.MAX_HTTP_TIMEOUT_SECONDS
import phpvpn.android.bridge.ipc.MonotonicClock
import phpvpn.android.bridge.ipc.OperationKind
import phpvpn.android.bridge.ipc.OperationResult
import phpvpn.android.bridge.ipc.OperationState
import phpvpn.android.bridge.ipc.Receipt
import phpvpn.android.bridge.ipc.SafeError
import phpvpn.android.bridge.ipc.SecretPayload
import phpvpn.android.bridge.ipc.WizardError
import phpvpn.android.bridge.ipc.WizardErrors
import phpvpn.android.bridge.ipc.WizardFields
import phpvpn.android.bridge.ipc.WizardSession
import phpvpn.android.bridge.ipc.WizardView

/** Wizard steps, `WizardStep` of core/wizard/wizard_model.h plus [CLOSED]. */
enum class WizardStep {
    CLOSED, LOADING, DISCONNECT_GATE, SERVER_URL, SERVER_SECRET, IDENTITY, CONTRACTS, LOAD_ERROR,
    SESSION_EXPIRED, COMMIT_UNKNOWN, FINISHED,
}

/** One manual Next (`WizardAttempt`). */
data class WizardAttempt(
    val clientAttemptId: String,
    val operationId: String?,
    val sequence: ULong,
    val kind: OperationKind,
    val deadlineMs: Long,
    val closed: Boolean,
)

/**
 * Everything the Wizard screens (11 to 18, 30, 31, 33, 40, 47) render. [view]
 * carries the service draft including secrets; [error] is the structured
 * error whose code selects the resource text (never its description).
 */
data class WizardUiState(
    val step: WizardStep = WizardStep.CLOSED,
    val view: WizardView? = null,
    val checks: List<CheckpointResult> = List(3) { CheckpointResult.PENDING },
    val busy: Boolean = false,
    val committing: Boolean = false,
    val disconnecting: Boolean = false,
    val closing: Boolean = false,
    val resetting: Boolean = false,
    val offlineKnown: Boolean = false,
    val error: WizardError? = null,
    val httpDiagnostic: HttpDiagnostic? = null,
    val suggestedIp: String? = null,
    val discardedRecordCount: ULong? = null,
    val attempt: WizardAttempt? = null,
    val receipt: Receipt? = null,
    /** Commit unknown after a service restart: the one config-state read (no write repeated). */
    val restartState: ConfigStateResult? = null,
    /** The explicit receipt check of 33 (wizard-get-commit) is running. */
    val commitChecking: Boolean = false,
)

/**
 * The IPC workflow of the Configuration Wizard, the counterpart of
 * apps/tray-linux/wizard_controller.cpp (itself the portable copy of the
 * Windows wizard_window.cpp): session lease with wizard-keepalive every 15 s
 * and local expiry after 60 s without contact (→ 47), one manual attempt per
 * Next with its own budget and 500 ms operation polling, cancel-attempt when an
 * attempt or a failed HTTP detail is invalidated, the disconnect gate (11) with
 * one disconnect and one resume, Finish with a commitId and the receipt, and an
 * unknown commit (→ 33) verified only by wizard-get-commit, never by a second
 * write. Field editing and the default key/IP rules stay in the UI model (WP09).
 *
 * Threading: call from one thread; [scope] must use a single-threaded
 * dispatcher (Dispatchers.Main in the app, the test dispatcher in tests).
 */
class WizardController(
    private val client: IpcClient,
    private val scope: CoroutineScope,
    private val clock: MonotonicClock = MonotonicClock.SYSTEM,
    private val ids: IdSource = IdSource.RANDOM,
) {
    private val _state = MutableStateFlow(WizardUiState())
    val state: StateFlow<WizardUiState> = _state.asStateFlow()

    private var view: WizardView? = null
    private var session: WizardSession? = null
    private var step = WizardStep.CLOSED
    private val checks = Array(3) { CheckpointResult.PENDING }
    private var busy = false
    private var committing = false
    private var disconnecting = false
    private var closing = false
    private var resetting = false
    private var offlineKnown = false
    private var error: WizardError? = null
    private var diagnostic: HttpDiagnostic? = null
    private var diagnosticAttempt: WizardAttempt? = null
    private var suggestedIp: String? = null
    private var discardedRecordCount: ULong? = null
    private var attempt: WizardAttempt? = null
    private var sequence = 0uL
    private var receipt: Receipt? = null
    private var restartState: ConfigStateResult? = null
    private var commitChecking = false
    private var commitId: String? = null
    private var commitOperation: String? = null
    private var commitDeadline = 0L
    private var lastContact = 0L
    private var lastHeartbeat = 0L
    private var lastPoll = 0L
    private var generation = 0L
    private var primaryBusy = false
    private var controlBusy = false
    private var heartbeatBusy = false
    private var statusBusy = false
    private var ticker: Job? = null

    private val locked: Boolean get() = busy || primaryBusy || controlBusy || closing || resetting || disconnecting

    // ------------------------------------------------------------ user actions

    /** Opens a new session (also "Open the wizard again" after 47 or a load error). */
    fun open(): Boolean {
        if (step != WizardStep.CLOSED && step != WizardStep.SESSION_EXPIRED && step != WizardStep.LOAD_ERROR) return false
        if (primaryBusy) return false
        resetSessionState()
        step = WizardStep.LOADING
        startTicker()
        publish()
        return primary { handleView(client.wizardOpen(), preserve = false, reset = false) }
    }

    /**
     * Next on the URL, secret or identity step with the UI draft. The request
     * uses a copy that is wiped after sending; [draft] stays owned by the caller.
     */
    fun verify(draft: WizardFields): Boolean {
        val current = session ?: return false
        val context = view?.context ?: return false
        if (locked || committing) return false
        val kind = when (step) {
            WizardStep.SERVER_URL -> OperationKind.URL
            WizardStep.SERVER_SECRET -> OperationKind.SECRET
            WizardStep.IDENTITY -> OperationKind.ACCESS
            else -> return false
        }
        clearDiagnostic()
        val seconds = context.responseTimeoutSeconds
        if (seconds < 1 || seconds > MAX_HTTP_TIMEOUT_SECONDS) {
            fail(WizardErrors.local(ErrorCode.CONFIG_INVALID_LIMIT))
            return false
        }
        val budget = (kind.ordinal + 1) * seconds * 1000 + 15_000
        val now = clock.nowMs()
        invalidate(kind.ordinal)
        val next = WizardAttempt(ids.next(), null, ++sequence, kind, now + budget, false)
        attempt = next
        busy = true
        error = null
        publish()
        val copy = draft.deepCopy()
        return primary {
            val outcome = try {
                when (kind) {
                    OperationKind.URL -> client.wizardCheckUrl(current, next.clientAttemptId, next.sequence, copy.serverUrl, copy.httpHeaders)
                    OperationKind.SECRET -> client.wizardCheckSecret(
                        current, next.clientAttemptId, next.sequence, copy.serverUrl, copy.serverSecret, copy.httpHeaders,
                    )
                    else -> client.wizardCheckAccess(current, next.clientAttemptId, next.sequence, copy)
                }
            } finally {
                copy.wipe()
            }
            when (outcome) {
                is IpcOutcome.Success -> {
                    contact()
                    val open = attempt
                    if (open != null && !open.closed && open.clientAttemptId == next.clientAttemptId && open.sequence == next.sequence) {
                        attempt = open.copy(operationId = outcome.value.operationId)
                    }
                }
                else -> {
                    if (attempt?.closed == false) cancelAttempt(attempt!!)
                    fail(errorOf(outcome))
                }
            }
        }
    }

    /** Finish on the contracts step (16). The draft copy is wiped after sending. */
    fun finish(draft: WizardFields, createDefaultContracts: Boolean): Boolean {
        val current = session ?: return false
        if (step != WizardStep.CONTRACTS || locked || committing) return false
        val id = ids.next()
        commitId = id
        commitOperation = null
        committing = true
        busy = true
        error = null
        commitDeadline = clock.nowMs() + IpcTimeouts.WIZARD_COMMIT_BUDGET_MS
        publish()
        val copy = draft.deepCopy()
        return primary {
            val outcome = try {
                client.wizardFinish(current, id, copy, createDefaultContracts)
            } finally {
                copy.wipe()
            }
            when (outcome) {
                is IpcOutcome.Success -> {
                    contact()
                    commitOperation = outcome.value.operationId
                }
                is IpcOutcome.ServiceError -> {
                    val failure = outcome.wizard ?: errorOf(outcome)
                    if (failure.code != ErrorCode.WIZARD_COMMIT_UNKNOWN.wireName) commitFailed(failure)
                    else commitUnknown(failure)
                }
                else -> commitUnknown(errorOf(outcome))
            }
        }
    }

    /** 33: verifies the existing receipt; never repeats the write. */
    fun verifyCommit(): Boolean {
        val current = session ?: return false
        val id = commitId ?: return false
        if (step != WizardStep.COMMIT_UNKNOWN || primaryBusy) return false
        commitChecking = true
        publish()
        val owner = generation
        return primary {
            val outcome = client.wizardGetCommit(current, id)
            // Explicit receipt verification discovered a restarted service.
            // One fresh state read informs the user, never repeats publication.
            val restarted = outcome is IpcOutcome.ServiceError && outcome.code == ErrorCode.WIZARD_SESSION_EXPIRED.wireName
            val restart = if (restarted) (client.getConfigState() as? IpcOutcome.Success)?.value else null
            if (owner != generation) return@primary
            commitChecking = false
            when (outcome) {
                is IpcOutcome.Success -> {
                    val value = outcome.value
                    when (value.commitState) {
                        CommitState.CONFIRMED -> value.receipt?.let { confirm(it) }
                        CommitState.FAILED -> value.error?.let { commitFailed(it) }
                        else -> commitUnknown(null)
                    }
                }
                is IpcOutcome.ServiceError -> {
                    if (restarted) {
                        restart?.let { restartState = it }
                        commitUnknown(outcome.wizard)
                    } else {
                        commitUnknown(outcome.wizard ?: errorOf(outcome))
                    }
                }
                else -> commitUnknown(errorOf(outcome))
            }
        }
    }

    /**
     * Gate 11 "Disconnect & continue": one global disconnect, then exactly one
     * resume once disconnected and idle are confirmed. A known offline client
     * only resumes.
     */
    fun disconnectAndContinue(): Boolean {
        if (step != WizardStep.DISCONNECT_GATE || locked) return false
        if (offlineKnown) return resume()
        disconnecting = true
        publish()
        val owner = generation
        scope.launch {
            val outcome = client.disconnect()
            if (owner != generation) return@launch
            when (outcome) {
                is IpcOutcome.Success -> if (outcome.value.state == ConnectionState.DISCONNECTED &&
                    outcome.value.phase == ConnectionPhase.IDLE
                ) {
                    disconnecting = false
                    offlineKnown = true
                    resume()
                }
                else -> {
                    disconnecting = false
                    fail(WizardErrors.local(ErrorCode.IPC_UNAVAILABLE))
                }
            }
            publish()
        }
        return true
    }

    /** Back on 14, 15 and 16. */
    fun back(): Boolean {
        if (locked) return false
        step = when (step) {
            WizardStep.SERVER_SECRET -> WizardStep.SERVER_URL
            WizardStep.IDENTITY -> WizardStep.SERVER_SECRET
            WizardStep.CONTRACTS -> WizardStep.IDENTITY
            else -> return false
        }
        clearDiagnostic()
        // WizardModel::back resets the error: it belonged to the step left behind.
        error = null
        publish()
        return true
    }

    /**
     * The user changed a value checked by [kind] (or the HTTP headers, kind URL):
     * the evidence from that check on is invalid and the step returns to it.
     */
    fun invalidateFrom(kind: OperationKind): Boolean {
        if (busy || committing || kind == OperationKind.FINISH) return false
        clearDiagnostic()
        invalidate(kind.ordinal)
        val target = stepFor(kind)
        if (step.ordinal > target.ordinal && step.ordinal <= WizardStep.CONTRACTS.ordinal) step = target
        error = null
        publish()
        return true
    }

    /** The HTTP detail window was invalidated by the user (30/31 closed for good). */
    fun dismissHttpDiagnostic() {
        clearDiagnostic()
        publish()
    }

    /** Reset (40): a new draft with defaults; the file changes only after Finish. */
    fun reset(): Boolean {
        if (session == null || committing || step == WizardStep.COMMIT_UNKNOWN || controlBusy || closing || resetting) return false
        resetting = true
        // A failed HTTP detail is invalidated with its one cancel-attempt before
        // the reset (wizard_window.cpp cancel); the reset then follows on the
        // control channel, at the latest with the next 500 ms tick.
        clearDiagnostic()
        closeOpenAttempt()
        publish()
        sendCloseOrReset()
        return true
    }

    /** Cancel/X: one wizard-close (1 s budget) and local destruction. Refused while committing. */
    fun close(): Boolean {
        if (committing) return false
        if (session == null) {
            destroy()
            return true
        }
        if (closing) return true
        closing = true
        closeOpenAttempt()
        publish()
        sendCloseOrReset()
        return true
    }

    /** Local end without IPC (approved discard): the service lease cleans up. */
    fun endSession() = destroy()

    // ------------------------------------------------------------ timer

    private fun startTicker() {
        if (ticker?.isActive == true) return
        ticker = scope.launch {
            while (isActive) {
                delay(TICK_MS)
                tick()
            }
        }
    }

    internal fun tick() {
        if (step == WizardStep.CLOSED || step == WizardStep.FINISHED) return
        val now = clock.nowMs()
        if (committing && now >= commitDeadline) commitUnknown(null)
        val live = session != null && step != WizardStep.SESSION_EXPIRED && step != WizardStep.COMMIT_UNKNOWN
        if (live && now - lastContact >= IpcTimeouts.WIZARD_LEASE_MS) {
            if (committing) commitUnknown(null) else sessionExpired()
        }
        if (session != null && step != WizardStep.SESSION_EXPIRED && step != WizardStep.COMMIT_UNKNOWN && !heartbeatBusy &&
            now - lastHeartbeat >= IpcTimeouts.WIZARD_KEEPALIVE_MS
        ) {
            lastHeartbeat = now
            keepalive()
        }
        attempt?.let { open ->
            if (!open.closed && now >= open.deadlineMs) {
                fail(WizardErrors.local(ErrorCode.TIMEOUT))
                cancelAttempt(open)
            }
        }
        publish()
        if (now - lastPoll < IpcTimeouts.WIZARD_OPERATION_POLL_MS) return
        lastPoll = now
        when {
            closing || resetting -> sendCloseOrReset()
            disconnecting || step == WizardStep.DISCONNECT_GATE -> readGateStatus()
            !primaryBusy && committing && commitOperation != null -> pollOperation(commitOperation!!)
            !primaryBusy && busy && attempt?.operationId != null && attempt?.closed == false -> pollOperation(attempt!!.operationId!!)
        }
    }

    // ------------------------------------------------------------ IPC handling

    private fun primary(block: suspend () -> Unit): Boolean {
        if (primaryBusy) return false
        primaryBusy = true
        val owner = generation
        scope.launch {
            try {
                block()
            } finally {
                if (owner == generation) primaryBusy = false
                publish()
            }
        }
        return true
    }

    private fun control(block: suspend () -> Unit): Boolean {
        if (controlBusy) return false
        controlBusy = true
        val owner = generation
        scope.launch {
            try {
                block()
            } finally {
                if (owner == generation) controlBusy = false
                publish()
            }
        }
        return true
    }

    private fun keepalive() {
        val current = session ?: return
        heartbeatBusy = true
        val owner = generation
        scope.launch {
            val outcome = client.wizardKeepalive(current)
            if (owner != generation) return@launch
            heartbeatBusy = false
            when (outcome) {
                is IpcOutcome.Success -> contact()
                is IpcOutcome.ServiceError -> if (outcome.code == ErrorCode.WIZARD_SESSION_EXPIRED.wireName ||
                    outcome.code == ErrorCode.WIZARD_CONNECTION_INTERRUPTED.wireName
                ) fail(errorOf(outcome))
                else -> Unit // A lost keepalive is covered by the lease; not an error.
            }
            publish()
        }
    }

    private fun pollOperation(operationId: String) {
        val current = session ?: return
        primary {
            val outcome = client.wizardGetOperation(current, operationId)
            if (outcome !is IpcOutcome.Success) {
                if (committing) {
                    commitUnknown(errorOf(outcome))
                } else {
                    attempt?.takeIf { !it.closed }?.let { cancelAttempt(it) }
                    fail(errorOf(outcome))
                }
                return@primary
            }
            contact()
            val operation = outcome.value
            if (operation.kind == OperationKind.FINISH) applyFinish(operation) else applyOperation(operation)
        }
    }

    private fun applyFinish(operation: OperationResult) {
        if (operation.operationId != commitOperation) {
            fail(WizardErrors.local(ErrorCode.IPC_MALFORMED))
            return
        }
        val payload = operation.payload
        if (operation.operationState == OperationState.SUCCEEDED && payload is Receipt) {
            confirm(payload)
            return
        }
        if (operation.operationState.terminal) {
            val failure = operation.error
            if (failure?.code == ErrorCode.WIZARD_COMMIT_UNKNOWN.wireName) commitUnknown(failure)
            else commitFailed(failure ?: WizardErrors.local(ErrorCode.CONFIG_WRITE_FAILED))
        }
    }

    /** Port of `WizardModel::applyOperation`. */
    private fun applyOperation(operation: OperationResult) {
        val open = attempt ?: return
        if (open.closed || operation.operationId != open.operationId || operation.kind != open.kind ||
            operation.clientAttemptId != open.clientAttemptId || operation.attemptSequence != open.sequence
        ) return
        val now = clock.nowMs()
        if (now >= open.deadlineMs) {
            fail(WizardErrors.local(ErrorCode.TIMEOUT))
            cancelAttempt(open)
            return
        }
        if (operation.connectionEpoch != view?.connectionEpoch || operation.operationState == OperationState.INTERRUPTED) {
            interrupt()
            return
        }
        when (operation.operationState) {
            OperationState.PENDING, OperationState.RUNNING, OperationState.CANCELLING -> return
            OperationState.SUCCEEDED -> Unit
            else -> {
                fail(operation.error ?: WizardErrors.local(ErrorCode.CANCELLED))
                if (operation.httpDiagnostic != null) {
                    diagnostic = operation.httpDiagnostic
                    diagnosticAttempt = attempt
                }
                return
            }
        }
        busy = false
        attempt = open.copy(closed = true)
        checks[operation.kind.ordinal] = CheckpointResult.SUCCESS
        when (step) {
            WizardStep.SERVER_URL -> step = WizardStep.SERVER_SECRET
            WizardStep.SERVER_SECRET -> {
                suggestedIp = (operation.payload as? SecretPayload)?.suggestedIp
                step = WizardStep.IDENTITY
            }
            else -> {
                discardedRecordCount = (operation.payload as? AccessPayload)?.discardedRecordCount
                step = WizardStep.CONTRACTS
            }
        }
    }

    private fun handleView(outcome: IpcOutcome<WizardView>, preserve: Boolean, reset: Boolean) {
        when (outcome) {
            is IpcOutcome.Success -> {
                load(outcome.value, preserve)
                if (reset) resetting = false
                contact()
                lastHeartbeat = clock.nowMs()
            }
            else -> {
                if (step == WizardStep.LOADING) {
                    step = WizardStep.LOAD_ERROR
                    error = errorOf(outcome)
                } else {
                    fail(errorOf(outcome))
                }
            }
        }
    }

    /** Port of `WizardModel::load`. */
    private fun load(next: WizardView, preserve: Boolean) {
        val newSession = next.sessionId != view?.sessionId
        if (newSession) sequence = 0uL
        if (!preserve) view?.fields?.wipe()
        view = next
        session = WizardSession(next.serviceInstanceId, next.sessionId)
        busy = false
        committing = false
        attempt = null
        error = null
        invalidate(0)
        offlineKnown = next.offline
        step = when {
            next.fields == null || next.context == null -> WizardStep.LOAD_ERROR
            next.offline -> WizardStep.SERVER_URL
            else -> WizardStep.DISCONNECT_GATE
        }
    }

    private fun resume(): Boolean {
        val current = session ?: return false
        val epoch = view?.connectionEpoch ?: return false
        return primary { handleView(client.wizardResume(current, epoch), preserve = true, reset = false) }
    }

    private fun readGateStatus() {
        if (statusBusy) return
        statusBusy = true
        val owner = generation
        scope.launch {
            val outcome = client.getStatus(IpcTimeouts.STATUS_REQUEST_MS)
            if (owner != generation) return@launch
            statusBusy = false
            val status = (outcome as? IpcOutcome.Success)?.value ?: return@launch
            val offline = status.state == ConnectionState.DISCONNECTED && status.phase == ConnectionPhase.IDLE
            offlineKnown = offline
            if (offline && disconnecting && !primaryBusy) {
                disconnecting = false
                resume()
            }
            publish()
        }
    }

    private fun closeOpenAttempt() {
        val open = attempt
        if (open != null && !open.closed) {
            fail(WizardErrors.local(ErrorCode.CANCELLED))
            cancelAttempt(open)
        }
    }

    private fun sendCloseOrReset() {
        val current = session ?: run {
            if (closing) destroy()
            return
        }
        if (controlBusy) return
        if (closing) {
            control {
                when (val outcome = client.wizardClose(current)) {
                    is IpcOutcome.ServiceError -> if (outcome.busy) return@control else destroy()
                    else -> destroy()
                }
            }
        } else if (resetting) {
            control {
                when (val outcome = client.wizardReset(current)) {
                    is IpcOutcome.Success -> handleView(outcome, preserve = false, reset = true)
                    is IpcOutcome.ServiceError -> if (!outcome.busy) {
                        resetting = false
                        fail(errorOf(outcome))
                    }
                    else -> {
                        resetting = false
                        fail(errorOf(outcome))
                    }
                }
            }
        }
    }

    /** cancel-attempt with the original identity; a late cancel never reaches a newer attempt. */
    private fun cancelAttempt(target: WizardAttempt) {
        val current = session ?: return
        control { client.wizardCancelAttempt(current, target.clientAttemptId, target.sequence) }
    }

    /** Invalidates the failed HTTP detail and asks the service to drop its copy once. */
    private fun clearDiagnostic() {
        val failed = diagnosticAttempt
        if (diagnostic != null && failed != null) cancelAttempt(failed)
        diagnostic = null
        diagnosticAttempt = null
    }

    private fun invalidate(from: Int) {
        clearDiagnostic()
        if (from <= 1) suggestedIp = null
        for (i in from until checks.size) checks[i] = CheckpointResult.PENDING
    }

    /** Port of `WizardController::fail` and `WizardModel::fail`. */
    private fun fail(value: WizardError) {
        if (committing || step == WizardStep.COMMIT_UNKNOWN) {
            commitUnknown(value)
            return
        }
        when (value.code) {
            ErrorCode.WIZARD_SESSION_EXPIRED.wireName -> sessionExpired()
            ErrorCode.WIZARD_CONNECTION_INTERRUPTED.wireName -> {
                interrupt()
                offlineKnown = false
            }
            else -> {
                clearDiagnostic()
                error = value
                busy = false
                attempt?.let { open ->
                    if (!open.closed) {
                        checks[open.kind.ordinal] = CheckpointResult.FAILED
                        attempt = open.copy(closed = true)
                    }
                }
            }
        }
    }

    private fun interrupt() {
        if (committing) return
        attempt = attempt?.copy(closed = true)
        busy = false
        invalidate(0)
        step = WizardStep.DISCONNECT_GATE
        error = WizardErrors.local(ErrorCode.WIZARD_CONNECTION_INTERRUPTED)
        // A Connect was accepted: "disconnected" is known again only from the next gate status read,
        // so an immediate "Disconnect and continue" sends the disconnect instead of a bare resume.
        offlineKnown = false
    }

    private fun sessionExpired() {
        busy = false
        attempt = attempt?.copy(closed = true)
        invalidate(0)
        if (!committing) step = WizardStep.SESSION_EXPIRED
        error = WizardErrors.local(ErrorCode.WIZARD_SESSION_EXPIRED)
    }

    private fun commitUnknown(value: WizardError?) {
        committing = false
        busy = false
        step = WizardStep.COMMIT_UNKNOWN
        if (value != null) error = value
    }

    private fun commitFailed(value: WizardError) {
        committing = false
        busy = false
        step = WizardStep.CONTRACTS
        error = value
    }

    private fun confirm(value: Receipt) {
        if (value.commitId != commitId) return
        committing = false
        busy = false
        receipt = value
        step = WizardStep.FINISHED
        error = null
        val current = session
        publish()
        // A lost close reply cannot undo the confirmed publication; the lease cleans up.
        if (current != null) control { client.wizardClose(current) }
    }

    private fun contact() {
        lastContact = clock.nowMs()
    }

    private fun destroy() {
        generation++
        ticker?.cancel()
        ticker = null
        view?.fields?.wipe()
        resetSessionState()
        step = WizardStep.CLOSED
        publish()
    }

    private fun resetSessionState() {
        view = null
        session = null
        checks.fill(CheckpointResult.PENDING)
        busy = false
        committing = false
        disconnecting = false
        closing = false
        resetting = false
        offlineKnown = false
        error = null
        diagnostic = null
        diagnosticAttempt = null
        suggestedIp = null
        discardedRecordCount = null
        attempt = null
        sequence = 0uL
        receipt = null
        restartState = null
        commitChecking = false
        commitId = null
        commitOperation = null
        primaryBusy = false
        controlBusy = false
        heartbeatBusy = false
        statusBusy = false
        val now = clock.nowMs()
        lastContact = now
        lastHeartbeat = now
        lastPoll = now
    }

    private fun stepFor(kind: OperationKind): WizardStep = when (kind) {
        OperationKind.URL -> WizardStep.SERVER_URL
        OperationKind.SECRET -> WizardStep.SERVER_SECRET
        OperationKind.ACCESS -> WizardStep.IDENTITY
        OperationKind.FINISH -> WizardStep.CONTRACTS
    }

    private fun errorOf(outcome: IpcOutcome<*>): WizardError = when (outcome) {
        is IpcOutcome.ServiceError -> outcome.wizard ?: outcome.error.toWizardError()
        IpcOutcome.TimedOut -> WizardErrors.local(ErrorCode.TIMEOUT)
        is IpcOutcome.Rejected -> WizardErrors.local(outcome.code)
        is IpcOutcome.Success -> WizardErrors.local(ErrorCode.INTERNAL)
    }

    private fun SafeError.toWizardError() = WizardError(code, description, recoverable, null, null, "")

    private fun publish() {
        _state.value = WizardUiState(
            step = step,
            view = view,
            checks = checks.toList(),
            busy = busy,
            committing = committing,
            disconnecting = disconnecting,
            closing = closing,
            resetting = resetting,
            offlineKnown = offlineKnown,
            error = error,
            httpDiagnostic = diagnostic,
            suggestedIp = suggestedIp,
            discardedRecordCount = discardedRecordCount,
            attempt = attempt,
            receipt = receipt,
            restartState = restartState,
            commitChecking = commitChecking,
        )
    }

    companion object {
        /** The tray Wizard timer (kWizardTickMs). */
        const val TICK_MS = 250L
    }
}
