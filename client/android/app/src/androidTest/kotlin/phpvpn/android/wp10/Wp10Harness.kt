package phpvpn.android.wp10

import android.graphics.Bitmap
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.annotation.StringRes
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.test.SemanticsNodeInteraction
import androidx.compose.ui.test.hasAnyAncestor
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.navigation.NavHostController
import androidx.navigation.compose.rememberNavController
import androidx.test.ext.junit.rules.ActivityScenarioRule
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.repository.ConnectionRepository
import phpvpn.android.ui.navigation.PhpvpnNavHost
import phpvpn.android.ui.screens.ConfigFlowDependencies
import phpvpn.android.ui.screens.LocalConfigFlowDependencies
import phpvpn.android.ui.theme.PhpvpnTheme

typealias ComposeRule = AndroidComposeTestRule<ActivityScenarioRule<ComponentActivity>, ComponentActivity>

/**
 * The real navigation graph of the app over a scripted [FakeIpcTransport]:
 * WP10 screens get their dependencies through [LocalConfigFlowDependencies].
 */
class Wp10Harness(private val compose: ComposeRule) {
    val transport = FakeIpcTransport()
    lateinit var nav: NavHostController
        private set

    fun launch(): Wp10Harness {
        val client = IpcClient(transport)
        val deps = ConfigFlowDependencies(client, ConnectionRepository(client, CoroutineScope(SupervisorJob() + Dispatchers.Default)))
        compose.setContent {
            PhpvpnTheme {
                CompositionLocalProvider(LocalConfigFlowDependencies provides deps) {
                    nav = rememberNavController()
                    PhpvpnNavHost(clientVersion = { "test" }, navController = nav)
                }
            }
        }
        compose.waitForIdle()
        return this
    }

    fun navigate(action: NavHostController.() -> Unit) {
        compose.runOnUiThread { nav.action() }
        compose.waitForIdle()
    }

    fun text(@StringRes id: Int, vararg args: Any): String = compose.activity.getString(id, *args)

    fun plural(id: Int, count: Int, vararg args: Any): String = compose.activity.resources.getQuantityString(id, count, *args)

    /**
     * Hides the soft keyboard and lets the window settle, so a following
     * click does not land while the footer moves with the IME inset.
     */
    fun hideKeyboard() {
        compose.runOnUiThread {
            val window = compose.activity.window
            WindowCompat.getInsetsController(window, window.decorView).hide(WindowInsetsCompat.Type.ime())
        }
        compose.waitForIdle()
        Thread.sleep(SETTLE_MS)
        compose.waitForIdle()
    }

    /** Waits until a node with exactly [value] exists (a screen change after an IPC result). */
    fun waitForText(value: String, timeoutMs: Long = WAIT_MS) =
        compose.waitUntil(timeoutMs) { compose.onAllNodes(hasText(value)).fetchSemanticsNodes().isNotEmpty() }

    /** Waits until the node tagged [tag] shows [value] (the draft count arrives with the next model revision). */
    fun waitForTagText(tag: String, value: String, timeoutMs: Long = WAIT_MS) =
        compose.waitUntil(timeoutMs) { compose.onAllNodes(hasTestTag(tag) and hasText(value)).fetchSemanticsNodes().isNotEmpty() }

    /** Waits until a node tagged [tag] exists. */
    fun waitForTag(tag: String, timeoutMs: Long = WAIT_MS) =
        compose.waitUntil(timeoutMs) { compose.onAllNodes(hasTestTag(tag)).fetchSemanticsNodes().isNotEmpty() }

    /** The editable text node inside a field wrapped by [tag]. */
    fun field(tag: String): SemanticsNodeInteraction = compose.onNode(hasSetTextAction() and hasAnyAncestor(hasTestTag(tag)))

    val secure: Boolean
        get() {
            var flags = 0
            compose.runOnUiThread { flags = compose.activity.window.attributes.flags }
            return flags and WindowManager.LayoutParams.FLAG_SECURE != 0
        }

    /**
     * Screenshot for the visual comparison with previews/full (T-U02). A
     * FLAG_SECURE window would capture black, so the flag is lifted only for
     * the capture and restored at once; the screens' own flag was asserted
     * before. Files go to the app's external files dir under wp10/.
     */
    fun screenshot(name: String) {
        // The design shows no keyboard: hide it before the capture.
        hideKeyboard()
        val wasSecure = secure
        if (wasSecure) compose.runOnUiThread { compose.activity.window.clearFlags(WindowManager.LayoutParams.FLAG_SECURE) }
        compose.waitForIdle()
        Thread.sleep(SETTLE_MS)
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val bitmap: Bitmap? = instrumentation.uiAutomation.takeScreenshot()
        if (wasSecure) compose.runOnUiThread { compose.activity.window.addFlags(WindowManager.LayoutParams.FLAG_SECURE) }
        bitmap ?: return
        val configuration = compose.activity.resources.configuration
        val dir = File(instrumentation.targetContext.getExternalFilesDir(null), "wp10").apply { mkdirs() }
        File(dir, "$name-w${configuration.screenWidthDp}-f${configuration.fontScale}.png").outputStream().use {
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)
        }
    }

    private companion object {
        const val SETTLE_MS = 400L
        const val WAIT_MS = 10_000L
    }
}
