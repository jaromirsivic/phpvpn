package phpvpn.android

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element

/**
 * T-J01 (texts): values/strings.xml (EN) and values-cs/strings.xml (CS) have
 * the same keys, and every key has the same format arguments in both
 * languages and in every plural quantity. Language-neutral strings belong in
 * symbols.xml, not in strings.xml.
 */
class StringResourcesTest {

    private val res = File("src/main/res")
    private val english = parse(File(res, "values/strings.xml"))
    private val czech = parse(File(res, "values-cs/strings.xml"))

    @Test
    fun bothLanguagesHaveTheSameKeys() {
        val onlyEnglish = english.keys - czech.keys
        val onlyCzech = czech.keys - english.keys
        assertTrue("keys missing in values-cs: $onlyEnglish", onlyEnglish.isEmpty())
        assertTrue("keys missing in values: $onlyCzech", onlyCzech.isEmpty())
    }

    @Test
    fun placeholdersMatchPerKey() {
        val mismatches = english.keys.intersect(czech.keys).mapNotNull { key ->
            val en = english.getValue(key)
            val cs = czech.getValue(key)
            val all = en.texts + cs.texts
            val expected = placeholders(all.first())
            val bad = all.filter { placeholders(it) != expected }
            if (bad.isEmpty()) null else "$key: expected $expected, got ${bad.map(::placeholders)}"
        }
        assertTrue("placeholder mismatches:\n" + mismatches.joinToString("\n"), mismatches.isEmpty())
    }

    @Test
    fun pluralsDefineTheQuantitiesOfTheirLanguage() {
        english.filterValues { it.plural }.forEach { (key, value) ->
            assertEquals("EN plural $key", setOf("one", "other"), value.quantities)
        }
        czech.filterValues { it.plural }.forEach { (key, value) ->
            assertEquals("CS plural $key", setOf("one", "few", "many", "other"), value.quantities)
        }
    }

    @Test
    fun stringsFilesContainOnlyTranslatableTexts() {
        val untranslatable = english.filterValues { !it.translatable }.keys + czech.filterValues { !it.translatable }.keys
        assertTrue("move to symbols.xml: $untranslatable", untranslatable.isEmpty())
        val empty = (english + czech).filterValues { value -> value.texts.any { it.isBlank() } }.keys
        assertTrue("empty texts: $empty", empty.isEmpty())
    }

    @Test
    fun everyScreenOfTheDesignHasAGroup() {
        // Comments name the screen ids 00-48 and 36a (android-screen-map.md).
        val source = File(res, "values/strings.xml").readText()
        val ids = (0..48).map { "%02d".format(it) } + "36a"
        val missing = ids.filterNot { Regex("<!-- $it ").containsMatchIn(source) }
        assertTrue("screens without a string group: $missing", missing.isEmpty())
    }

    private data class Entry(val texts: List<String>, val quantities: Set<String>, val plural: Boolean, val translatable: Boolean)

    private fun parse(file: File): Map<String, Entry> {
        val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(file)
        val result = linkedMapOf<String, Entry>()
        val children = document.documentElement.childNodes
        for (i in 0 until children.length) {
            val node = children.item(i) as? Element ?: continue
            val name = node.getAttribute("name")
            val translatable = node.getAttribute("translatable") != "false"
            val entry = when (node.tagName) {
                "string" -> Entry(listOf(node.textContent), emptySet(), plural = false, translatable)
                "plurals" -> {
                    val items = node.getElementsByTagName("item")
                    val texts = (0 until items.length).map { items.item(it).textContent }
                    val quantities = (0 until items.length).map { (items.item(it) as Element).getAttribute("quantity") }.toSet()
                    Entry(texts, quantities, plural = true, translatable)
                }
                "string-array" -> {
                    val items = node.getElementsByTagName("item")
                    Entry((0 until items.length).map { items.item(it).textContent }, emptySet(), plural = false, translatable)
                }
                else -> continue
            }
            check(result.put(name, entry) == null) { "duplicate key $name in $file" }
        }
        return result
    }

    /** Positional format arguments as "index:conversion", e.g. {"1:s", "2:d"}. */
    private fun placeholders(text: String): Set<String> {
        val withoutEscapes = text.replace("%%", "")
        val found = Regex("""%(\d+)\$[-#+ 0,(]*\d*(?:\.\d+)?([a-zA-Z])""").findAll(withoutEscapes)
            .map { "${it.groupValues[1]}:${it.groupValues[2]}" }.toSet()
        // Unnumbered arguments would break as soon as a translation reorders them.
        val unnumbered = Regex("""%(?!\d+\$)[-#+ 0,(]*\d*(?:\.\d+)?[a-zA-Z]""").containsMatchIn(withoutEscapes)
        return if (unnumbered) found + "unnumbered" else found
    }
}
