package phpvpn.android.bridge.jni

/**
 * Raw JNI surface of client/apps/android-jni/jni_models.cpp, registered by
 * `JNI_OnLoad`. Use the typed wrappers of this package instead: they own the
 * handles, serialize the calls and wipe the secrets.
 *
 * Conventions: handles are `Long` (0 = none), IPv4 addresses are the host
 * order value as `Int`, an error is its stable wire name as UTF-8 (`null` =
 * success), text is UTF-8 `ByteArray` and secrets are `CharArray` (UTF-16
 * that the caller wipes). Enum results are the C++ ordinals, which equal the
 * ordinals of the Kotlin enums in phpvpn.android.bridge.controller.
 */
internal object ModelNatives {
    init {
        // JNI_OnLoad registers the natives; loading twice is a no-op.
        System.loadLibrary("phpvpn-android")
    }

    // ContractsModel (standalone handle or the model source of a machine).
    @JvmStatic external fun contractsCreate(document: CharArray, configPath: ByteArray, revisionToken: ByteArray, error: Array<ByteArray?>): Long

    @JvmStatic external fun contractsDestroy(model: Long)

    @JvmStatic external fun contractsRowAddresses(model: Long): IntArray

    @JvmStatic external fun contractsRowFlags(model: Long): IntArray

    @JvmStatic external fun contractsRowKey(model: Long, remoteIp: Int): CharArray?

    @JvmStatic external fun contractsState(model: Long): LongArray?

    @JvmStatic external fun contractsText(model: Long, which: Int): ByteArray?

    @JvmStatic external fun contractsSetEnabled(model: Long, remoteIp: Int, enabled: Boolean): ByteArray?

    @JvmStatic external fun contractsSetKey(model: Long, remoteIp: Int, key: CharArray): ByteArray?

    @JvmStatic external fun contractsSetFocus(model: Long, remoteIp: Int, column: Int): ByteArray?

    @JvmStatic external fun contractsTarget(model: Long, remoteIp: Int): Long

    @JvmStatic external fun contractsTargetContractId(model: Long, target: Long): ByteArray?

    @JvmStatic external fun contractsTargetRelease(model: Long, target: Long)

    @JvmStatic external fun contractsConfirmRandom(model: Long, target: Long, confirmed: Boolean): ByteArray?

    @JvmStatic external fun contractsApplyPasswordKey(model: Long, target: Long, key: CharArray): ByteArray?

    @JvmStatic external fun contractsResumeEditing(model: Long, saveGeneration: Long): Boolean

    // tray::ContractsSaveMachine.
    @JvmStatic external fun machineCreate(): Long

    @JvmStatic external fun machineDestroy(machine: Long)

    @JvmStatic external fun machineLoad(machine: Long, document: CharArray, configPath: ByteArray, revisionToken: ByteArray): ByteArray?

    @JvmStatic external fun machineStage(machine: Long): Int

    @JvmStatic external fun machineSaving(machine: Long): Boolean

    @JvmStatic external fun machineHasModel(machine: Long): Boolean

    @JvmStatic external fun machineModel(machine: Long): Long

    @JvmStatic external fun machineSnapshot(machine: Long, checking: Boolean): LongArray?

    @JvmStatic external fun machineChanged(machine: Long)

    @JvmStatic external fun machinePrepare(machine: Long, confirmedRepeat: Boolean, checking: Boolean): ByteArray?

    @JvmStatic external fun machineDispatch(machine: Long, nowMs: Long, operation: LongArray): CharArray?

    @JvmStatic external fun machineSaveFinished(machine: Long, operation: LongArray, outcome: Int, error: ByteArray, validationDescription: ByteArray): Int

    @JvmStatic external fun machineCheck(machine: Long, checking: Boolean): Long

    @JvmStatic external fun machineCheckFinished(machine: Long, checkGeneration: Long, requestId: ByteArray, response: ByteArray?): Int

    @JvmStatic external fun machineCanRepeat(machine: Long, checking: Boolean): Boolean

    @JvmStatic external fun machineNotice(machine: Long): ByteArray

    @JvmStatic external fun machineClose(machine: Long)

    // tray::classifySaveResponse: [outcome digit, error name, validation description].
    @JvmStatic external fun classifySave(operation: LongArray, requestId: ByteArray, response: ByteArray?, writeAttempted: Boolean, completedAtMs: Long): Array<ByteArray>?

    // PasswordKeyModel.
    @JvmStatic external fun passwordCreate(): Long

    @JvmStatic external fun passwordDestroy(model: Long)

    @JvmStatic external fun passwordUpdate(model: Long, password: CharArray): Int

    @JvmStatic external fun passwordState(model: Long): IntArray?

    @JvmStatic external fun passwordKey(model: Long): CharArray?

    @JvmStatic external fun passwordClear(model: Long)

    @JvmStatic external fun passwordRandom(): CharArray?

    // ServerInfoModel.
    @JvmStatic external fun serverInfoParse(response: ByteArray, error: Array<ByteArray?>): Long

    @JvmStatic external fun serverInfoDestroy(model: Long)

    @JvmStatic external fun serverInfoCounts(model: Long): LongArray?

    @JvmStatic external fun serverInfoRowAddresses(model: Long): IntArray?

    @JvmStatic external fun serverInfoRowStates(model: Long): IntArray?

    @JvmStatic external fun serverInfoRowLastSeen(model: Long): LongArray?

    @JvmStatic external fun serverInfoRowContract(model: Long, index: Int): ByteArray?

    @JvmStatic external fun serverInfoText(model: Long, which: Int): ByteArray?

    @JvmStatic external fun serverInfoStateText(state: Int): ByteArray?

    @JvmStatic external fun serverInfoPageCount(rows: Int): Int

    @JvmStatic external fun serverInfoPageNumbers(count: Int, current: Int): IntArray?

    // WizardIpc documentMatches and the get-default-config template check.
    @JvmStatic external fun configDocumentMatches(document: ByteArray, availability: ByteArray): Boolean

    @JvmStatic external fun configValidTemplate(document: ByteArray): Boolean
}
