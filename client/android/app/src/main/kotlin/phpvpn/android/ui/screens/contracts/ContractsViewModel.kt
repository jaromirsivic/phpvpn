package phpvpn.android.ui.screens.contracts

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import phpvpn.android.bridge.controller.ContractColumn
import phpvpn.android.bridge.controller.ContractFocus
import phpvpn.android.bridge.controller.ContractTargetHandle
import phpvpn.android.bridge.controller.ContractsController
import phpvpn.android.bridge.controller.ContractsEvent
import phpvpn.android.bridge.controller.ContractsModelHandle
import phpvpn.android.bridge.controller.ContractsStage
import phpvpn.android.bridge.controller.ContractsUiState
import phpvpn.android.bridge.controller.SaveOutcome
import phpvpn.android.bridge.controller.SaveResponseClassifier
import phpvpn.android.bridge.controller.SaveResult
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.ui.screens.ConfigFlowDependencies
import phpvpn.android.ui.screens.save.SaveOutcomeSource
import phpvpn.android.ui.screens.save.SaveVerification

/** Page size of the contracts list, `kTablePageSize` of core/presentation/table_pagination.h. */
const val CONTRACTS_PAGE_SIZE = 20

/** `tablePageCount`. */
fun contractsPageCount(rows: Int): Int = maxOf(1, (rows + CONTRACTS_PAGE_SIZE - 1) / CONTRACTS_PAGE_SIZE)

/** One list row of 19/41, without the key text. */
data class ContractListRow(
    val remoteIp: String,
    val enabled: Boolean,
    val dirty: Boolean,
    /** Enabled with an invalid key: blocks Save (22). */
    val blocking: Boolean,
)

/** Values of one contract while its detail (20/22) is open. */
data class ContractDetailState(
    val remoteIp: String,
    val localIp: String,
    val enabled: Boolean,
    val keyLength: Int,
    val keyValid: Boolean,
    val keyErrorVisible: Boolean,
    /** The row differs from the state when the detail was opened. */
    val changedSinceOpen: Boolean,
    val editable: Boolean,
)

/** Why the last save did not publish: [code] classified, [notSent] when the request never left. */
data class SaveProblem(val code: ErrorCode, val notSent: Boolean)

/** One-shot navigation requests of the contracts flow. */
sealed interface ContractsNavigation {
    /** Confirmed save → 25. */
    data object Saved : ContractsNavigation

    /** Unknown save → 33. */
    data object Unknown : ContractsNavigation

    /** Save is blocked by the first invalid key → 22 for [remoteIp]. */
    data class Blocked(val remoteIp: String) : ContractsNavigation
}

/**
 * State holder of the contracts flow (19, 20, 21, 22, 41 and the contracts
 * side of 33 and 46), scoped to the contracts navigation graph so the draft
 * survives page switches and the detail screens. All decisions stay in the
 * native [phpvpn.android.bridge.controller.ContractsSaveMachineHandle] (the
 * Kotlin stand-in until WP04); this class only routes UI actions to it.
 */
class ContractsViewModel(private val deps: ConfigFlowDependencies) : ViewModel(), SaveOutcomeSource {
    private val machine = deps.contractsMachine()
    private var lastSaveResult: SaveResult? = null

    // The controller reports only the event; the classified result carries
    // the rejection code that screen 19 explains.
    private val recordingClassifier = SaveResponseClassifier { operation, requestId, response, writeAttempted, completedAtMs ->
        deps.classifier.classify(operation, requestId, response, writeAttempted, completedAtMs).also { lastSaveResult = it }
    }
    private val controller = ContractsController(deps.ipcClient, machine, recordingClassifier, deps.clock)
    private val _navigation = MutableStateFlow<ContractsNavigation?>(null)
    private var snapshot: Snapshot? = null

    val state: StateFlow<ContractsUiState> = controller.state
    val navigation: StateFlow<ContractsNavigation?> = _navigation.asStateFlow()

    /** Last result of [verify], read by 46. */
    var verification by mutableStateOf<SaveVerification?>(null)
        private set

    /** A save that was refused before publication or not sent at all; the draft is kept. */
    var rejection by mutableStateOf<SaveProblem?>(null)
        private set

    var verifying by mutableStateOf(false)
        private set

    private val model: ContractsModelHandle? get() = machine.model

    init {
        reload()
    }

    fun reload() {
        viewModelScope.launch { controller.open() }
    }

    val localIp: String get() = model?.localIp ?: ""

    /** All rows in numeric order; the key copies are wiped right away. */
    fun rows(): List<ContractListRow> = model?.rows()?.map { row ->
        row.keyText.wipe()
        ContractListRow(row.remoteIp, row.enabled, row.dirty, row.enabled && !row.keyValid)
    } ?: emptyList()

    fun dirtyCount(): Int = rows().count { it.dirty }

    val blockingErrors: Int get() = model?.blockingErrors ?: 0

    val firstBlocking: String? get() = model?.firstBlockingFocus?.remoteIp

    val editable: Boolean get() = model?.let { !it.frozen && !it.closed } ?: false

    // ------------------------------------------------------------------ detail (20, 22)

    /** Remembers the row as it is when the detail opens, for Discard (32). */
    fun openDetail(remoteIp: String) {
        if (snapshot?.remoteIp == remoteIp) return
        snapshot?.key?.wipe()
        val row = model?.row(remoteIp) ?: return
        snapshot = Snapshot(remoteIp, row.enabled, row.keyText)
        model?.setFocus(ContractFocus(remoteIp, ContractColumn.KEY))
    }

