package phpvpn.android.ui.state

import java.net.URI
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.serialization.json.JsonObject
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.IpcOutcome
import phpvpn.android.bridge.ipc.JsonChecks
import phpvpn.android.bridge.ipc.StrictJson

/**
 * The public part of the stored configuration shown next to the status:
 * server URL, virtual IP and VPN network (01, 02, 05, 07, 48) and the server
 * host (28, 29). Secrets are never copied out of the document.
 */
data class ConfigSummary(
    val availability: ConfigAvailability,
    val configPath: String,
    val reasonCode: String?,
    val serverUrl: String? = null,
    val virtualIp: String? = null,
    val vpnCidr: String? = null,
) {
    /** Host of [serverUrl] for the Server eyebrow, or null. */
    val serverHost: String?
        get() = serverUrl?.let { url -> runCatching { URI(url).host }.getOrNull()?.takeIf { it.isNotEmpty() } }

    val hasDetails: Boolean get() = serverUrl != null || virtualIp != null || vpnCidr != null
}

sealed interface ConfigSummaryState {
    /** Not read yet: nothing is shown and nothing is claimed. */
    data object Unknown : ConfigSummaryState

    data class Loaded(val summary: ConfigSummary) : ConfigSummaryState

    /** The read failed; [code] selects the text (ErrorStrings), timeouts included. */
    data class Failed(val code: String) : ConfigSummaryState
}

/**
 * Reads `get-config-document` once per request, like the desktop progress
 * window reads the configuration once per Connect (progress_window.cpp). The
 * document text is wiped right after the three public members are taken.
 */
class ConfigSummaryRepository(private val client: IpcClient) {
    private val mutex = Mutex()
    private val _state = MutableStateFlow<ConfigSummaryState>(ConfigSummaryState.Unknown)
    val state: StateFlow<ConfigSummaryState> = _state.asStateFlow()

    suspend fun refresh(): ConfigSummaryState = mutex.withLock {
        val next = when (val outcome = client.getConfigDocument()) {
            is IpcOutcome.Success -> {
                val result = outcome.value
                val document = result.document
                val members = try {
                    document?.read { chars -> publicMembers(String(chars)) }
                } finally {
                    document?.wipe()
                }
                ConfigSummaryState.Loaded(
                    ConfigSummary(
                        availability = result.availability,
                        configPath = result.configPath,
                        reasonCode = result.reasonCode,
                        serverUrl = members?.get(SERVER_URL),
                        virtualIp = members?.get(VIRTUAL_IP),
                        vpnCidr = members?.get(VPN_CIDR),
                    ),
                )
            }
            is IpcOutcome.ServiceError -> ConfigSummaryState.Failed(outcome.code)
            IpcOutcome.TimedOut -> ConfigSummaryState.Failed(ErrorCode.TIMEOUT.wireName)
            is IpcOutcome.Rejected -> ConfigSummaryState.Failed(outcome.code.wireName)
        }
        // A failed re-read keeps the last loaded summary: the card is the
        // configuration, not a live status, and must not flicker away.
        if (next is ConfigSummaryState.Loaded || _state.value !is ConfigSummaryState.Loaded) _state.value = next
        next
    }

    private fun publicMembers(text: String): Map<String, String> {
        val root = StrictJson.parse(text) as? JsonObject ?: return emptyMap()
        val setup = root["masterSetup"] as? JsonObject ?: return emptyMap()
        return listOf(SERVER_URL, VIRTUAL_IP, VPN_CIDR)
            .mapNotNull { name -> JsonChecks.string(setup[name])?.takeIf { it.isNotEmpty() }?.let { name to it } }
            .toMap()
    }

    private companion object {
        const val SERVER_URL = "serverUrl"
        const val VIRTUAL_IP = "virtualIp"
        const val VPN_CIDR = "vpnCidr"
    }
}
