package phpvpn.android.ui.screens.wizard

import android.content.pm.PackageManager
import androidx.activity.compose.BackHandler
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavController
import phpvpn.android.R
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.OperationKind
import phpvpn.android.bridge.ipc.WizardError
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Hint
import phpvpn.android.ui.components.Intro
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.navigation.DiagnosticResponse
import phpvpn.android.ui.navigation.DiagnosticSource

/** 18 in the graph, drawn over the step whose check failed. */
@Composable
fun WizardErrorRoute(nav: NavController, entry: NavBackStackEntry) {
    val vm = wizardViewModel(nav, entry)
    val state by vm.state.collectAsStateWithLifecycle()
    WizardNavigationEffect(vm, nav)
    val correct = { nav.popBackStack() }
    BackHandler { correct() }
    val context = LocalContext.current
    val clientVersion = remember(context) {
        // The app version comes from client/CMakeLists.txt, the same source as PHPVPN_CLIENT_VERSION.
        runCatching {
            context.packageManager.getPackageInfo(context.packageName, PackageManager.PackageInfoFlags.of(0)).versionName
        }.getOrNull().orEmpty()
    }
    ErrorScreen(
        state = state,
        clientVersion = clientVersion,
        onCorrect = { correct() },
        onHttpDetail = { nav.navigate(DiagnosticResponse(DiagnosticSource.Wizard)) },
    )
}

/** The presentation variants of 18, chosen by error code and step. */
enum class WizardErrorVariant { SECRET_MISMATCH, VERSION, TIMEOUT, ACCESS_REFUSED, CONFLICT, GENERIC }

/** Variant of [error] on the step of [kind]. */
fun wizardErrorVariant(error: WizardError, kind: OperationKind?): WizardErrorVariant = when {
    error.code == ErrorCode.CRYPTO_TAG_MISMATCH.wireName &&
        (error.field == WizardViewModel.FIELD_SECRET || kind == OperationKind.SECRET) -> WizardErrorVariant.SECRET_MISMATCH
    error.code == ErrorCode.SERVER_VERSION_INCOMPATIBLE.wireName -> WizardErrorVariant.VERSION
    isTimeout(error.code) -> WizardErrorVariant.TIMEOUT
    error.code == ErrorCode.SERVER_UNAUTHORIZED.wireName &&
        (error.field == WizardViewModel.FIELD_KEY || kind == OperationKind.ACCESS) -> WizardErrorVariant.ACCESS_REFUSED
    error.code == ErrorCode.WIZARD_CONTRACT_SELF_CONFLICT.wireName -> WizardErrorVariant.CONFLICT
    else -> WizardErrorVariant.GENERIC
}

/**
 * The server version of a server_version_incompatible description, accepted
 * only in the sanitised form of `displayableServerVersion` (at most 32
 * characters of [0-9A-Za-z.+-]); anything else is not shown.
 */
fun serverVersionOf(description: String): String? =
    Regex(""", server version ([0-9A-Za-z.+-]{1,32})\.$""").find(description)?.groupValues?.get(1)

/**
 * 18 Verification error. The text comes from resources by code; the
 * received description is used only for the sanitised server version. The
 * secret is never repeated (not even masked: the field stays on 14). A
 * timeout does not mark any input as wrong. "Correct" returns to the failed
 * step with the draft; the HTTP detail entry exists only when the probe
 * captured an exchange.
 */
@Composable
fun ErrorScreen(
    state: WizardScreenState,
    clientVersion: String,
    onCorrect: () -> Unit,
    onHttpDetail: () -> Unit,
) {
    val kind = state.attemptKind
    val error = state.error
    val variant = error?.let { wizardErrorVariant(it, kind) } ?: WizardErrorVariant.GENERIC
    val stepNumber = when (kind) {
        OperationKind.URL -> 1
        OperationKind.SECRET -> 2
        OperationKind.ACCESS -> 3
        else -> null
    }
    val correctLabel = when (variant) {
        WizardErrorVariant.TIMEOUT -> R.string.wp09_error_back_to_step
        else -> when (kind) {
            OperationKind.SECRET -> R.string.wizard_error_correct
            OperationKind.ACCESS -> R.string.wp09_error_correct_identity
            else -> R.string.wp09_error_correct_url
        }
    }
    WizardScaffold(
        title = stringResource(R.string.wizard_error_title),
        onBack = onCorrect,
        step = stepNumber,
        footer = { ActionButton(stringResource(correctLabel), onCorrect, style = ActionStyle.Primary) },
    ) {
        if (error == null) {
            Heading(stringResource(R.string.wizard_error_notice_title))
            return@WizardScaffold
        }
        val code = error.code
        when (variant) {
            WizardErrorVariant.SECRET_MISMATCH -> {
                Heading(stringResource(R.string.wizard_error_heading))
                Intro(stringResource(R.string.wizard_error_intro))
                ErrorNotice(stringResource(R.string.wizard_error_notice_text))
            }
            WizardErrorVariant.VERSION -> {
                Heading(stringResource(R.string.wp09_error_version_heading))
                Intro(errorMessageText(code))
                val server = serverVersionOf(error.description) ?: stringResource(R.string.wp09_error_version_unrecognised)
                ErrorNotice(
                    stringResource(R.string.wp09_error_version_text, clientVersion, server) + "\n" +
                        errorActionText(code, error.field),
                )
            }
            WizardErrorVariant.TIMEOUT -> {
                Heading(stringResource(R.string.wp09_error_timeout_heading))
                ErrorNotice(stringResource(R.string.wp09_error_timeout_text))
            }
            WizardErrorVariant.ACCESS_REFUSED -> {
                Heading(stringResource(R.string.wp09_error_access_heading))
                Intro(stringResource(R.string.wp09_error_access_intro))
                ErrorNotice(errorActionText(code, error.field))
            }
            WizardErrorVariant.CONFLICT -> {
                Heading(stringResource(R.string.wp09_error_conflict_heading))
                Intro(errorMessageText(code))
                ErrorNotice(errorActionText(code, error.field) + "\n\n" + stringResource(R.string.wp09_error_conflict_reset))
            }
            WizardErrorVariant.GENERIC -> {
                Heading(
                    stringResource(
                        when (kind) {
                            OperationKind.SECRET -> R.string.wp09_error_secret_heading
                            OperationKind.ACCESS -> R.string.wp09_error_identity_heading
                            else -> R.string.wp09_error_url_heading
                        },
                    ),
                )
                Intro(errorMessageText(code))
                ErrorNotice(errorActionText(code, error.field))
            }
        }
        error.httpStatus?.let { Hint(stringResource(R.string.wp09_http_status, it)) }
        HttpDetailEntry(state, onHttpDetail)
    }
}

@Composable
private fun ErrorNotice(text: String) {
    Notice(text, kind = NoticeKind.Error, title = stringResource(R.string.wizard_error_notice_title))
}
