package phpvpn.android.ui.navigation

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import androidx.navigation.navDeepLink
import phpvpn.android.ui.components.ActionStyle

/**
 * Helpers shared by the graph files. The graph is split per work package
 * (MainGraph WP08, WizardGraph WP09, ContractsGraph and EditorGraph WP10) so
 * the screens can be filled in parallel without editing one file.
 */

/**
 * WP07 placeholders report a pending draft on every screen of the 32 pattern
 * (12-16, 19-22, 23) so that Back and Cancel exercise the dialog. WP09 and
 * WP10 replace this with the real dirty flag of their state holders.
 */
internal const val PLACEHOLDER_HAS_DRAFT = true

internal const val WP08 = 8
internal const val WP09 = 9
internal const val WP10 = 10

internal fun link(uri: String) = listOf(navDeepLink { uriPattern = uri })

@Composable
internal fun navAction(@StringRes label: Int, style: ActionStyle = ActionStyle.Secondary, onClick: () -> Unit) =
    PlaceholderAction(stringResource(label), onClick, style)
