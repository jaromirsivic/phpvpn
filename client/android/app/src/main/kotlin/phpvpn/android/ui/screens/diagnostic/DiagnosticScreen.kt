package phpvpn.android.ui.screens.diagnostic

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavController
import kotlinx.coroutines.flow.MutableStateFlow
import phpvpn.android.R
import phpvpn.android.bridge.ipc.HttpDiagnostic
import phpvpn.android.bridge.ipc.RequestHeadersSource
import phpvpn.android.bridge.ipc.ResponseHeadersSource
import phpvpn.android.bridge.repository.ConnectionStatus
import phpvpn.android.ui.components.Card
import phpvpn.android.ui.components.CodeBlock
import phpvpn.android.ui.components.Eyebrow
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Hint
import phpvpn.android.ui.components.InfoRow
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.ScreenScaffold
import phpvpn.android.ui.components.Tabs
import phpvpn.android.ui.components.TopBar
import phpvpn.android.ui.navigation.DiagnosticSource
import phpvpn.android.ui.navigation.switchDiagnosticTab
import phpvpn.android.ui.screens.wizard.WizardNavigationEffect
import phpvpn.android.ui.screens.wizard.existingWizardViewModel
import phpvpn.android.ui.screens.wizard.rememberWizardDependencies

/**
 * 30 and 31 for [source]. The Wizard detail is the one of the live session
 * (its failed check), the Connect detail the one of the status, pinned to the
 * attempt (epoch and connectAttemptId) it was first shown for, so a later
 * attempt never passes as this one.
 *
 * Closing the view is not an invalidation (client.md "HTTP detail
 * neúspěšného Wizard probe" and "... Connect"): the same failed attempt can
 * be opened again. The Wizard detail disappears, with one cancel-attempt,
 * when its attempt is invalidated (new check, edit, Back, Reset, resume,
 * interruption, expiry); the Connect detail with dismiss-connect-diagnostic
 * when the Connect error panel (06) is closed, or with the next Connect.
 */
@Composable
fun DiagnosticRoute(nav: NavController, entry: NavBackStackEntry, source: DiagnosticSource, showRequest: Boolean) {
    val diagnostic = when (source) {
        DiagnosticSource.Wizard -> wizardDiagnostic(nav, entry)
        DiagnosticSource.Connect -> connectDiagnostic()
    }
    DiagnosticScreen(
        source = source,
        showRequest = showRequest,
        diagnostic = diagnostic,
        onBack = { nav.popBackStack() },
        onTab = { index -> if ((index == 0) != showRequest) nav.switchDiagnosticTab(source, showRequest = index == 0) },
    )
}

@Composable
private fun wizardDiagnostic(nav: NavController, entry: NavBackStackEntry): HttpDiagnostic? {
    val vm = existingWizardViewModel(nav, entry) ?: return null
    val state by vm.state.collectAsStateWithLifecycle()
    // A lease expiry or an interruption still moves the Wizard on from here.
    WizardNavigationEffect(vm, nav)
    return state.httpDiagnostic
}

@Composable
private fun connectDiagnostic(): HttpDiagnostic? {
    val connection = rememberWizardDependencies().connection
    val flow = remember(connection) { connection?.status ?: MutableStateFlow<ConnectionStatus>(ConnectionStatus.Loading) }
    val status by flow.collectAsStateWithLifecycle()
    val progress = connection?.progress?.collectAsStateWithLifecycle()?.value
    val current = (status as? ConnectionStatus.Available)?.status
    val (identity, diagnostic) = when {
        current?.httpDiagnostic != null -> (current.connectionEpoch to current.connectAttemptId) to current.httpDiagnostic
        progress?.httpDiagnostic != null -> (progress.connectionEpoch to progress.connectAttemptId) to progress.httpDiagnostic
        else -> null to null
    }
    // Plain holder, not state: pinning must not trigger another composition.
    val pin = remember { AttemptPin() }
    if (pin.identity == null && identity != null) pin.identity = identity
    return if (identity != null && identity == pin.identity) diagnostic else null
}

/** The Connect attempt (epoch, connectAttemptId) whose detail this view shows. */
private class AttemptPin {
    var identity: Pair<ULong, String?>? = null
}

/**
 * 30 HTTP response and 31 HTTP request: separate Request and Response tabs,
 * the provenance of the headers (`winhttp` = captured by the platform
 * transport, `merged`/`application` reconstructed, `unavailable`), notes for
 * incomplete, truncated or escaped data, and the raw octets as read-only
 * text. Nothing is copied to the clipboard automatically.
 */
