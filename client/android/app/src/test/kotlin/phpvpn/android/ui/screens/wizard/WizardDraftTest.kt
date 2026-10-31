package phpvpn.android.ui.screens.wizard

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.ipc.HttpHeaders
import phpvpn.android.bridge.ipc.OperationKind
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.ipc.WizardMode
import phpvpn.android.ui.screens.wizard.WizardTestViews.modelView

/** Compares a secret without ever putting its value into the test output. */
internal fun assertSecret(message: String, expected: String, actual: SecretText) {
    assertTrue(message, SecretText.of(expected).contentEquals(actual))
}

/**
 * Field logic of [WizardDraft], 1:1 with the field cases of
 * client/tests/unit/test_wizard_model.cpp (the attempt, correlation and
 * commit cases belong to WizardController and WizardViewModelTest).
 */
class WizardDraftTest {
    private fun loaded(view: phpvpn.android.bridge.ipc.WizardView = modelView()) =
        WizardDraft().also { it.load(view, preserveFields = false, newSession = true) }

    /** WizardModel.DefaultKeyTransitionsAndExistingValue */
    @Test
    fun defaultKeyTransitionsAndExistingValue() {
        val draft = loaded()
        assertTrue(draft.useDefaultKey)
        assertEquals(OperationKind.ACCESS, draft.setAutoDetectIp(false, null))
        assertEquals(OperationKind.ACCESS, draft.setIp("10.99.99.10"))
        assertSecret("default key of .10", WizardRules.defaultAccessKey("10.99.99.10").reveal(), draft.accessKeyText)
        assertNull("own key refused while the default is on", draft.setAccessKey(SecretText.of("a".repeat(64))))
        draft.setDefaultKey(false)
        assertNotNull(draft.setAccessKey(SecretText.of("a".repeat(64))))
        draft.setIp("10.99.99.11")
        assertSecret("own key kept on IP change", "a".repeat(64), draft.accessKeyText)
        draft.setDefaultKey(true)
        assertSecret("switching on overwrites with the default", WizardRules.defaultAccessKey("10.99.99.11").reveal(), draft.accessKeyText)
        draft.setDefaultKey(false)
        assertSecret("switching off keeps the shown default", WizardRules.defaultAccessKey("10.99.99.11").reveal(), draft.accessKeyText)
        draft.setDefaultKey(true)
        draft.setIp("invalid")
        assertTrue("an invalid IP empties the default key", draft.accessKeyText.isEmpty())

        // An existing key equal to the algorithmic default with the flag false stays own.
        val edit = modelView(
            mode = WizardMode.EDIT,
            virtualIp = "10.99.99.2",
            accessKey = WizardRules.defaultAccessKey("10.99.99.2").reveal(),
            useDefaultKey = false,
        )
        assertFalse(loaded(edit).useDefaultKey)
    }

    /** WizardModel.HttpHeadersCanonicalDraftInvalidatesChecksAndIsolatesSessions (draft part) */
    @Test
    fun httpHeadersCanonicalDraftAndSessionIsolation() {
        val view = modelView()
        val draft = loaded(view)
        assertEquals(HttpHeaders.DEFAULT, draft.httpHeaders)
        assertTrue(draft.setHttpHeaders(HttpHeaders.DEFAULT) is HeaderCheck.Valid)
        assertEquals(HttpHeaders.DEFAULT, draft.httpHeaders)
        assertTrue(draft.setHttpHeaders("Cookie: synthetic=1\nUser-Agent: test\n") is HeaderCheck.Valid)
        assertEquals("Cookie: synthetic=1\r\nUser-Agent: test", draft.httpHeaders)
        val expected = mapOf(
            "" to HeaderCheck.Invalid(HeaderProblem.REQUIRED, 1),
            "Host: secret" to HeaderCheck.Invalid(HeaderProblem.MANAGED, 1, "Host"),
            "X: a\n\nY: b" to HeaderCheck.Invalid(HeaderProblem.LINE_FORMAT, 2),
            "Cookie: a\ncOoKiE: b" to HeaderCheck.Invalid(HeaderProblem.DUPLICATE, 2, "cOoKiE"),
        )
        for ((bad, problem) in expected) {
            assertEquals(problem, draft.setHttpHeaders(bad))
            assertEquals("rejected block keeps the draft", "Cookie: synthetic=1\r\nUser-Agent: test", draft.httpHeaders)
        }
        // Resume of the same session keeps the draft, a new session takes the service value.
        assertFalse(draft.load(view, preserveFields = true, newSession = false))
        assertEquals("Cookie: synthetic=1\r\nUser-Agent: test", draft.httpHeaders)
        draft.load(modelView(sessionId = WizardTestViews.OPID), preserveFields = true, newSession = true)
        assertEquals(HttpHeaders.DEFAULT, draft.httpHeaders)
    }

