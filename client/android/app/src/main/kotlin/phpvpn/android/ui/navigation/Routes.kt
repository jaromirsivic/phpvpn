package phpvpn.android.ui.navigation

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

// Type-safe routes of Plans/2026-10-31-09-40-08/android-screen-map.md. The
// serial names follow the map's paths; arguments become path segments
// (required) or query parameters (with a default). Every route also has an
// internal deep link phpvpn://app/<map path> (see [MapPaths]) used by the
// debug route switcher and by UI tests; no intent filter exports them.

// Main destinations (bottom navigation). Connection, Traffic and Server are
// one route each: the states 01-08, 26/27/43 and 28/44/45 are variants the
// screen renders from its state holder, not separate routes.

/** 01, 02, 04, 05, 06, 07, 08, 38: connection status. */
@Serializable @SerialName("connection")
data object Connection

/** 48: the four checks of the last connection. Opening it never connects. */
@Serializable @SerialName("connection/checks")
data object ConnectionChecks

/** 37: VPN permission denied (03 is the system dialog, not a route). */
@Serializable @SerialName("connection/permission")
data object ConnectionPermission

/** 26, 27, 43: traffic statistics. */
@Serializable @SerialName("traffic")
data object Traffic

/** Tab of the Server destination; `server?tab=raw` opens 29. */
enum class ServerTab { Online, Raw }

/** 28, 29, 44, 45: who is online and the raw server data. */
@Serializable @SerialName("server")
data class Server(val tab: ServerTab = ServerTab.Online)

/** 10: settings hub. */
@Serializable @SerialName("settings")
data object Settings

/** 00: first launch, shown instead of Connection while no configuration exists. */
@Serializable @SerialName("welcome")
data object Welcome

// Wizard (WP09). The nested graph lets Discard (32) and Cancel leave the whole
// wizard in one pop. Saved (17) and Expired (47) replace the wizard on the
// back stack, so they live outside the graph.

@Serializable @SerialName("wizard")
data object WizardGraph

/** 11: disconnect gate; skips itself when the client is already disconnected. */
@Serializable @SerialName("wizard/gate")
data object WizardGate

/** 12 and its reset dialog 40. */
@Serializable @SerialName("wizard/url")
data object WizardUrl

/** 13 */
@Serializable @SerialName("wizard/headers")
data object WizardHeaders

/** 14 */
@Serializable @SerialName("wizard/secret")
data object WizardSecret

/** 15 */
@Serializable @SerialName("wizard/identity")
data object WizardIdentity

/** 16 */
@Serializable @SerialName("wizard/finish")
data object WizardFinish

/** 18: error of the current wizard step (the error itself comes from the wizard state). */
@Serializable @SerialName("wizard/error")
data object WizardError

/** 17 */
@Serializable @SerialName("wizard/saved")
data object WizardSaved

/** 47 */
@Serializable @SerialName("wizard/expired")
data object WizardExpired

// Contracts (WP10). The draft is kept per target IP across pages, so a page
// switch replaces the list entry instead of stacking pages.

@Serializable @SerialName("contracts/graph")
data object ContractsGraph

/** 19 and 41 (`contracts?page=n`, 1-based). */
@Serializable @SerialName("contracts")
data class Contracts(val page: Int = 1)

/** 20 and 22 (`contracts/{ip}`). */
@Serializable @SerialName("contracts/detail")
data class ContractDetail(val ip: String)

/** 21 (`contracts/{ip}/password`). */
@Serializable @SerialName("contracts/password")
data class ContractPassword(val ip: String)

// Configuration editor (WP10).

@Serializable @SerialName("editor/graph")
data object EditorGraph

/** 23, 39 */
@Serializable @SerialName("editor")
data object Editor

/** 24, 42 */
@Serializable @SerialName("editor/reset")
data object EditorReset

/** Who started a save whose outcome is reported by 33 and 46. */
enum class SaveOrigin { Wizard, Contracts, Editor }

/** 25: saved, offers Reconnect or Connect. */
@Serializable @SerialName("saved")
data object Saved

/** 33: the save outcome is unknown; the wizard checks its receipt, the others re-read the configuration. */
@Serializable @SerialName("save/unknown")
data class SaveUnknown(val origin: SaveOrigin)

/** 46: result of the check after 33. */
@Serializable @SerialName("save/checked")
data class SaveChecked(val origin: SaveOrigin)

/** Which attempt captured the HTTP diagnostics of 30 and 31. */
enum class DiagnosticSource { Connect, Wizard }

/** 30 */
@Serializable @SerialName("diagnostic/response")
data class DiagnosticResponse(val source: DiagnosticSource)

/** 31 */
@Serializable @SerialName("diagnostic/request")
data class DiagnosticRequest(val source: DiagnosticSource)

/** 34 */
@Serializable @SerialName("about")
data object About

/** 36: missing, invalid or unreadable configuration. */
@Serializable @SerialName("config/attention")
data object ConfigAttention

/**
 * 36a (dialog): the configuration file is missing; [path] is the absolute
 * path reported by `get-config-document`, shown verbatim.
 */
@Serializable @SerialName("config/create-default")
data class CreateDefaultConfig(val path: String)

/** 35 (dialog): close the window or disconnect. */
@Serializable @SerialName("exit")
data object Exit

/** Internal deep link patterns: the map's paths under phpvpn://app/. */
object MapPaths {
    const val BASE = "phpvpn://app"
    const val CONNECTION = "$BASE/connection"
    const val CONNECTION_CHECKS = "$BASE/connection/checks"
    const val CONNECTION_PERMISSION = "$BASE/connection/permission"
    const val TRAFFIC = "$BASE/traffic"
    const val SERVER = "$BASE/server?tab={tab}"
    const val SETTINGS = "$BASE/settings"
    const val WELCOME = "$BASE/welcome"
    const val WIZARD_GATE = "$BASE/wizard/gate"
    const val WIZARD_URL = "$BASE/wizard/url"
    const val WIZARD_HEADERS = "$BASE/wizard/headers"
    const val WIZARD_SECRET = "$BASE/wizard/secret"
    const val WIZARD_IDENTITY = "$BASE/wizard/identity"
    const val WIZARD_FINISH = "$BASE/wizard/finish"
    const val WIZARD_SAVED = "$BASE/wizard/saved"
    const val WIZARD_ERROR = "$BASE/wizard/error"
    const val WIZARD_EXPIRED = "$BASE/wizard/expired"
    const val CONTRACTS = "$BASE/contracts?page={page}"
    const val CONTRACT_DETAIL = "$BASE/contracts/{ip}"
    const val CONTRACT_PASSWORD = "$BASE/contracts/{ip}/password"
    const val EDITOR = "$BASE/editor"
    const val EDITOR_RESET = "$BASE/editor/reset"
    const val SAVED = "$BASE/saved"
    const val SAVE_UNKNOWN = "$BASE/save/unknown?origin={origin}"
    const val SAVE_CHECKED = "$BASE/save/checked?origin={origin}"
    const val DIAGNOSTIC_RESPONSE = "$BASE/diagnostic/response?source={source}"
    const val DIAGNOSTIC_REQUEST = "$BASE/diagnostic/request?source={source}"
    const val ABOUT = "$BASE/about"
    const val CONFIG_ATTENTION = "$BASE/config/attention"
    const val CREATE_DEFAULT_CONFIG = "$BASE/config/create-default?path={path}"
    const val EXIT = "$BASE/exit"
}