@Composable
fun DiagnosticScreen(
    source: DiagnosticSource,
    showRequest: Boolean,
    diagnostic: HttpDiagnostic?,
    onBack: () -> Unit,
    onTab: (Int) -> Unit,
) {
    ScreenScaffold(
        topBar = {
            TopBar(stringResource(if (showRequest) R.string.http_request_title else R.string.http_response_title), onBack = onBack)
        },
    ) {
        Eyebrow(stringResource(R.string.http_eyebrow))
        Heading(stringResource(R.string.http_heading))
        Tabs(
            listOf(stringResource(R.string.http_tab_request), stringResource(R.string.http_tab_response)),
            if (showRequest) 0 else 1,
            onTab,
        )
        val message = diagnostic?.let { if (showRequest) it.request else it.response }
        val display = remember(message) { message?.let { HttpDiagnosticText.display(it) } }
        if (diagnostic == null || message == null || display == null) {
            Notice(
                stringResource(R.string.wp09_diag_unavailable_text),
                kind = NoticeKind.Info,
                title = stringResource(R.string.wp09_diag_unavailable_title),
            )
            return@ScreenScaffold
        }
        if (showRequest) RequestNotice(diagnostic) else ResponseNotice(diagnostic)
        Card {
            InfoRow(
                stringResource(R.string.wp09_diag_row_source),
                stringResource(if (source == DiagnosticSource.Connect) R.string.wp09_diag_source_connect else R.string.wp09_diag_source_wizard),
            )
            InfoRow(stringResource(R.string.wp09_diag_row_action), diagnostic.action.name.lowercase(), divider = true)
            InfoRow(stringResource(R.string.wp09_diag_row_headers), headersSourceText(diagnostic, showRequest), divider = true)
            InfoRow(
                stringResource(R.string.wp09_diag_row_size),
                stringResource(R.string.wp09_diag_size_kib, HttpDiagnosticText.sizeKiB(message.originalSizeBytes)),
                divider = true,
            )
        }
        if (message.truncated) Hint(stringResource(R.string.wp09_diag_truncated, HttpDiagnosticText.omittedBytes(message).toString()))
        if (display.escaped) Hint(stringResource(R.string.wp09_diag_escaped))
        CodeBlock(display.text)
        Hint(stringResource(if (source == DiagnosticSource.Connect) R.string.http_response_hint else R.string.wp09_diag_hint_wizard))
    }
}

@Composable
private fun RequestNotice(diagnostic: HttpDiagnostic) {
    val unknownVersion = HttpDiagnosticText.unknownHttpVersion(diagnostic.request)
    val versionNote = if (unknownVersion) "\n" + stringResource(R.string.wp09_diag_http_unknown) else ""
    when (diagnostic.requestHeadersSource) {
        RequestHeadersSource.APPLICATION -> Notice(
            stringResource(R.string.wp09_diag_request_application_text) + versionNote,
            kind = NoticeKind.Info,
            title = stringResource(R.string.http_request_notice_title),
        )
        RequestHeadersSource.MERGED -> Notice(
            stringResource(R.string.wp09_diag_request_merged_text) + versionNote,
            kind = NoticeKind.Info,
            title = stringResource(R.string.wp09_diag_request_merged_title),
        )
        RequestHeadersSource.WINHTTP -> Notice(
            stringResource(R.string.wp09_diag_request_captured_text) + versionNote,
            kind = NoticeKind.Info,
            title = stringResource(R.string.wp09_diag_request_captured_title),
        )
    }
}

@Composable
private fun ResponseNotice(diagnostic: HttpDiagnostic) {
    val notes = buildList {
        if (!diagnostic.responseComplete) add(stringResource(R.string.wp09_diag_incomplete))
        if (diagnostic.responseHeadersSource != ResponseHeadersSource.WINHTTP) add(stringResource(R.string.wp09_diag_response_headers_partial))
    }
    if (notes.isEmpty()) return
    Notice(notes.joinToString("\n"), kind = NoticeKind.Warn, title = stringResource(R.string.http_response_notice_title))
}

@Composable
private fun headersSourceText(diagnostic: HttpDiagnostic, request: Boolean): String = stringResource(
    if (request) {
        when (diagnostic.requestHeadersSource) {
            RequestHeadersSource.WINHTTP -> R.string.wp09_diag_request_captured
            RequestHeadersSource.MERGED -> R.string.wp09_diag_request_merged
            RequestHeadersSource.APPLICATION -> R.string.wp09_diag_request_application
        }
    } else {
        when (diagnostic.responseHeadersSource) {
            ResponseHeadersSource.WINHTTP -> R.string.wp09_diag_response_captured
            ResponseHeadersSource.AVAILABLE -> R.string.wp09_diag_response_available
            ResponseHeadersSource.UNAVAILABLE -> R.string.wp09_diag_response_unavailable
        }
    },
)