    /** WizardModel.ExactSecretAndLiteralRoundTrip */
    @Test
    fun exactSecretAndLiteralRoundTrip() {
        val secrets = listOf("", "0".repeat(32), "0".repeat(64), " a ", "a\u0000b\n")
        for (secret in secrets) {
            val draft = loaded(modelView(secret = secret))
            assertFalse(draft.useDefaultSecret)
            assertSecret("exact secret loaded", secret, draft.secretValue)
            draft.setSecretEditor(draft.secretEditorText.copy())
            assertSecret("editor round trip keeps the exact bytes", secret, draft.secretValue)
            if (draft.literalSecret) {
                assertSecret("JSON literal form", "\"a\\u0000b\\n\"", draft.secretEditorText)
                draft.setSecretEditor(SecretText.of("\"incomplete"))
                assertEquals(LocalProblem.SECRET_LITERAL, draft.validate(DraftStep.SERVER_SECRET, null))
                assertSecret("an invalid literal keeps the last exact value", secret, draft.secretValue)
            } else {
                assertNull(draft.validate(DraftStep.SERVER_SECRET, null))
            }
            draft.setDefaultSecret(true)
            assertSecret("default on", WizardRules.DEFAULT_SERVER_SECRET, draft.secretValue)
            draft.setDefaultSecret(false)
            assertSecret("default off keeps the shown default", WizardRules.DEFAULT_SERVER_SECRET, draft.secretValue)
        }
    }

    /** The explicit default secret flag wins and disables own entry. */
    @Test
    fun defaultSecretFlagDisablesOwnEntry() {
        val draft = loaded(modelView(secret = "own", useDefaultSecret = true))
        assertTrue(draft.useDefaultSecret)
        assertSecret("flag true shows the default", WizardRules.DEFAULT_SERVER_SECRET, draft.secretValue)
        assertNull(draft.setSecretEditor(SecretText.of("typed")))
        assertSecret("own entry ignored", WizardRules.DEFAULT_SERVER_SECRET, draft.secretValue)
        assertTrue(draft.secretIsDefault())
    }

    /** WizardModel.ContractsAndResumePreserveDraft (draft part) */
    @Test
    fun contractsAndResumePreserveDraft() {
        val view = modelView()
        val draft = loaded(view)
        draft.setAutoDetectIp(false, null)
        draft.setIp("10.99.99.20")
        assertEquals(14uL, draft.contractCount)
        assertTrue(draft.setCreateContracts(false))
        assertEquals(0uL, draft.contractCount)
        // Interrupted by Connect, then resumed in the same session with a new epoch.
        assertFalse(draft.load(modelView(epoch = 2uL), preserveFields = true, newSession = false))
        assertEquals("10.99.99.20", draft.virtualIp)
        assertFalse(draft.createContracts)

        val repair = modelView(mode = WizardMode.REPAIR, contractCount = 7uL)
        draft.load(repair, preserveFields = false, newSession = false)
        assertTrue(draft.contractsAllowed)
        assertEquals(7uL, draft.contractCount)

        val unsupported = modelView(mode = WizardMode.RESET, vpnCidr = "192.168.0.0/24")
        draft.load(unsupported, preserveFields = false, newSession = false)
        assertFalse(draft.contractsAllowed)
        assertFalse(draft.setCreateContracts(true))
        assertEquals(0uL, draft.contractCount)
    }

