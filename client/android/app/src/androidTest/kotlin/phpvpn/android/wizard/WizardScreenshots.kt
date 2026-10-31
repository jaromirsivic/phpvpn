package phpvpn.android.wizard

import android.graphics.Bitmap
import androidx.activity.ComponentActivity
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.DeviceConfigurationOverride
import androidx.compose.ui.test.FontScale
import androidx.compose.ui.test.ForcedSize
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.isDialog
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.then
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.File
import java.io.FileOutputStream
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import phpvpn.android.bridge.controller.WizardStep
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.CheckpointResult
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.ConnectionPhase
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.HttpDiagnostic
import phpvpn.android.bridge.ipc.HttpHeaders
import phpvpn.android.bridge.ipc.IpcJson
import phpvpn.android.bridge.ipc.OperationKind
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.ipc.WizardError
import phpvpn.android.debug.FakeWizardService
import phpvpn.android.ui.navigation.DiagnosticSource
import phpvpn.android.ui.navigation.UnsavedChangesDialog
import phpvpn.android.ui.screens.diagnostic.DiagnosticScreen
import phpvpn.android.ui.screens.secure.LocalSecureWindowEnabled
import phpvpn.android.ui.screens.wizard.ErrorScreen
import phpvpn.android.ui.screens.wizard.ExpiredScreen
import phpvpn.android.ui.screens.wizard.FinishScreen
import phpvpn.android.ui.screens.wizard.GateScreen
import phpvpn.android.ui.screens.wizard.HeaderCheck
import phpvpn.android.ui.screens.wizard.HeaderProblem
import phpvpn.android.ui.screens.wizard.HeaderRules
import phpvpn.android.ui.screens.wizard.HeadersScreen
import phpvpn.android.ui.screens.wizard.IdentityScreen
import phpvpn.android.ui.screens.wizard.LocalProblem
import phpvpn.android.ui.screens.wizard.SavedScreen
import phpvpn.android.ui.screens.wizard.SecretScreen
import phpvpn.android.ui.screens.wizard.UrlScreen
import phpvpn.android.ui.screens.wizard.WizardResetDialog
import phpvpn.android.ui.screens.wizard.WizardRules
import phpvpn.android.ui.screens.wizard.WizardScreenState
import phpvpn.android.ui.theme.PhpvpnTheme

/**
 * Screenshots of every WP09 screen (T-U02, design comparison) at 412 dp, at
 * 412 dp with the whole page height, and at 320 dp with 200 % text, from
 * synthetic states. Files go to the app's external files directory
 * (Android/data/<applicationId>/files/wp09) for `adb pull`; the comparison
 * with assets/android-en/previews/full is done by eye. FLAG_SECURE is off
 * only here, otherwise 13 to 15 would be captured black.
 */
@RunWith(AndroidJUnit4::class)
class WizardScreenshots {
    @get:Rule
    val compose = createAndroidComposeRule<ComponentActivity>()

    private class Variant(val suffix: String, val size: DpSize, val fontScale: Float)

    private val variants = listOf(
        Variant("412", DpSize(412.dp, 892.dp), 1f),
        Variant("412-full", DpSize(412.dp, 1500.dp), 1f),
        Variant("320-fs200", DpSize(320.dp, 892.dp), 2f),
    )

    private val url = "https://vpn.example.com/phpvpn.php"
    private val base = WizardScreenState(
        step = WizardStep.SERVER_URL,
        serverUrl = url,
        httpHeaders = HttpHeaders.DEFAULT,
        secretText = SecretText.of("synthetic-secret"),
        virtualIp = "10.99.99.10",
        autoDetectIp = true,
        suggestedIp = "10.99.99.10",
        accessKey = WizardRules.defaultAccessKey("10.99.99.10"),
        useDefaultKey = true,
        vpnCidr = "10.99.99.0/24",
        contractsAllowed = true,
        missingContracts = 13,
        contractsToAdd = 13,
    )
    private val diagnostic: HttpDiagnostic =
        IpcJson.decodeFromJsonElement(HttpDiagnostic.serializer(), FakeWizardService().httpDiagnostic())

    private fun error(code: ErrorCode, field: String?, description: String = "", httpStatus: Int? = null) =
        WizardError(code.wireName, description, false, field, httpStatus, "")

    private var variant by mutableStateOf(variants.first())
    private var screen by mutableStateOf<(@Composable () -> Unit)?>(null)

