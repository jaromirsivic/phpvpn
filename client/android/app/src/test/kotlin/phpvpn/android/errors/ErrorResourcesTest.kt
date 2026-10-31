package phpvpn.android.errors

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element
import phpvpn.android.bridge.errors.ConnectionErrorCode
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.errors.ErrorTextKey

/**
 * T-J01 (error texts): values/errors.xml and values-cs/errors.xml define the
 * same keys, exactly the keys of [ErrorTextKey], every ErrorCode of
 * client/src/core/base/error.h has a text in both languages, and
 * [ErrorStrings] resolves every key to its own resource.
 */
class ErrorResourcesTest {
    private val res = File("src/main/res")
    private val english = parse(File(res, "values/errors.xml"))
    private val czech = parse(File(res, "values-cs/errors.xml"))

    private fun parse(file: File): Map<String, String> {
        val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(file)
        val strings = document.documentElement.getElementsByTagName("string")
        return (0 until strings.length).associate {
            val element = strings.item(it) as Element
            element.getAttribute("name") to element.textContent
        }
    }

    @Test
    fun bothLanguagesHaveTheSameErrorKeys() {
        assertEquals(english.keys, czech.keys)
        assertEquals(ErrorTextKey.entries.map { it.resourceName }.toSet(), english.keys)
        val empty = (english + czech.mapKeys { "cs:" + it.key }).filterValues { it.isBlank() }.keys
        assertTrue("empty texts: $empty", empty.isEmpty())
        // Czech is a translation, not a copy (identifiers like SERVER_SECRET may repeat).
        val copied = english.filter { (key, text) -> czech[key] == text && text.length > 20 }.keys
        assertTrue("untranslated: $copied", copied.isEmpty())
    }

    @Test
    fun everyErrorCodeOfErrorHeaderHasAnEnglishAndCzechText() {
        val header = File(clientRoot(), "src/core/base/error.h").readText()
        val body = Regex("""enum class ErrorCode \{(.*?)\};""", RegexOption.DOT_MATCHES_ALL).find(header)!!.groupValues[1]
        val identifiers = body.lines().map { it.substringBefore("//").trim().trimEnd(',').substringBefore('=').trim() }
            .filter { it.isNotEmpty() }
        assertTrue(identifiers.size >= 91)
        for (identifier in identifiers) {
            val key = ErrorCode.valueOf(identifier.uppercase()).messageKey.resourceName
            assertTrue("$identifier EN", english[key]?.isNotBlank() == true)
            assertTrue("$identifier CS", czech[key]?.isNotBlank() == true)
        }
        for (code in ConnectionErrorCode.entries) {
            for (key in listOf(code.title, code.message, code.action)) assertTrue(key.resourceName, key.resourceName in english)
        }
    }

    @Test
    fun errorStringsResolveEveryKeyToItsOwnResource() {
        val ids = ErrorTextKey.entries.map { ErrorStrings.resource(it) }
        assertEquals(ids.size, ids.toSet().size)
    }

    @Test
    fun textsDoNotUseDesktopOnlyWording() {
        // Android has no service to restart and no tray; config_directory_security_invalid points to the app.
        val all = english.values + czech.values
        for (forbidden in listOf("tray", "Windows", "ProgramData", "Edit Config", "administrator to restart")) {
            assertTrue(forbidden, all.none { it.contains(forbidden) })
        }
        assertTrue(english.getValue("error_action_config_directory_security").contains("Restart the app"))
    }

    private fun clientRoot(): File {
        var dir: File? = File("").absoluteFile
        while (dir != null && !File(dir, "src/core/base/error.h").isFile) dir = dir.parentFile
        return dir ?: error("client/src/core/base/error.h not found")
    }
}
