package phpvpn.android.ui

import android.app.Activity
import android.content.Intent
import android.os.Handler
import android.os.Looper
import androidx.activity.ComponentActivity
import androidx.activity.compose.LocalActivityResultRegistryOwner
import androidx.activity.result.ActivityResultRegistry
import androidx.activity.result.ActivityResultRegistryOwner
import androidx.activity.result.contract.ActivityResultContract
import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.SemanticsNodeInteraction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.core.app.ActivityOptionsCompat
import androidx.navigation.NavHostController
import androidx.navigation.compose.rememberNavController
import androidx.test.ext.junit.rules.ActivityScenarioRule
import androidx.test.platform.app.InstrumentationRegistry
import phpvpn.android.TestApplication
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.ui.navigation.PhpvpnNavHost
import phpvpn.android.ui.theme.PhpvpnTheme

typealias UiRule = AndroidComposeTestRule<ActivityScenarioRule<ComponentActivity>, ComponentActivity>

/** The test application with a fresh container for each test. */
fun freshApp(): TestApplication {
    val app = InstrumentationRegistry.getInstrumentation().targetContext.applicationContext as TestApplication
    app.resetContainer()
    app.consent.granted = true
    app.consent.nextResult = false
    app.consent.requests = 0
    return app
}

/**
 * Answers every launch (the VPN consent dialog 03) with the test consent's
 * next result, like the system dialog would, without leaving the app.
 */
class ConsentRegistryOwner(private val app: TestApplication) : ActivityResultRegistryOwner {
    override val activityResultRegistry = object : ActivityResultRegistry() {
        override fun <I, O> onLaunch(requestCode: Int, contract: ActivityResultContract<I, O>, input: I, options: ActivityOptionsCompat?) {
            val result = if (app.consent.nextResult) Activity.RESULT_OK else Activity.RESULT_CANCELED
            Handler(Looper.getMainLooper()).post { dispatchResult(requestCode, result, Intent()) }
        }
    }
}

/** Sets the app graph as content; returns the controller for direct navigation. */
fun UiRule.showApp(app: TestApplication, wrap: @Composable (@Composable () -> Unit) -> Unit = { it() }): NavHostController {
    lateinit var nav: NavHostController
    val owner = ConsentRegistryOwner(app)
    setContent {
        nav = rememberNavController()
        CompositionLocalProvider(LocalActivityResultRegistryOwner provides owner) {
            wrap {
                PhpvpnTheme { PhpvpnNavHost(clientVersion = { "1.0.3-test" }, navController = nav) }
            }
        }
    }
    waitForIdle()
    return nav
}

fun UiRule.text(@StringRes id: Int, vararg args: Any): String = activity.getString(id, *args)

fun UiRule.waitForTag(tag: String, timeoutMs: Long = 10_000) {
    waitUntil(timeoutMs) { onAllNodesWithTag(tag, useUnmergedTree = true).fetchSemanticsNodes().isNotEmpty() }
}

fun UiRule.waitForText(text: String, timeoutMs: Long = 10_000) {
    waitUntil(timeoutMs) { onAllNodesWithText(text, useUnmergedTree = true).fetchSemanticsNodes().isNotEmpty() }
}

fun UiRule.hasNoText(text: String): Boolean = onAllNodesWithText(text, useUnmergedTree = true).fetchSemanticsNodes().isEmpty()

/** Clicks the first clickable node with [text], scrolling it into view first when it is in the body. */
fun UiRule.click(text: String) {
    waitForText(text)
    val node: SemanticsNodeInteraction = onNodeWithText(text)
    runCatching { node.performScrollTo() }
    node.performClick()
    waitForIdle()
}

/** Clicks a bottom navigation item (role Tab), not other texts with the same words. */
fun UiRule.clickNav(text: String) {
    waitForText(text)
    onNode(hasText(text) and SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.Tab)).performClick()
    waitForIdle()
}

fun UiRule.pressBack() {
    runOnUiThread { activity.onBackPressedDispatcher.onBackPressed() }
    waitForIdle()
}

fun FakeIpcTransport.count(command: String) = requests(command).size
