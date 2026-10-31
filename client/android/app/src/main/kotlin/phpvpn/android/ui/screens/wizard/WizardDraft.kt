package phpvpn.android.ui.screens.wizard

import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.OperationKind
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.ipc.WizardFields
import phpvpn.android.bridge.ipc.WizardView

/**
 * A local check that failed before any IPC (`WizardModel::validateCurrent`).
 * [code] is the error code the desktop model returns for the same input.
 */
enum class LocalProblem(val code: ErrorCode, val field: String?) {
    NO_CONTEXT(ErrorCode.WIZARD_PREREQUISITE_FAILED, null),
    URL_INVALID(ErrorCode.CONFIG_INVALID_URL, "serverUrl"),
    URL_PLACEHOLDER(ErrorCode.CONFIG_INVALID_URL, "serverUrl"),
    SECRET_LITERAL(ErrorCode.CONFIG_INVALID_VALUE, "serverSecret"),
    NO_SUGGESTION(ErrorCode.WIZARD_PREREQUISITE_FAILED, "virtualIp"),
    IP_INVALID(ErrorCode.CONFIG_INVALID_ADDRESS, "virtualIp"),
    KEY_INVALID(ErrorCode.CONFIG_INVALID_KEY, "accessKey"),
}

/** The Wizard step whose inputs [WizardDraft.validate] checks. */
enum class DraftStep { SERVER_URL, SERVER_SECRET, IDENTITY }

/**
 * Field logic of the Configuration Wizard, the Kotlin port of the draft part
 * of `WizardModel` (core/wizard/wizard_model.cpp): default secret, literal
 * secret display, automatic IP from the verified suggestion, the default
 * access key that follows every IP change, the contracts option and the
 * local validation of each step. The IPC workflow (attempts, checks, lease,
 * Finish) stays in [phpvpn.android.bridge.controller.WizardController];
 * every mutator returns the [OperationKind] whose evidence the change
 * invalidates, so the caller can tell the controller.
 *
 * Secrets (server secret, its editor text, access key) live only in
 * [SecretText]; [wipe] zeroes them. Not thread-safe: use from one thread.
 */
class WizardDraft {
    var serverUrl: String = ""
        private set
    var virtualIp: String = ""
        private set
    var httpHeaders: String = ""
        private set
    var useDefaultSecret: Boolean = false
        private set
    var autoDetectIp: Boolean = false
        private set
    var useDefaultKey: Boolean = false
        private set
    var createContracts: Boolean = true
        private set

    /** True when the secret has control characters and is edited as a JSON string literal. */
    var literalSecret: Boolean = false
        private set

    /** False while the literal editor text is not one complete JSON string. */
    var secretValid: Boolean = true
        private set

    private var serverSecret: SecretText = SecretText.empty()
    private var secretEditor: SecretText = SecretText.empty()
    private var accessKey: SecretText = SecretText.empty()

    var vpnCidr: String? = null
        private set
    var contractCountOriginal: ULong = 0uL
        private set
    private var existingDefaultPeers: List<String> = emptyList()
    private var loaded = false

    /** The text of the secret field (the literal form in literal mode). */
    val secretEditorText: SecretText get() = secretEditor

    /** The access key text of the field (the default key while [useDefaultKey]). */
    val accessKeyText: SecretText get() = accessKey

    /** The exact secret that a check or Finish sends. */
    val secretValue: SecretText get() = serverSecret

    val hasContext: Boolean get() = vpnCidr != null

    /**
     * `WizardModel::load`. A new session or [preserveFields] false replaces
     * the draft with the service fields; Resume of the same session keeps
     * every entered value and option. Returns true when the fields were
     * replaced.
     */
    fun load(view: WizardView, preserveFields: Boolean, newSession: Boolean): Boolean {
        vpnCidr = view.context?.vpnCidr
        contractCountOriginal = view.contractCount
        existingDefaultPeers = view.existingDefaultContractPeers
        val fields = view.fields
        if (fields == null || view.context == null) {
            clearFields()
            loaded = false
            return true
        }
        if (preserveFields && !newSession && loaded) return false
        clearFields()
        serverUrl = fields.serverUrl
        virtualIp = fields.virtualIp
        httpHeaders = fields.httpHeaders
        useDefaultSecret = fields.useDefaultServerSecretInWizard
        autoDetectIp = fields.autoDetectIpAddressInWizard
        useDefaultKey = fields.useDefaultAccessKeyInWizard
        serverSecret = if (useDefaultSecret) SecretText.of(WizardRules.DEFAULT_SERVER_SECRET) else fields.serverSecret.copy()
        accessKey = fields.accessKey.copy()
        updateDefaultKey()
        updateSecretText()
        createContracts = true
        loaded = true
        return true
    }

