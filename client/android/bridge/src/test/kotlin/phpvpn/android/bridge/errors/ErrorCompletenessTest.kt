package phpvpn.android.bridge.errors

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.Fixtures
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.model.ProgressModel
import phpvpn.android.bridge.model.ProgressStage
import phpvpn.android.bridge.model.TrayStatus

/**
 * Completeness of the error mapping against the desktop sources: every
 * ErrorCode of client/src/core/base/error.h (parsed here), its wire name and
 * recoverable flag in error.cpp, every public connection code produced by
 * status_snapshot.cpp and the progress allowlist of progress_model.cpp.
 */
class ErrorCompletenessTest {
    private data class NativeError(val identifier: String, val wireName: String, val recoverable: Boolean)

    private val nativeErrors: List<NativeError> by lazy {
        val header = Fixtures.source("src/core/base/error.h")
        val body = Regex("""enum class ErrorCode \{(.*?)\};""", RegexOption.DOT_MATCHES_ALL).find(header)!!.groupValues[1]
        val identifiers = body.lines().map { it.substringBefore("//").trim().trimEnd(',').substringBefore('=').trim() }.filter { it.isNotEmpty() }
        val table = Regex("""\{ErrorCode::(\w+),\s*"([^"]+)",\s*"(?:[^"\\]|\\.)*",\s*(true|false)\}""")
            .findAll(Fixtures.source("src/core/base/error.cpp")).associate { it.groupValues[1] to (it.groupValues[2] to (it.groupValues[3] == "true")) }
        identifiers.map { id ->
            val (wire, recoverable) = table[id] ?: error("error.cpp has no entry for $id")
            NativeError(id, wire, recoverable)
        }
    }

    @Test
    fun everyErrorCodeOfErrorHeaderHasTheSameKotlinCode() {
        assertTrue(nativeErrors.size >= 91)
        for (native in nativeErrors) {
            val code = ErrorCode.valueOf(native.identifier.uppercase())
            assertEquals(native.identifier, native.wireName, code.wireName)
            assertEquals(native.identifier, native.recoverable, code.recoverable)
            assertEquals(code, ErrorCode.fromWire(native.wireName))
        }
        // Kotlin has nothing else except the documented Android additions.
        val extra = ErrorCode.entries.map { it.name.lowercase() }.toSet() - nativeErrors.map { it.identifier }.toSet()
        assertTrue(extra.toString(), extra.all { ErrorCode.valueOf(it.uppercase()) in ErrorCode.ANDROID_ADDITIONS })
        // The order follows error.h, so a new desktop code lands at the same place.
        val inHeader = ErrorCode.entries.filter { it !in ErrorCode.ANDROID_ADDITIONS }.map { it.name.lowercase() }
        assertEquals(nativeErrors.map { it.identifier }, inHeader)
    }

    @Test
    fun everyErrorCodeHasItsOwnMessageKey() {
        val keys = ErrorCode.entries.map { it.messageKey }
        assertEquals(keys.size, keys.toSet().size)
        for (code in ErrorCode.entries) {
            assertEquals("error_${code.wireName}", code.messageKey.resourceName)
            assertEquals(code.messageKey, ErrorTexts.messageKey(code.wireName))
        }
        assertEquals(ErrorTextKey.ERROR_UNKNOWN, ErrorTexts.messageKey("some_future_code"))
        assertEquals(ErrorTextKey.ERROR_UNKNOWN, ErrorTexts.messageKey(null))
        // Resource names are unique and valid Android identifiers.
        val names = ErrorTextKey.entries.map { it.resourceName }
        assertEquals(names.size, names.toSet().size)
        assertTrue(names.all { Regex("[a-z][a-z0-9_]*").matches(it) })
    }

