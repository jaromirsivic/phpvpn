package phpvpn.android.ui

import android.graphics.Bitmap
import androidx.activity.ComponentActivity
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.hasScrollAction
import androidx.compose.ui.test.isDialog
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipeUp
import androidx.compose.ui.unit.Density
import androidx.navigation.NavHostController
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.time.Instant
import java.time.temporal.ChronoUnit
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import phpvpn.android.R
import phpvpn.android.TestApplication
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.testing.IpcReplies
import phpvpn.android.testing.respondText
import phpvpn.android.testing.respondTimeout
import phpvpn.android.ui.components.MainDestination
import phpvpn.android.ui.navigation.About
import phpvpn.android.ui.navigation.ConnectionChecks
import phpvpn.android.ui.navigation.ConnectionPermission
import phpvpn.android.ui.navigation.Exit
import phpvpn.android.ui.navigation.Server
import phpvpn.android.ui.navigation.ServerTab
import phpvpn.android.ui.navigation.navigateToMain
import phpvpn.android.ui.screens.connection.CHECKS_TAG
import phpvpn.android.ui.screens.connection.ConnectionTags
import phpvpn.android.ui.screens.server.ServerTags
import phpvpn.android.ui.screens.settings.ABOUT_TAG
import phpvpn.android.ui.screens.settings.ATTENTION_TAG
import phpvpn.android.ui.screens.settings.EXIT_TAG
import phpvpn.android.ui.screens.settings.SETTINGS_TAG
import phpvpn.android.ui.screens.settings.WELCOME_TAG
import phpvpn.android.ui.screens.traffic.TrafficTags

/**
 * T-U02 (WP08 part): every WP08 screen at 412, 360 and 320 dp, each with 100 %
 * and 200 % text, plus the bottom of the 412 dp page. The width is set by
 * overriding the density so the full device width equals the target dp.
 * Images go to <external files>/wp08 and are compared by eye with
 * assets/android-en/previews/full (pulled to work/wp08, gitignored).
 */
@RunWith(AndroidJUnit4::class)
class Wp08ScreenshotTest {
    @get:Rule
    val rule: UiRule = createAndroidComposeRule<ComponentActivity>()

    private data class Variant(val widthDp: Int, val fontScale: Float) {
        val suffix: String get() = "w$widthDp" + if (fontScale > 1f) "-fs200" else ""
    }

    private val variants = listOf(
        Variant(412, 1f), Variant(360, 1f), Variant(320, 1f),
        Variant(412, 2f), Variant(360, 2f), Variant(320, 2f),
    )

    private val counters = IpcReplies.Counters(312, 1284, 5_310_000, 23_700_000, 8204, 14826, 4_160_000, 18_420_000)

    private fun startedAgo(seconds: Long): String =
        Instant.now().minusSeconds(seconds).truncatedTo(ChronoUnit.SECONDS).toString().dropLast(1) + ".000Z"

    private fun shoot(
        name: String,
        tag: String,
        dialog: Boolean = false,
        prepare: (FakeIpcTransport, TestApplication) -> Unit = { _, _ -> },
        act: (NavHostController) -> Unit = {},
        afterNavigate: () -> Unit = {},
    ) {
        val app = freshApp()
        val transport = app.transport
        transport.respondText("get-config-state", IpcReplies.configState())
        transport.respondText("get-config-document", IpcReplies.configDocument())
        transport.respondText("get-status", IpcReplies.disconnected())
        prepare(transport, app)
        var variant by mutableStateOf(variants.first())
        val nav = rule.showApp(app) { content ->
            val widthPx = LocalWindowInfo.current.containerSize.width.toFloat()
            val density = if (widthPx > 0f) widthPx / variant.widthDp else LocalDensity.current.density
            CompositionLocalProvider(LocalDensity provides Density(density, variant.fontScale)) { content() }
        }
        rule.runOnUiThread { act(nav) }
        afterNavigate()
        rule.waitForTag(tag, 15_000)
        val dir = File(InstrumentationRegistry.getInstrumentation().targetContext.getExternalFilesDir(null), "wp08").apply { mkdirs() }
        for (next in variants) {
            variant = next
            rule.waitForIdle()
            Thread.sleep(250)
            save(capture(dialog), File(dir, "$name-${next.suffix}.png"))
        }
        if (!dialog) {
            variant = variants.first()
            rule.waitForIdle()
            val scrollable = rule.onAllNodes(hasScrollAction())
            if (scrollable.fetchSemanticsNodes().isNotEmpty()) {
                repeat(3) { scrollable.onFirst().performTouchInput { swipeUp() } }
                rule.waitForIdle()
                Thread.sleep(250)
                save(capture(false), File(dir, "$name-w412-bottom.png"))
            }
        }
    }

    private fun capture(dialog: Boolean): Bitmap =
        (if (dialog) rule.onNode(isDialog()) else rule.onRoot(useUnmergedTree = true)).captureToImage().asAndroidBitmap()

    private fun save(bitmap: Bitmap, file: File) {
        file.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }

    @Test
    fun s00_welcome() = shoot("00-welcome", WELCOME_TAG, prepare = { t, _ ->
        t.respondText("get-config-state", IpcReplies.configState("missing", "config_not_found"))
    })

    @Test
    fun s01_connected() = shoot("01-connected", ConnectionTags.CONNECTED, prepare = { t, _ ->
        t.respondText("get-status", IpcReplies.connected(counters, startedAgo(24 * 60 + 18)))
    })

    @Test
    fun s02_disconnected() = shoot("02-disconnected", ConnectionTags.DISCONNECTED)