    /** `setUrl`: a different URL invalidates every check. */
    fun setUrl(text: String): OperationKind? {
        if (serverUrl == text) return null
        serverUrl = text
        return OperationKind.URL
    }

    /**
     * `setHttpHeaders`: accepts only a valid block (canonical CRLF) that fits
     * the document; a changed value invalidates every check. On rejection the
     * draft keeps its previous value.
     */
    fun setHttpHeaders(text: String): HeaderCheck {
        val check = HeaderRules.validate(text)
        if (check is HeaderCheck.Valid) httpHeaders = check.normalized
        return check
    }

    /** `setSecretEditor`; ignored while the default secret is selected. Takes ownership of [text]. */
    fun setSecretEditor(text: SecretText): OperationKind? {
        if (useDefaultSecret) {
            text.wipe()
            return null
        }
        secretEditor.wipe()
        secretEditor = text
        if (literalSecret) {
            val parsed = text.read { SecretLiteral.parse(it) }
            secretValid = parsed != null
            if (parsed != null) replaceSecret(SecretText.adopt(parsed))
        } else {
            secretValid = text.read { SecretLiteral.pairedSurrogates(it) }
            if (secretValid) replaceSecret(text.copy())
        }
        return OperationKind.SECRET
    }

    /** `setDefaultSecret`: on replaces the value with the default, off keeps the shown text. */
    fun setDefaultSecret(checked: Boolean): OperationKind {
        useDefaultSecret = checked
        if (checked) replaceSecret(SecretText.of(WizardRules.DEFAULT_SERVER_SECRET))
        updateSecretText()
        return OperationKind.SECRET
    }

    /** `setIp`: refused while automatic detection decides the IP. */
    fun setIp(text: String): OperationKind? {
        if (autoDetectIp) return null
        if (virtualIp == text) return null
        virtualIp = text
        updateDefaultKey()
        return OperationKind.ACCESS
    }

    /** `setAutoDetectIpAddress`: on takes the still valid verified suggestion, if any. */
    fun setAutoDetectIp(checked: Boolean, suggestedIp: String?): OperationKind {
        autoDetectIp = checked
        if (checked && suggestedIp != null) virtualIp = suggestedIp
        updateDefaultKey()
        return OperationKind.ACCESS
    }

    /** `setAccessKey`: refused while the default key is selected. Takes ownership of [text]. */
    fun setAccessKey(text: SecretText): OperationKind? {
        if (useDefaultKey) {
            text.wipe()
            return null
        }
        accessKey.wipe()
        accessKey = text
        return OperationKind.ACCESS
    }

    /** `setDefaultAccessKey`: on overwrites the key with the default of the IP, off keeps the shown key. */
    fun setDefaultKey(checked: Boolean): OperationKind {
        useDefaultKey = checked
        updateDefaultKey()
        return OperationKind.ACCESS
    }

    /** `setCreateContracts`: only where default contracts are supported. */
    fun setCreateContracts(checked: Boolean): Boolean {
        if (!contractsAllowed) return false
        createContracts = checked
        return true
    }

    /**
     * The secret check succeeded with [suggestedIp] (`applyOperation`): with
     * automatic detection the IP takes the suggestion and the default key
     * follows. Returns true when a visible value changed.
     */
    fun applySuggestion(suggestedIp: String): Boolean {
        if (!autoDetectIp || virtualIp == suggestedIp) return false
        virtualIp = suggestedIp
        updateDefaultKey()
        return true
    }

    val contractsAllowed: Boolean get() = WizardRules.supportsDefaultContracts(vpnCidr)

    val shouldAddDefaultContracts: Boolean get() = createContracts && contractsAllowed