    @Test
    fun everyPublicConnectionCodeOfStatusSnapshotIsMapped() {
        val produced = Regex("""return \{"([a-z_]+)"""").findAll(Fixtures.source("src/core/runtime/status_snapshot.cpp"))
            .map { it.groupValues[1] }.toSet()
        assertTrue(produced.size >= 9)
        for (code in produced) {
            val connection = ConnectionErrorCode.fromWire(code)
            assertTrue(code, connection != null)
            assertEquals(connection!!.message, ErrorTexts.messageKey(code).takeIf { ErrorCode.fromWire(code) == null } ?: connection.message)
            for (key in listOf(connection.title, connection.message, connection.action)) assertTrue(key.resourceName.startsWith("error_connection_$code"))
        }
    }

    @Test
    fun progressAllowlistMatchesProgressModelCpp() {
        val cpp = Fixtures.source("apps/tray/progress_model.cpp")
        val allowlist = Regex("""\{\{?"([a-z_]+)",""").findAll(cpp).map { it.groupValues[1] }.toList()
        assertEquals(allowlist, ErrorTexts.progressCodes)
        for (code in allowlist) assertEquals(code, ErrorTexts.progressErrorText(code).code)
        // Unknown codes cannot inject text or claim key rejection.
        for (code in listOf("connection_failed", "busy", "", "access_key_rejected_x")) {
            assertEquals("communication_failed", ErrorTexts.progressErrorText(code).code)
        }
    }

    @Test
    fun everyErrorCodeAsLastErrorFailsTheProgressWithAnAllowlistedText() {
        val codes = ErrorCode.entries.map { it.wireName } + ConnectionErrorCode.entries.map { it.wireName }
        for (code in codes) {
            val model = ProgressModel()
            model.onSnapshot(TrayStatus(valid = true, state = ConnectionState.ERROR, hasError = true, errorCode = code, errorDescription = "RAW $code"), 0)
            assertEquals(ProgressStage.FAILED, model.view.stage)
            val expected = if (code in ErrorTexts.progressCodes) code else "communication_failed"
            assertEquals(code, expected, model.view.errorCode)
            assertNotEquals(null, model.view.errorText)
            assertTrue(!model.view.toString().contains("RAW"))
        }
    }

    @Test
    fun wizardActionKeysFollowSafeErrorOfWizardIpc() {
        // Every branch of WizardIpc::safeError (wizard_ipc.cpp), by code.
        val expected = mapOf(
            "config_directory_security_invalid" to ErrorTextKey.ACTION_CONFIG_DIRECTORY_SECURITY,
            "config_write_failed" to ErrorTextKey.ACTION_CONFIG_CHECK,
            "journal_missing" to ErrorTextKey.ACTION_CONFIG_CHECK,
            "server_unauthorized" to ErrorTextKey.ACTION_SERVER_UNAUTHORIZED,
            "busy" to ErrorTextKey.ACTION_BUSY,
            "wizard_session_expired" to ErrorTextKey.ACTION_SESSION_EXPIRED,
            "wizard_commit_unknown" to ErrorTextKey.ACTION_COMMIT_UNKNOWN,
            "wizard_connection_interrupted" to ErrorTextKey.ACTION_CONNECTION_INTERRUPTED,
            "wizard_contract_self_conflict" to ErrorTextKey.ACTION_CONTRACT_SELF_CONFLICT,
            "wizard_unsupported" to ErrorTextKey.ACTION_UNSUPPORTED,
            "server_version_incompatible" to ErrorTextKey.ACTION_SERVER_VERSION_INCOMPATIBLE,
            "crypto_tag_mismatch" to ErrorTextKey.ACTION_VERIFY_AGAIN,
        )
        for ((code, key) in expected) assertEquals(code, key, ErrorTexts.wizardActionKey(code, null))
        assertEquals(ErrorTextKey.ACTION_CONFIG_CHECK, ErrorTexts.wizardActionKey("config_invalid_value", "configuration"))
        assertEquals(ErrorTextKey.ACTION_VERIFY_AGAIN, ErrorTexts.wizardActionKey("config_invalid_address", "virtualIp"))
        for (code in ErrorCode.entries) ErrorTexts.wizardActionKey(code.wireName, null) // total over all codes
    }
}
