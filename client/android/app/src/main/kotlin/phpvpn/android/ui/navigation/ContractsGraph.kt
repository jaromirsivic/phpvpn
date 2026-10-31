package phpvpn.android.ui.navigation

import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.navigation.NavGraphBuilder
import androidx.navigation.NavHostController
import androidx.navigation.compose.composable
import androidx.navigation.compose.navigation
import androidx.navigation.toRoute
import phpvpn.android.R
import phpvpn.android.ui.components.ActionStyle
import phpvpn.android.ui.components.Pager

/** First sample target of the design (19); real targets come from ContractsModel (WP10). */
private const val PLACEHOLDER_CONTRACT_IP = "10.80.0.2"

/** Sample page count for the placeholder pager (252 targets, 4 per page in the design). */
private const val PLACEHOLDER_PAGE_SIZE = 4
private const val PLACEHOLDER_TARGETS = 252

/**
 * WP10: Communication contracts (19, 41), contract detail (20, 22) and the
 * key from a password (21). Everything here is still the WP07 placeholder.
 */
internal fun NavGraphBuilder.contracts(nav: NavHostController) {
    navigation<ContractsGraph>(startDestination = Contracts()) {
        composable<Contracts>(deepLinks = link(MapPaths.CONTRACTS)) { entry ->
            val page = entry.toRoute<Contracts>().page.coerceAtLeast(1)
            val leave = rememberUnsavedChangesGuard(PLACEHOLDER_HAS_DRAFT) { nav.leaveContracts() }
            val pages = (PLACEHOLDER_TARGETS + PLACEHOLDER_PAGE_SIZE - 1) / PLACEHOLDER_PAGE_SIZE
            val first = (page - 1) * PLACEHOLDER_PAGE_SIZE + 1
            val last = minOf(page * PLACEHOLDER_PAGE_SIZE, PLACEHOLDER_TARGETS)
            PlaceholderScreen(
                if (page == 1) R.string.contracts_title else R.string.contracts_more_title,
                WP10,
                onBack = leave,
                argument = "page=$page",
                extra = {
                    Pager(
                        pluralStringResource(R.plurals.contracts_page_range, PLACEHOLDER_TARGETS, first, last, PLACEHOLDER_TARGETS),
                        previousLabel = stringResource(R.string.contracts_page_previous),
                        onPrevious = if (page > 1) ({ nav.openContractsPage(page - 1) }) else null,
                        nextLabel = stringResource(R.string.contracts_page_next),
                        onNext = if (page < pages) ({ nav.openContractsPage(page + 1) }) else null,
                    )
                },
                links = listOf(
                    navAction(R.string.contract_detail_title) { nav.navigate(ContractDetail(PLACEHOLDER_CONTRACT_IP)) },
                    navAction(R.string.save_unknown_title) { nav.showSaveUnknown(SaveOrigin.Contracts) },
                ),
                footer = listOf(
                    PlaceholderAction(
                        pluralStringResource(R.plurals.contracts_save_changes, 2, 2),
                        { nav.showSaved(SaveOrigin.Contracts) },
                        ActionStyle.Primary,
                    ),
                    navAction(R.string.action_discard_changes, ActionStyle.Ghost, leave),
                ),
            )
        }
        composable<ContractDetail>(deepLinks = link(MapPaths.CONTRACT_DETAIL)) { entry ->
            val ip = entry.toRoute<ContractDetail>().ip
            // Discarding a detail returns to the list; the list keeps its other draft changes.
            val leave = rememberUnsavedChangesGuard(PLACEHOLDER_HAS_DRAFT) { nav.popBackStack() }
            PlaceholderScreen(
                R.string.contract_detail_title,
                WP10,
                onBack = leave,
                argument = "ip=$ip",
                links = listOf(navAction(R.string.contract_detail_from_password) { nav.navigate(ContractPassword(ip)) }),
                footer = listOf(navAction(R.string.contract_detail_apply, ActionStyle.Primary) { nav.popBackStack() }),
            )
        }
        composable<ContractPassword>(deepLinks = link(MapPaths.CONTRACT_PASSWORD)) { entry ->
            val ip = entry.toRoute<ContractPassword>().ip
            val leave = rememberUnsavedChangesGuard(PLACEHOLDER_HAS_DRAFT) { nav.popBackStack() }
            PlaceholderScreen(
                R.string.password_key_title,
                WP10,
                onBack = leave,
                argument = "ip=$ip",
                footer = listOf(navAction(R.string.password_key_use, ActionStyle.Primary) { nav.popBackStack() }),
            )
        }
    }
}
