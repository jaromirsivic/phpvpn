package phpvpn.android.ui.screens.wizard

import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavController
import phpvpn.android.R
import phpvpn.android.bridge.ipc.HttpHeaders
import phpvpn.android.ui.components.ActionButton
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.Eyebrow
import phpvpn.android.ui.components.Heading
import phpvpn.android.ui.components.Hint
import phpvpn.android.ui.components.Intro
import phpvpn.android.ui.components.Notice
import phpvpn.android.ui.components.NoticeKind
import phpvpn.android.ui.components.TextInput
import phpvpn.android.ui.navigation.rememberUnsavedChangesGuard
import phpvpn.android.ui.screens.secure.SecureWindow

/** 13 in the graph: edits a copy of the draft headers. */
@Composable
fun WizardHeadersRoute(nav: NavController, entry: NavBackStackEntry) {
    val vm = wizardViewModel(nav, entry)
    val state by vm.state.collectAsStateWithLifecycle()
    WizardNavigationEffect(vm, nav)
    // The edit lives in the ViewModel (rotation-safe, never in the saved state);
    // 12 drops it whenever it is shown again.
    val text = vm.headersEdit ?: vm.headersAccepted
    var problem by remember { mutableStateOf<HeaderCheck.Invalid?>(null) }
    val close = {
        vm.endHeadersEdit()
        nav.popBackStack()
        Unit
    }
    // Cancel and Back drop only the header edits (32 when there are any).
    val cancel = rememberUnsavedChangesGuard(text != vm.headersAccepted) { close() }
    HeadersScreen(
        text = text,
        problem = problem,
        enabled = state.editable,
        onText = {
            vm.updateHeadersEdit(it)
            problem = null
        },
        onReset = {
            vm.updateHeadersEdit(HeaderRules.displayText(HttpHeaders.DEFAULT))
            problem = null
        },
        onUse = {
            when (val check = vm.acceptHeaders(text)) {
                is HeaderCheck.Valid -> close()
                is HeaderCheck.Invalid -> problem = check
            }
        },
        onCancel = cancel,
    )
}

/**
 * 13 HTTP headers, the counterpart of http_headers_window: one header per
 * line, the shared validator on "Use in wizard" (canonical CRLF into the
 * draft, a change invalidates every check), Reset loads the default
 * template into this text only, Cancel keeps the draft value. Errors name
 * the line and at most the header name, never a value. Headers may carry
 * cookies, so the window is secure while the editor is shown.
 */
@Composable
fun HeadersScreen(
    text: String,
    problem: HeaderCheck.Invalid?,
    enabled: Boolean,
    onText: (String) -> Unit,
    onReset: () -> Unit,
    onUse: () -> Unit,
    onCancel: () -> Unit,
) {
    SecureWindow()
    WizardScaffold(
        title = stringResource(R.string.headers_title),
        onBack = onCancel,
        footer = {
            ActionButton(stringResource(R.string.headers_use), onUse, style = ActionStyle.Primary, enabled = enabled)
            ActionButton(stringResource(R.string.headers_cancel), onCancel, style = ActionStyle.Ghost)
        },
    ) {
        Eyebrow(stringResource(R.string.headers_eyebrow))
        Heading(stringResource(R.string.headers_heading))
        Intro(stringResource(R.string.headers_intro))
        TextInput(
            value = text,
            onValueChange = onText,
            label = stringResource(R.string.headers_field),
            hint = stringResource(R.string.headers_hint),
            error = problem?.let { headerProblemText(it) },
            enabled = enabled,
            singleLine = false,
            minLines = 6,
            monospace = true,
            keyboardOptions = KeyboardOptions(
                capitalization = KeyboardCapitalization.None,
                autoCorrectEnabled = false,
                keyboardType = KeyboardType.Ascii,
            ),
        )
        Hint(stringResource(R.string.wp09_headers_defaults_hint))
        Notice(stringResource(R.string.headers_notice_text), kind = NoticeKind.Info)
        ActionButton(stringResource(R.string.headers_reset), onReset, style = ActionStyle.Ghost, enabled = enabled)
    }
}

@Composable
private fun headerProblemText(problem: HeaderCheck.Invalid): String = when (problem.problem) {
    HeaderProblem.REQUIRED -> stringResource(R.string.wp09_headers_error_required)
    HeaderProblem.LINE_FORMAT, HeaderProblem.BARE_CR -> stringResource(R.string.wp09_headers_error_line, problem.line)
    HeaderProblem.INVALID_NAME -> stringResource(R.string.wp09_headers_error_name, problem.line)
    HeaderProblem.CONTROL_OCTET -> stringResource(R.string.wp09_headers_error_control, problem.line)
    HeaderProblem.MANAGED -> stringResource(R.string.wp09_headers_error_managed, problem.line, problem.name.orEmpty())
    HeaderProblem.DUPLICATE -> stringResource(R.string.wp09_headers_error_duplicate, problem.line, problem.name.orEmpty())
    HeaderProblem.TOO_LARGE -> stringResource(R.string.wp09_headers_error_too_large)
    HeaderProblem.ENCODING -> stringResource(R.string.wp09_headers_error_encoding)
    HeaderProblem.BUSY -> stringResource(R.string.wp09_headers_error_busy)
}