    fun detail(remoteIp: String): ContractDetailState? {
        val row = model?.row(remoteIp) ?: return null
        try {
            val opened = snapshot?.takeIf { it.remoteIp == remoteIp }
            val changed = opened != null && (opened.enabled != row.enabled || !opened.key.contentEquals(row.keyText))
            return ContractDetailState(
                remoteIp = remoteIp,
                localIp = localIp,
                enabled = row.enabled,
                keyLength = row.keyText.length,
                keyValid = row.keyValid,
                keyErrorVisible = row.keyErrorVisible,
                changedSinceOpen = changed,
                editable = editable,
            )
        } finally {
            row.keyText.wipe()
        }
    }

    /** The key as text for the field; a String copy is unavoidable for the text field. */
    fun keyText(remoteIp: String): String {
        val row = model?.row(remoteIp) ?: return ""
        return try {
            row.keyText.reveal()
        } finally {
            row.keyText.wipe()
        }
    }

    fun setEnabled(remoteIp: String, enabled: Boolean) {
        val current = model ?: return
        current.setEnabled(remoteIp, enabled)
        changed()
    }

    /** Applies a typed key to the draft; returns false when the model refused it (size limit). */
    fun setKey(remoteIp: String, text: String): Boolean {
        val key = SecretText.of(text)
        val error = try {
            model.let { current -> if (current == null) ErrorCode.INVALID_ARGUMENT else current.setKey(remoteIp, key) }
        } finally {
            key.wipe()
        }
        changed()
        return error == null
    }

    /** Apply to draft: the detail's changes stay in the shared draft. */
    fun closeDetail() {
        snapshot?.key?.wipe()
        snapshot = null
        model?.setFocus(null)
    }

    /** Discard (32) on a detail: the row returns to the state it had when the detail opened. */
    fun discardDetail(remoteIp: String) {
        val opened = snapshot?.takeIf { it.remoteIp == remoteIp }
        if (opened != null && editable) {
            model?.setKey(remoteIp, opened.key)
            model?.setEnabled(remoteIp, opened.enabled)
        }
        closeDetail()
        changed()
    }

    // ------------------------------------------------------------------ password (21)

    /** `ContractTarget` captured when 21 opens; stale after a save freezes the model. */
    fun target(remoteIp: String): ContractTargetHandle? = model?.target(remoteIp)

    fun applyPasswordKey(target: ContractTargetHandle, key: SecretText): ErrorCode? {
        val current = model ?: return ErrorCode.INVALID_ARGUMENT
        val error = current.applyPasswordKey(target, key)
        changed()
        return error
    }

    fun targetEnabled(remoteIp: String): Boolean {
        val row = model?.row(remoteIp) ?: return false
        row.keyText.wipe()
        return row.enabled
    }

    // ------------------------------------------------------------------ save (19 → 25/33/22)

    fun save() {
        rejection = null
        firstBlocking?.let {
            _navigation.value = ContractsNavigation.Blocked(it)
            return
        }
        viewModelScope.launch { handle(controller.save()) }
    }

    /** "Save draft again" on 46: only after a check found a different file. */
    fun saveAgain() {
        if (!state.value.canRepeat) return
        rejection = null
        viewModelScope.launch { handle(controller.save(confirmedRepeat = true)) }
    }

    private fun handle(event: ContractsEvent) {
        val result = lastSaveResult
        lastSaveResult = null
        when (event) {
            ContractsEvent.SAVED -> _navigation.value = ContractsNavigation.Saved
            ContractsEvent.UNKNOWN -> {
                verification = null
                _navigation.value = ContractsNavigation.Unknown
            }
            ContractsEvent.REJECTED -> rejection = SaveProblem(result?.error ?: ErrorCode.INTERNAL, result?.outcome == SaveOutcome.NOT_SENT)
            // A local preparation failure (size limits, busy): nothing was sent.
            else -> controller.state.value.failureCode?.let { rejection = SaveProblem(ErrorCode.fromWire(it) ?: ErrorCode.INTERNAL, false) }
        }
    }

    fun consumeNavigation() {
        _navigation.value = null
    }

    /** 33 → 46: one get-config-document compared with the frozen candidate; never writes. */
    override suspend fun verify(): SaveVerification = viewModelScope.async {
        verifying = true
        try {
            val result = when (controller.checkSaveResult()) {
                ContractsEvent.MATCHED -> SaveVerification.Confirmed(controller.state.value.configPath ?: "")
                ContractsEvent.DIFFERENT -> SaveVerification.Failed(DIFFERENT)
                else -> SaveVerification.Unknown
            }
            verification = result
            result
        } finally {
            verifying = false
        }
    }.await()

    private fun changed() {
        viewModelScope.launch { controller.changed() }
    }

    override fun onCleared() {
        snapshot?.key?.wipe()
        snapshot = null
        machine.close()
    }

    private class Snapshot(val remoteIp: String, val enabled: Boolean, val key: SecretText)

    companion object {
        /** [SaveVerification.Failed] reason when the current configuration is not the draft. */
        const val DIFFERENT = "different"

        /** True while the stage allows changes in the list and the details. */
        fun editing(stage: ContractsStage): Boolean = stage == ContractsStage.EDITING || stage == ContractsStage.INVALID
    }
}
