package phpvpn.android

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element

/**
 * T-J01 for the WP08 additions: values/strings_wp08.xml and
 * values-cs/strings_wp08.xml have the same keys and format arguments, define
 * no key of strings.xml again and contain only translatable, non-empty texts.
 * (StringResourcesTest covers strings.xml only.)
 */
class Wp08StringResourcesTest {
    private val res = File("src/main/res")
    private val english = parse(File(res, "values/strings_wp08.xml"))
    private val czech = parse(File(res, "values-cs/strings_wp08.xml"))

    @Test
    fun bothLanguagesHaveTheSameKeys() {
        assertTrue("keys missing in values-cs: ${english.keys - czech.keys}", (english.keys - czech.keys).isEmpty())
        assertTrue("keys missing in values: ${czech.keys - english.keys}", (czech.keys - english.keys).isEmpty())
        assertTrue("no WP08 strings found", english.size > 30)
    }

    @Test
    fun placeholdersMatchPerKey() {
        val bad = english.keys.filter { placeholders(english.getValue(it)) != placeholders(czech.getValue(it)) }
        assertTrue("placeholder mismatches: $bad", bad.isEmpty())
    }

    @Test
    fun noKeyOfTheBaseFilesIsDefinedAgain() {
        val base = listOf("values/strings.xml", "values/errors.xml", "values/symbols.xml")
            .flatMap { parse(File(res, it)).keys }
            .toSet()
        val duplicates = english.keys.intersect(base)
        assertTrue("duplicated keys: $duplicates", duplicates.isEmpty())
    }

    @Test
    fun textsAreNonEmptyAndTranslatable() {
        val empty = (english + czech).filterValues { it.isBlank() }.keys
        assertTrue("empty texts: $empty", empty.isEmpty())
        for (file in listOf("values/strings_wp08.xml", "values-cs/strings_wp08.xml")) {
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
