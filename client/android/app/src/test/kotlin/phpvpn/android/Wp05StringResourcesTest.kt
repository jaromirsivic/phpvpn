package phpvpn.android

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element

/**
 * T-J01 for the WP05 additions: values/strings_wp05.xml and
 * values-cs/strings_wp05.xml have the same keys and format arguments, define
 * no key of the other string files again and contain only translatable,
 * non-empty texts.
 */
class Wp05StringResourcesTest {
    private val res = File("src/main/res")
    private val english = parse(File(res, "values/strings_wp05.xml"))
    private val czech = parse(File(res, "values-cs/strings_wp05.xml"))

    @Test
    fun bothLanguagesHaveTheSameKeys() {
        assertTrue("keys missing in values-cs: ${english.keys - czech.keys}", (english.keys - czech.keys).isEmpty())
        assertTrue("keys missing in values: ${czech.keys - english.keys}", (czech.keys - english.keys).isEmpty())
        assertTrue("no WP05 strings found", english.size >= 10)
    }

    @Test
    fun placeholdersMatchPerKey() {
        val bad = english.keys.filter { placeholders(english.getValue(it)) != placeholders(czech.getValue(it)) }
        assertTrue("placeholder mismatches: $bad", bad.isEmpty())
    }

    @Test
    fun noKeyOfTheOtherFilesIsDefinedAgain() {
        val others = res.resolve("values").listFiles { file -> file.name.endsWith(".xml") && file.name != "strings_wp05.xml" }.orEmpty()
            .flatMap { parse(it).keys }
            .toSet()
        val duplicates = english.keys.intersect(others)
        assertTrue("duplicated keys: $duplicates", duplicates.isEmpty())
    }

    @Test
    fun textsAreNonEmptyAndTranslatable() {
        val empty = (english + czech).filterValues { it.isBlank() }.keys
        assertTrue("empty texts: $empty", empty.isEmpty())
        for (file in listOf("values/strings_wp05.xml", "values-cs/strings_wp05.xml")) {
            assertTrue("$file must not contain translatable=false", !File(res, file).readText().contains("translatable=\"false\""))
        }
    }

    private fun placeholders(text: String): List<String> = Regex("""%(\d+\$)?[sd]""").findAll(text).map { it.value }.sorted().toList()

    private fun parse(file: File): Map<String, String> {
        val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(file)
        val result = linkedMapOf<String, String>()
        val children = document.documentElement.childNodes
        for (i in 0 until children.length) {
            val node = children.item(i) as? Element ?: continue
            result[node.getAttribute("name")] = node.textContent
        }
        return result
    }
}
