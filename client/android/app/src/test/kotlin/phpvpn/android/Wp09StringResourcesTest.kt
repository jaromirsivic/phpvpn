package phpvpn.android

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element

/**
 * T-J01 for the WP09 texts: values/strings_wp09.xml (EN) and
 * values-cs/strings_wp09.xml (CS) have exactly the same keys, every key is
 * prefixed `wp09_` (no clash with strings.xml of WP07 or the files of WP08
 * and WP10), the format arguments match per key, no text is empty, and every
 * key is used by the WP09 sources.
 */
class Wp09StringResourcesTest {
    private val res = File("src/main/res")
    private val english = parse(File(res, "values/strings_wp09.xml"))
    private val czech = parse(File(res, "values-cs/strings_wp09.xml"))

    @Test
    fun bothLanguagesHaveTheSameKeys() {
        assertEquals("keys missing in values-cs", emptySet<String>(), english.keys - czech.keys)
        assertEquals("keys missing in values", emptySet<String>(), czech.keys - english.keys)
        assertTrue(english.isNotEmpty())
    }

    @Test
    fun everyKeyHasTheWp09Prefix() {
        val foreign = english.keys.filterNot { it.startsWith("wp09_") }
        assertTrue("keys without the wp09_ prefix: $foreign", foreign.isEmpty())
    }

    @Test
    fun placeholdersMatchAndTextsAreNotEmpty() {
        val mismatches = english.keys.mapNotNull { key ->
            val en = placeholders(english.getValue(key))
            val cs = placeholders(czech.getValue(key))
            if (en == cs) null else "$key: $en vs $cs"
        }
        assertTrue("placeholder mismatches: $mismatches", mismatches.isEmpty())
        val empty = (english + czech).filterValues { it.isBlank() }.keys
        assertTrue("empty texts: $empty", empty.isEmpty())
    }

    @Test
    fun everyKeyIsUsed() {
        val sources = File("src/main/kotlin/phpvpn/android/ui/screens").walkTopDown()
            .filter { it.isFile && it.extension == "kt" }
            .joinToString("\n") { it.readText() }
        val unused = english.keys.filterNot { sources.contains("R.string.$it") }
        assertTrue("unused WP09 texts: $unused", unused.isEmpty())
    }

    private fun parse(file: File): Map<String, String> {
        val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(file)
        val result = linkedMapOf<String, String>()
        val children = document.documentElement.childNodes
        for (i in 0 until children.length) {
            val node = children.item(i) as? Element ?: continue
            check(node.tagName == "string") { "only <string> in $file, found ${node.tagName}" }
            check(result.put(node.getAttribute("name"), node.textContent) == null) { "duplicate ${node.getAttribute("name")} in $file" }
        }
        return result
    }

    private fun placeholders(text: String): Set<String> =
        Regex("""%(\d+)\$([a-zA-Z])""").findAll(text.replace("%%", "")).map { "${it.groupValues[1]}:${it.groupValues[2]}" }.toSet()
}
