package phpvpn.android

import java.io.File
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Cheap lint for the WP07 rules (plan-android-v001, R7 and texts):
 * - no colour literal outside the theme package, so a dark theme is only a
 *   second token set;
 * - no user-visible string literal passed to Text() in the UI sources; texts
 *   come from resources.
 */
class UiSourceRulesTest {

    private val sources = listOf(File("src/main/kotlin"), File("src/debug/kotlin"))
        .flatMap { root -> root.walkTopDown().filter { it.isFile && it.extension == "kt" }.toList() }
    private val themeDir = File("src/main/kotlin/phpvpn/android/ui/theme").absoluteFile

    @Test
    fun sourcesAreFound() {
        assertTrue("no Kotlin sources under src/main/kotlin", sources.any { it.path.contains("ui") })
    }

    @Test
    fun colourLiteralsOnlyInTheTheme() {
        val literal = Regex("""Color\(\s*0x|Color\(\s*red\s*=|Color\.(White|Black|Red|Green|Blue|Gray|DarkGray|LightGray|Yellow|Cyan|Magenta)\b|Color\.parseColor\(""")
        val offenders = sources
            .filterNot { it.absoluteFile.startsWith(themeDir) }
            .flatMap { file -> matches(file, literal) }
        assertTrue("colour literals outside ui/theme:\n" + offenders.joinToString("\n"), offenders.isEmpty())
    }

    @Test
    fun noHardCodedTextInComposables() {
        // Text("…") with a letter inside the literal; data such as "9:41" in
        // the debug catalog is allowed because it contains no words.
        val literalText = Regex("""\b(Text|Hint|Intro|Heading|Eyebrow|Title|MetaLabel)\(\s*"[^"]*\p{L}{2,}""")
        val offenders = sources.flatMap { file -> matches(file, literalText) }
        assertTrue("hard-coded texts:\n" + offenders.joinToString("\n"), offenders.isEmpty())
    }

    private fun matches(file: File, regex: Regex): List<String> =
        file.readLines().mapIndexedNotNull { index, line ->
            val code = line.substringBefore("//")
            if (regex.containsMatchIn(code)) "${file.path}:${index + 1}: ${line.trim()}" else null
        }
}
