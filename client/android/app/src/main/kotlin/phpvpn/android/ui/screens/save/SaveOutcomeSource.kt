package phpvpn.android.ui.screens.save

import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.getValue
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigation.NavController
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.ui.navigation.SaveOrigin
import phpvpn.android.ui.navigation.WizardFinish
import phpvpn.android.ui.navigation.finishWizard
import phpvpn.android.ui.navigation.showSaveChecked
import phpvpn.android.ui.navigation.showSaved

/*
 * Contract between the flows that save a configuration (the Wizard of WP09,
 * the contracts and the editor of WP10) and the shared outcome screens
 * "Save result unknown" (33) and "Save checked" (46), which live in
 * EditorGraph.kt (WP10) under the routes SaveUnknown(origin) and
 * SaveChecked(origin).
 *
 * - Each origin flow implements [SaveOutcomeSource] in the state holder that
 *   owns its draft and registers it in [SaveOutcomeRegistry] for as long as
 *   the draft lives (the Wizard: WizardViewModel, scoped to the Wizard graph,
 *   registers in init and unregisters in onCleared). The registration does
 *   not depend on a screen being composed, because 33 is drawn on top of the
 *   origin.
 * - The shared screen reads the source of its route's origin with
 *   [rememberSaveOutcomeSource] and renders [SaveOutcomeSource.state]:
 *   UNKNOWN (33 with "Verify save" enabled), CHECKING (33, actions disabled),
 *   CONFIRMED, NOT_SAVED and CONFLICT. On every state it calls
 *   [followSaveOutcome], which performs the one navigation rule per origin
 *   (CONFIRMED: Wizard → 17, others → 25; NOT_SAVED: Wizard → back to 16 with
 *   the preserved draft and the error, others → 46; CONFLICT → 46).
 * - "Verify save" calls [SaveOutcomeSource.verify]: exactly one check (the
 *   Wizard: one wizard-get-commit for the original commitId; contracts and
 *   editor: one get-config-document), never a second write. The call returns
 *   false when nothing may be checked now (already checking, no unknown save).
 * - "Save draft again" (46) calls [SaveOutcomeSource.repeatSave] only while
 *   [SaveOutcomeState.canRepeat] is true. The Wizard never repeats a write
 *   from 33/46: its source always reports canRepeat false and returns false;
 *   a failed receipt sends the user back to 16, where Finish is a new,
 *   explicit commit with a new commitId.
 * - "Inspect current configuration" is plain navigation to the editor
 *   (`openEditor()`), it does not involve the source.
 * - With no registered source (process recreated, origin already closed) the
 *   screen shows the unknown state without a check action and offers only
 *   the inspection; it never claims success or failure.
 */

/** Phase of an uncertain save as the shared screens 33 and 46 show it. */
enum class SaveOutcomePhase {
    /** No save outcome is pending for this origin. */
    IDLE,

    /** The result of the last save is unknown (33). */
    UNKNOWN,

    /** The explicit check of 33 is running. */
    CHECKING,

    /** The write is confirmed: success, never Retry. */
    CONFIRMED,

    /** The write certainly did not happen; the draft is preserved. */
    NOT_SAVED,

    /** The current configuration differs from the draft (contracts, editor); never overwritten silently. */
    CONFLICT,
}

/**
 * What 33 and 46 render. [errorCode] is the code of the last failure (a text
 * is chosen by code, never from a received description). After a service
 * restart the Wizard cannot read its receipt any more: [serviceRestarted]
 * with the one config-state read ([configAvailability], [configPath]).
 */
@Immutable
data class SaveOutcomeState(
    val phase: SaveOutcomePhase = SaveOutcomePhase.IDLE,
    val canRepeat: Boolean = false,
    val draftChanges: Int? = null,
    val errorCode: String? = null,
    val serviceRestarted: Boolean = false,
    val configAvailability: ConfigAvailability? = null,
    val configPath: String? = null,
)

/** The origin side of 33 and 46; see the contract above. Main thread only. */
interface SaveOutcomeSource {
    val origin: SaveOrigin
    val state: StateFlow<SaveOutcomeState>

    /** "Verify save" on 33: one check of the unknown save, never a second write. */
    fun verify(): Boolean

    /** "Save draft again" on 46, only while [SaveOutcomeState.canRepeat]. */
    fun repeatSave(): Boolean
}

/** The live sources by origin; at most one per origin. */
object SaveOutcomeRegistry {
    private val sources = MutableStateFlow<Map<SaveOrigin, SaveOutcomeSource>>(emptyMap())

    val all: StateFlow<Map<SaveOrigin, SaveOutcomeSource>> = sources.asStateFlow()

    /** Registers [source] for its origin; the returned action removes exactly this registration. */
    fun register(source: SaveOutcomeSource): () -> Unit {
        sources.value = sources.value + (source.origin to source)
        return {
            if (sources.value[source.origin] === source) sources.value = sources.value - source.origin
        }
    }

    fun source(origin: SaveOrigin): SaveOutcomeSource? = sources.value[origin]
}

/** The registered source of [origin], recomposed when it registers or leaves. */
@Composable
fun rememberSaveOutcomeSource(origin: SaveOrigin): SaveOutcomeSource? {
    val registered by SaveOutcomeRegistry.all.collectAsStateWithLifecycle()
    return registered[origin]
}

/** The navigation rule of [phase] for [origin]; IDLE, UNKNOWN and CHECKING stay. */
fun NavController.followSaveOutcome(origin: SaveOrigin, phase: SaveOutcomePhase) {
    when (phase) {
        SaveOutcomePhase.CONFIRMED -> if (origin == SaveOrigin.Wizard) finishWizard() else showSaved(origin)
        SaveOutcomePhase.NOT_SAVED -> if (origin == SaveOrigin.Wizard) {
            // Back to 16: the draft and the reported error are still there.
            popBackStack<WizardFinish>(inclusive = false)
        } else {
            showSaveChecked(origin)
        }
        SaveOutcomePhase.CONFLICT -> showSaveChecked(origin)
        SaveOutcomePhase.IDLE, SaveOutcomePhase.UNKNOWN, SaveOutcomePhase.CHECKING -> Unit
    }
}
