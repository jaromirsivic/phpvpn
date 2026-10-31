package phpvpn.android.ui.screens.save

/**
 * How the shared screens 33 (Save result unknown) and 46 (Save checked)
 * verify an unconfirmed save. The origin owns the check:
 * - the Wizard (WP09) asks `wizard-get-commit` for its commit receipt;
 * - the editor and the communication contracts (WP10) read the current
 *   document once with `get-config-document` and compare it with the
 *   submitted draft ([phpvpn.android.bridge.controller.EditorController],
 *   [phpvpn.android.bridge.controller.ContractsController]).
 *
 * [verify] never writes. A repeated write is a separate, explicit user
 * decision on 46 after [SaveVerification.Failed].
 */
interface SaveOutcomeSource {
    suspend fun verify(): SaveVerification
}

/** Result of [SaveOutcomeSource.verify]. */
sealed class SaveVerification {
    /**
     * The write is proven: the receipt is committed, or the current
     * configuration equals the submitted draft. This is a success (46 leads
     * on to 25), never a reason to save again. [summary] is the origin's
     * short, non-secret description (the configuration path or the receipt).
     */
    data class Confirmed(val summary: String) : SaveVerification()

    /**
     * The draft is provably not the current state (the receipt failed, or the
     * current configuration differs or is missing). The draft is kept; only an
     * explicit "Save draft again" on 46 writes, never a silent overwrite.
     * [error] is the reason as a wire code (for example `config_not_found`).
     */
    data class Failed(val error: String) : SaveVerification()

    /** The check itself could not decide (read failed or timed out); stay on 33. */
    data object Unknown : SaveVerification()
}