    @Test
    fun s02_revoked() = shoot("02-revoked", ConnectionTags.DISCONNECTED, prepare = { _, app ->
        app.container.connectionEvents.report(phpvpn.android.ui.state.DisconnectReason.REVOKED)
    })

    @Test
    fun s04_connecting() = shoot("04-connecting", ConnectionTags.CONNECTING, prepare = { t, _ ->
        t.respondText("get-status", IpcReplies.connecting())
    })

    @Test
    fun s05_reconnecting() = shoot("05-reconnecting", ConnectionTags.RECONNECTING, prepare = { t, _ ->
        t.respondText(
            "get-status",
            IpcReplies.status("recovering", "health", checks = IpcReplies.ALL_PASSED, errorCode = "communication_timeout", recoverable = true),
        )
    })

    @Test
    fun s06_error() = shoot("06-connection-error", ConnectionTags.FAILED, prepare = { t, _ ->
        t.respondText("get-status", IpcReplies.failed(diagnostic = true))
    })

    @Test
    fun s07_disconnecting() = shoot("07-disconnecting", ConnectionTags.DISCONNECTING, prepare = { t, _ ->
        t.respondText("get-status", IpcReplies.stopping())
    })

    @Test
    fun s08_unavailable() = shoot("08-unavailable", ConnectionTags.UNAVAILABLE, prepare = { t, _ ->
        t.enqueueText("get-status", IpcReplies.connected(counters, startedAgo(60 * 60)))
        t.respondTimeout("get-status")
    }, afterNavigate = { rule.waitForTag(ConnectionTags.CONNECTED) })

    @Test
    fun s37_permission() = shoot("37-permission-denied", ConnectionTags.PERMISSION, act = { it.navigate(ConnectionPermission) })

    @Test
    fun s38_unconfirmed() = shoot("38-disconnect-unknown", ConnectionTags.UNCONFIRMED, prepare = { t, _ ->
        t.respondText("get-status", IpcReplies.connected(counters))
        t.respondText("disconnect", IpcReplies.error("busy", recoverable = true))
    }, afterNavigate = {
        rule.waitForTag(ConnectionTags.CONNECTED)
        rule.click(rule.text(R.string.action_disconnect_vpn))
    })

    @Test
    fun s48_checks() = shoot("48-connection-checks", CHECKS_TAG, prepare = { t, _ ->
        t.respondText("get-status", IpcReplies.connected(counters))
    }, act = { it.navigate(ConnectionChecks) })

    @Test
    fun s10_settings() = shoot("10-settings", SETTINGS_TAG, act = { it.navigateToMain(MainDestination.Settings) })

    @Test
    fun s34_about() = shoot("34-about", ABOUT_TAG, act = { it.navigate(About) })

    @Test
    fun s35_exit() = shoot("35-exit", EXIT_TAG, dialog = true, act = { it.navigate(Exit) })

    @Test
    fun s36_attention() = shoot("36-config-unavailable", ATTENTION_TAG, prepare = { t, _ ->
        t.respondText("get-config-state", IpcReplies.configState("invalid", "config_invalid_json"))
    })

    @Test
    fun s26_traffic() = shoot("26-statistics", TrafficTags.CURRENT, prepare = { t, _ ->
        t.respondText("get-status", IpcReplies.connected(counters, startedAgo(24 * 60 + 18)))
    }, act = { it.navigateToMain(MainDestination.Traffic) })

    @Test
    fun s27_trafficStale() = shoot("27-statistics-stale", TrafficTags.STALE, prepare = { t, _ ->
        t.enqueueText("get-status", IpcReplies.connected(counters))
        t.respondTimeout("get-status")
    }, act = { it.navigateToMain(MainDestination.Traffic) })

    @Test
    fun s43_trafficWaiting() = shoot("43-statistics-empty", TrafficTags.WAITING, prepare = { t, _ ->
        t.respondTimeout("get-status")
    }, act = { it.navigateToMain(MainDestination.Traffic) })

    @Test
    fun s28_server() = shoot("28-server", ServerTags.LIST, prepare = { t, _ ->
        t.respondText("get-server-info", IpcReplies.serverInfoSample())
    }, act = { it.navigateToMain(MainDestination.Server) })

    @Test
    fun s29_serverRaw() = shoot("29-server-raw", ServerTags.RAW, prepare = { t, _ ->
        t.respondText("get-server-info", IpcReplies.serverInfoSample())
    }, act = { it.navigate(Server(ServerTab.Raw)) })

    @Test
    fun s44_serverEmpty() = shoot("44-server-empty", ServerTags.EMPTY, prepare = { t, _ ->
        t.respondText("get-server-info", IpcReplies.serverInfo())
    }, act = { it.navigateToMain(MainDestination.Server) })

    @Test
    fun s45_serverStale() = shoot("45-server-stale", ServerTags.STALE, prepare = { t, _ ->
        t.enqueueText("get-server-info", IpcReplies.serverInfoSample())
        t.respondTimeout("get-server-info")
    }, act = { it.navigateToMain(MainDestination.Server) }, afterNavigate = {
        rule.waitForTag(ServerTags.LIST)
        rule.click(rule.text(R.string.action_refresh_now))
    })

    @Test
    fun s45_serverFailed() = shoot("45-server-failed", ServerTags.FAILED, prepare = { t, _ ->
        t.respondText("get-server-info", IpcReplies.error("server_version_incompatible"))
    }, act = { it.navigateToMain(MainDestination.Server) })
}