    @Test
    fun captureEveryScreen() {
        compose.setContent {
            val current = screen ?: return@setContent
            DeviceConfigurationOverride(
                DeviceConfigurationOverride.ForcedSize(variant.size) then DeviceConfigurationOverride.FontScale(variant.fontScale),
            ) {
                PhpvpnTheme {
                    CompositionLocalProvider(LocalSecureWindowEnabled provides false) { current() }
                }
            }
        }
        val noop: () -> Unit = {}
        shoot("11-gate") { GateScreen(base.copy(step = WizardStep.DISCONNECT_GATE, vpnState = ConnectionState.CONNECTED, vpnPhase = ConnectionPhase.RUNNING), noop, noop) }
        shoot("11-gate-interrupted") {
            GateScreen(
                base.copy(step = WizardStep.DISCONNECT_GATE, offlineKnown = true, error = error(ErrorCode.WIZARD_CONNECTION_INTERRUPTED, null)),
                noop,
                noop,
            )
        }
        shoot("12-url") { UrlScreen(base, noop, {}, noop, noop, noop, noop, noop, noop, noop) }
        shoot("12-url-placeholder") {
            UrlScreen(
                base.copy(serverUrl = FakeWizardService.PLACEHOLDER_URL, localProblem = LocalProblem.URL_PLACEHOLDER, invalidField = "serverUrl"),
                noop, {}, noop, noop, noop, noop, noop, noop, noop,
            )
        }
        shoot("12-url-load-error") {
            UrlScreen(base.copy(step = WizardStep.LOAD_ERROR, availability = ConfigAvailability.UNREADABLE), noop, {}, noop, noop, noop, noop, noop, noop, noop)
        }
        shoot("13-headers") { HeadersScreen(HeaderRules.displayText(HttpHeaders.DEFAULT), null, true, {}, noop, noop, noop) }
        shoot("13-headers-error") {
            HeadersScreen("Host: example", HeaderCheck.Invalid(HeaderProblem.MANAGED, 1, "Host"), true, {}, noop, noop, noop)
        }
        val secretStep = base.copy(step = WizardStep.SERVER_SECRET, checks = listOf(CheckpointResult.SUCCESS, CheckpointResult.PENDING, CheckpointResult.PENDING))
        shoot("14-secret") { SecretScreen(secretStep, noop, {}, {}, noop, noop, noop) }
        shoot("14-secret-default") {
            SecretScreen(secretStep.copy(useDefaultSecret = true, secretIsDefault = true, secretText = SecretText.of(WizardRules.DEFAULT_SERVER_SECRET)), noop, {}, {}, noop, noop, noop)
        }
        val identityStep = base.copy(step = WizardStep.IDENTITY)
        shoot("15-identity") { IdentityScreen(identityStep, noop, {}, {}, {}, {}, noop, noop) }
        shoot("15-identity-manual") {
            IdentityScreen(
                identityStep.copy(autoDetectIp = false, suggestedIp = null, useDefaultKey = false, virtualIp = "10.99.99.300", accessKey = SecretText.of("ab12"),
                    localProblem = LocalProblem.IP_INVALID, invalidField = "virtualIp"),
                noop, {}, {}, {}, {}, noop, noop,
            )
        }
        shoot("15-identity-checking") { IdentityScreen(identityStep.copy(busy = true, attemptKind = OperationKind.ACCESS), noop, {}, {}, {}, {}, noop, noop) }
        val finishStep = base.copy(step = WizardStep.CONTRACTS)
        shoot("16-finish") { FinishScreen(finishStep, false, noop, {}, noop, noop, noop, noop) }
        shoot("16-finish-unsupported") {
            FinishScreen(finishStep.copy(contractsAllowed = false, missingContracts = 0, contractsToAdd = 0, vpnCidr = "192.168.7.0/24"), false, noop, {}, noop, noop, noop, noop)
        }
        shoot("16-finish-unknown") { FinishScreen(finishStep.copy(step = WizardStep.COMMIT_UNKNOWN), true, noop, {}, noop, noop, noop, noop) }
        shoot("17-saved") { SavedScreen(url, "10.99.99.10", "10.99.99.0/24", noop, noop, noop, noop) }
        shoot("18-error-secret") {
            ErrorScreen(secretStep.copy(error = error(ErrorCode.CRYPTO_TAG_MISMATCH, "serverSecret"), attemptKind = OperationKind.SECRET), "1.0.3", noop, noop)
        }
        shoot("18-error-version") {
            ErrorScreen(
                base.copy(
                    error = error(ErrorCode.SERVER_VERSION_INCOMPATIBLE, "serverUrl", "Client version 1.0.3, server version 2.0.0."),
                    attemptKind = OperationKind.URL,
                ),
                "1.0.3",
                noop,
                noop,
            )
        }
        shoot("18-error-http") {
            ErrorScreen(
                base.copy(error = error(ErrorCode.RESPONSE_INVALID, "serverUrl", httpStatus = 200), attemptKind = OperationKind.URL, httpDiagnostic = diagnostic),
                "1.0.3",
                noop,
                noop,
            )
        }
        shoot("18-error-timeout") { ErrorScreen(identityStep.copy(error = error(ErrorCode.TIMEOUT, null), attemptKind = OperationKind.ACCESS), "1.0.3", noop, noop) }
        shoot("18-error-access") {
            ErrorScreen(identityStep.copy(error = error(ErrorCode.SERVER_UNAUTHORIZED, "accessKey", httpStatus = 401), attemptKind = OperationKind.ACCESS), "1.0.3", noop, noop)
        }
        shoot("30-http-response") { DiagnosticScreen(DiagnosticSource.Wizard, false, diagnostic, noop) {} }
        shoot("31-http-request") { DiagnosticScreen(DiagnosticSource.Wizard, true, diagnostic, noop) {} }
        shoot("30-http-unavailable") { DiagnosticScreen(DiagnosticSource.Connect, false, null, noop) {} }
        shoot("47-session-expired") { ExpiredScreen(noop, noop, noop) }
        shoot("40-wizard-reset", dialog = true) {
            UrlScreen(base, noop, {}, noop, noop, noop, noop, noop, noop, noop)
            WizardResetDialog(noop, noop)
        }
        shoot("32-discard", dialog = true) {
            UrlScreen(base.copy(dirty = true), noop, {}, noop, noop, noop, noop, noop, noop, noop)
            UnsavedChangesDialog(noop, noop)
        }
    }

    private fun shoot(name: String, dialog: Boolean = false, content: @Composable () -> Unit) {
        val directory = File(compose.activity.getExternalFilesDir(null), "wp09").apply { mkdirs() }
        for (each in variants) {
            compose.runOnUiThread {
                variant = each
                screen = content
            }
            compose.waitForIdle()
            val node = if (dialog) compose.onNode(isDialog()) else compose.onRoot()
            val bitmap = node.captureToImage().asAndroidBitmap()
            FileOutputStream(File(directory, "$name-${each.suffix}.png")).use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
        }
    }
}
