package phpvpn.android

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element

/**
 * T-J01 for the WP10 texts: values/strings_wp10.xml and
 * values-cs/strings_wp10.xml have the same keys and the same positional
 * format arguments, no empty text, and no key of strings.xml is redefined.
 */
class Wp10StringResourcesTest {
    private val res = File("src/main/res")
    private val english = parse(File(res, "values/strings_wp10.xml"))
    private val czech = parse(File(res, "values-cs/strings_wp10.xml"))

    @Test
    fun bothLanguagesHaveTheSameKeys() {
        assertTrue("keys missing in values-cs: ${english.keys - czech.keys}", (english.keys - czech.keys).isEmpty())
        assertTrue("keys missing in values: ${czech.keys - english.keys}", (czech.keys - english.keys).isEmpty())
        assertTrue(english.isNotEmpty())
    }

    @Test
    fun placeholdersMatchAndNoTextIsEmpty() {
        for (key in english.keys) {
            assertEquals(key, placeholders(english.getValue(key)), placeholders(czech.getValue(key)))
            assertTrue(key, english.getValue(key).isNotBlank() && czech.getValue(key).isNotBlank())
        }
    }

    @Test
    fun noKeyOfStringsXmlIsRedefined() {
        val shared = parse(File(res, "values/strings.xml")).keys
        assertTrue("redefined: ${english.keys.intersect(shared)}", english.keys.intersect(shared).isEmpty())
    }

    private fun parse(file: File): Map<String, String> {
        val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(file)
        val nodes = document.documentElement.childNodes
        return (0 until nodes.length).mapNotNull { nodes.item(it) as? Element }
            .filter { it.tagName == "string" }
            .associate { it.getAttribute("name") to it.textContent }
    }

    private fun placeholders(text: String): Set<String> =
        Regex("""%(\d+)\$[-#+ 0,(]*\d*(?:\.\d+)?([a-zA-Z])""").findAll(text.replace("%%", "")).map { it.value }.toSet()
}
