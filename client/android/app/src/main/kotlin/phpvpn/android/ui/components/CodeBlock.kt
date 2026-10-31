package phpvpn.android.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import phpvpn.android.R
import phpvpn.android.ui.theme.PhpvpnTokens

/**
 * Read-only monospace block for JSON and HTTP dumps (`.code`). Long tokens
 * wrap (`overflow-wrap: anywhere`) so there is never horizontal scrolling;
 * the text is selectable for copying.
 */
@Composable
fun CodeBlock(text: String, modifier: Modifier = Modifier) {
    SelectionContainer(modifier.fillMaxWidth()) {
        Text(
            text,
            Modifier
                .fillMaxWidth()
                .background(PhpvpnTokens.colors.codeBg, PhpvpnTokens.shapes.code)
                .padding(16.dp),
            color = PhpvpnTokens.colors.codeFg,
            style = PhpvpnTokens.text.code,
        )
    }
}

/** Inline monospace value, e.g. a hex key preview (`.mono`). */
@Composable
fun MonoText(text: String, modifier: Modifier = Modifier) {
    SelectionContainer(modifier) {
        Text(text, color = PhpvpnTokens.colors.ink, style = PhpvpnTokens.text.mono)
    }
}

@ComponentPreviews
@Composable
private fun CodeBlockPreview() = PreviewSurface {
    CodeBlock("HTTP/1.1 503 Service Unavailable\nContent-Type: text/plain\n\nService temporarily unavailable.")
    CodeBlock("{\n  \"serverVersion\": \"1.0.0-synthetic\",\n  \"generatedAt\": \"2026-10-30T08:41:03Z\"\n}")
    Card {
        MetaLabel(stringResource(R.string.password_key_preview))
        MonoText("a29f".repeat(16))
    }
}
