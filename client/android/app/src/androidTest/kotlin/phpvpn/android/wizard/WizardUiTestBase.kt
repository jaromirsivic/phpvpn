package phpvpn.android.wizard

import androidx.activity.ComponentActivity
import androidx.annotation.StringRes
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.test.SemanticsNodeInteraction
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.navigation.NavHostController
import androidx.navigation.compose.rememberNavController
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import org.junit.After
import org.junit.Rule
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.repository.ConnectionRepository
import phpvpn.android.debug.FakeWizardService
import phpvpn.android.ui.navigation.PhpvpnNavHost
import phpvpn.android.ui.screens.secure.SecureWindowFlags
import phpvpn.android.ui.screens.wizard.LocalWizardDependencies
import phpvpn.android.ui.screens.wizard.WizardDependencies
import phpvpn.android.ui.theme.PhpvpnTheme

/**
 * The real navigation graph of the app over the scripted
 * [FakeWizardService]: every screen, the back stack and the window flags
 * behave as in the app, only the IPC replies are synthetic.
 */
abstract class WizardUiTestBase {
    @get:Rule
    val compose = createAndroidComposeRule<ComponentActivity>()

    lateinit var service: FakeWizardService
    lateinit var nav: NavHostController
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    @After
    fun stopScope() = scope.cancel()

    fun launch(start: Any, configure: FakeWizardService.() -> Unit = {}) {
        service = FakeWizardService().apply(configure)
        val client = IpcClient(service.transport)
        val dependencies = WizardDependencies(client, scope, ConnectionRepository(client, scope))
        compose.setContent {
            PhpvpnTheme {
                CompositionLocalProvider(LocalWizardDependencies provides dependencies) {
                    nav = rememberNavController()
                    PhpvpnNavHost(clientVersion = { "1.0.3" }, navController = nav)
                }
            }
        }
        compose.runOnUiThread { nav.navigate(start) }
        compose.waitForIdle()
    }

    fun text(@StringRes id: Int, vararg args: Any): String = compose.activity.getString(id, *args)

    /** Waits until a node with exactly [id]'s text exists (a screen change after a real result). */
    fun waitForText(@StringRes id: Int, timeoutMs: Long = 10_000) = waitForText(text(id), timeoutMs)

    fun waitForText(value: String, timeoutMs: Long = 10_000, substring: Boolean = false) {
        compose.waitUntil(timeoutMs) {
            compose.onAllNodes(hasText(value, substring = substring)).fetchSemanticsNodes().isNotEmpty()
        }
    }

    fun click(@StringRes id: Int) {
        compose.onNodeWithText(text(id)).scrollIntoView().performClick()
        compose.waitForIdle()
    }

    /** The text field labelled [label] (TextInput puts the label into the content description). */
    fun field(@StringRes label: Int): SemanticsNodeInteraction =
        compose.onNode(hasContentDescription(text(label)) and SemanticsMatcher.keyIsDefined(SemanticsProperties.EditableText))

    fun secure(): Boolean = SecureWindowFlags.isSecure(compose.activity.window)

    fun pressBack() {
        // Let the last input recompose first: Back acts on the composed (dirty) state.
        compose.waitForIdle()
        compose.runOnUiThread { compose.activity.onBackPressedDispatcher.onBackPressed() }
        compose.waitForIdle()
    }

    fun waitForSecure(expected: Boolean) = compose.waitUntil(5_000) { secure() == expected }

    /**
     * Scrolls the node into the visible part of the screen body first: at
     * 320 dp with 200 % text most content lies below the pinned footer, and a
     * click on a node outside the viewport would land on whatever is drawn
     * there. Nodes outside a scroll container (footer, top bar) stay as they are.
     */
    fun SemanticsNodeInteraction.scrollIntoView(): SemanticsNodeInteraction =
        try {
            performScrollTo()
        } catch (_: AssertionError) {
            this
        }

    /** The node is on screen, after scrolling the body to it when needed. */
    fun SemanticsNodeInteraction.shown(): SemanticsNodeInteraction = scrollIntoView().assertIsDisplayed()
}