    /** Suggestion after the secret check: only with automatic detection, and the default key follows. */
    @Test
    fun suggestionAppliesOnlyWithAutomaticDetection() {
        val draft = loaded()
        assertTrue(draft.applySuggestion("10.99.99.10"))
        assertEquals("10.99.99.10", draft.virtualIp)
        assertSecret("key follows the suggested IP", WizardRules.defaultAccessKey("10.99.99.10").reveal(), draft.accessKeyText)
        assertEquals(13uL, draft.contractCount)
        assertFalse("same suggestion is no change", draft.applySuggestion("10.99.99.10"))

        val manual = loaded()
        manual.setAutoDetectIp(false, null)
        manual.setIp("10.99.99.7")
        assertFalse(manual.applySuggestion("10.99.99.10"))
        assertEquals("10.99.99.7", manual.virtualIp)
        // Switching automatic detection on takes the still valid suggestion.
        manual.setAutoDetectIp(true, "10.99.99.10")
        assertEquals("10.99.99.10", manual.virtualIp)
        assertNull("the field is disabled while automatic", manual.setIp("10.99.99.8"))
    }

    /** `validateCurrent` per step, with the desktop error codes. */
    @Test
    fun localValidationPerStep() {
        val draft = loaded()
        assertNull(draft.validate(DraftStep.SERVER_URL, null))
        draft.setUrl("http://MODIFY-THIS-URL/phpvpn.php")
        assertEquals(LocalProblem.URL_PLACEHOLDER, draft.validate(DraftStep.SERVER_URL, null))
        for (bad in listOf("", "ftp://host/phpvpn.php", "https://user@host/p.php", "https://host/p.php?x=1", "https://host/p.php#f", "https://host:0/")) {
            draft.setUrl(bad)
            assertEquals(bad, LocalProblem.URL_INVALID, draft.validate(DraftStep.SERVER_URL, null))
        }
        draft.setUrl("https://vpn.example.com:8443/relay.php")
        assertNull(draft.validate(DraftStep.SERVER_URL, null))

        assertEquals(LocalProblem.NO_SUGGESTION, draft.validate(DraftStep.IDENTITY, null))
        draft.applySuggestion("10.99.99.10")
        assertNull(draft.validate(DraftStep.IDENTITY, "10.99.99.10"))
        assertEquals("a stale suggestion is not the verified one", LocalProblem.NO_SUGGESTION, draft.validate(DraftStep.IDENTITY, "10.99.99.11"))
        draft.setAutoDetectIp(false, null)
        draft.setIp("10.99.99.255")
        assertEquals(LocalProblem.IP_INVALID, draft.validate(DraftStep.IDENTITY, null))
        draft.setIp("10.99.99.1")
        assertEquals("first host address is reserved", LocalProblem.IP_INVALID, draft.validate(DraftStep.IDENTITY, null))
        draft.setIp("10.99.99.9")
        draft.setDefaultKey(false)
        draft.setAccessKey(SecretText.of("A".repeat(64)))
        assertEquals("no automatic lowercasing", LocalProblem.KEY_INVALID, draft.validate(DraftStep.IDENTITY, null))
        draft.setAccessKey(SecretText.of(" " + "a".repeat(63)))
        assertEquals("no trimming", LocalProblem.KEY_INVALID, draft.validate(DraftStep.IDENTITY, null))
        draft.setAccessKey(SecretText.of("a".repeat(64)))
        assertNull(draft.validate(DraftStep.IDENTITY, null))
    }

    @Test
    fun loadErrorViewHasNoFieldsAndNoContext() {
        val draft = WizardDraft()
        val view = modelView().copy(fields = null, context = null)
        assertTrue(draft.load(view, preserveFields = false, newSession = true))
        assertFalse(draft.hasContext)
        assertEquals(LocalProblem.NO_CONTEXT, draft.validate(DraftStep.SERVER_URL, null))
    }

    @Test
    fun toFieldsCopiesSecretsAndWipeClearsTheDraft() {
        val draft = loaded(modelView(secret = "synthetic"))
        val fields = draft.toFields()
        fields.wipe()
        assertSecret("the draft keeps its own copy", "synthetic", draft.secretValue)
        draft.wipe()
        assertTrue(draft.secretValue.isEmpty())
        assertTrue(draft.accessKeyText.isEmpty())
        assertEquals("", draft.serverUrl)
    }
}