    /** `missingContractCount`: defaults missing for the current own IP. */
    val missingContractCount: Int
        get() {
            if (!contractsAllowed || !WizardRules.isValidClientAddress(virtualIp, vpnCidr)) return 0
            return WizardRules.missingDefaultContractPeers(vpnCidr, virtualIp, existingDefaultPeers).size
        }

    /** `contractCount`: the original contracts plus what Finish adds. */
    val contractCount: ULong
        get() = contractCountOriginal + (if (shouldAddDefaultContracts) missingContractCount.toULong() else 0uL)

    /** What Finish adds (16, "To add on completion"). */
    val contractsToAdd: Int get() = if (shouldAddDefaultContracts) missingContractCount else 0

    /** `validateCurrent` for [step]; null when the inputs may be sent. */
    fun validate(step: DraftStep, suggestedIp: String?): LocalProblem? {
        if (vpnCidr == null) return LocalProblem.NO_CONTEXT
        return when (step) {
            DraftStep.SERVER_URL -> when (WizardRules.checkServerUrl(serverUrl)) {
                WizardRules.UrlCheck.VALID -> null
                WizardRules.UrlCheck.INVALID -> LocalProblem.URL_INVALID
                WizardRules.UrlCheck.PLACEHOLDER -> LocalProblem.URL_PLACEHOLDER
            }
            DraftStep.SERVER_SECRET -> if (secretValid) null else LocalProblem.SECRET_LITERAL
            DraftStep.IDENTITY -> when {
                autoDetectIp && (suggestedIp == null || suggestedIp != virtualIp) -> LocalProblem.NO_SUGGESTION
                !WizardRules.isValidClientAddress(virtualIp, vpnCidr) -> LocalProblem.IP_INVALID
                !accessKey.read { key -> key.size == 64 && key.all { it in '0'..'9' || it in 'a'..'f' } } -> LocalProblem.KEY_INVALID
                else -> null
            }
        }
    }

    /**
     * The eight fields for a check or Finish, with their own secret copies.
     * The caller wipes the result (the controller copies again and wipes its
     * copy after sending).
     */
    fun toFields(): WizardFields = WizardFields(
        serverUrl = serverUrl,
        serverSecret = serverSecret.copy(),
        virtualIp = virtualIp,
        accessKey = accessKey.copy(),
        httpHeaders = httpHeaders,
        useDefaultServerSecretInWizard = useDefaultSecret,
        autoDetectIpAddressInWizard = autoDetectIp,
        useDefaultAccessKeyInWizard = useDefaultKey,
    )

    /** True when the secret equals the publicly known default (warning on 14). */
    fun secretIsDefault(): Boolean = serverSecret.read { secret ->
        val default = WizardRules.DEFAULT_SERVER_SECRET
        secret.size == default.length && secret.indices.all { secret[it] == default[it] }
    }

    fun wipe() {
        clearFields()
        loaded = false
    }

    private fun clearFields() {
        serverSecret.wipe()
        secretEditor.wipe()
        accessKey.wipe()
        serverSecret = SecretText.empty()
        secretEditor = SecretText.empty()
        accessKey = SecretText.empty()
        serverUrl = ""
        virtualIp = ""
        httpHeaders = ""
        literalSecret = false
        secretValid = true
    }

    private fun replaceSecret(value: SecretText) {
        serverSecret.wipe()
        serverSecret = value
    }

    /** `updateDefaultKey`: the default of a valid client IP, empty for anything else. */
    private fun updateDefaultKey() {
        if (!useDefaultKey) return
        accessKey.wipe()
        accessKey = if (WizardRules.isValidClientAddress(virtualIp, vpnCidr)) {
            WizardRules.defaultAccessKey(virtualIp)
        } else {
            SecretText.empty()
        }
    }

    /** `updateSecretText`: the literal form for control characters, the plain text otherwise. */
    private fun updateSecretText() {
        secretEditor.wipe()
        literalSecret = serverSecret.read { SecretLiteral.needed(it) }
        secretEditor = if (literalSecret) SecretText.adopt(serverSecret.read { SecretLiteral.escape(it) }) else serverSecret.copy()
        secretValid = true
    }
}
