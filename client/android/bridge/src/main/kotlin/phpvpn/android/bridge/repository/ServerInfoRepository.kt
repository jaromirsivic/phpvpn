package phpvpn.android.bridge.repository

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.IpcClient
import phpvpn.android.bridge.ipc.IpcOutcome
import phpvpn.android.bridge.ipc.IpcTimeouts
import phpvpn.android.bridge.ipc.ServerInfoReply

/**
 * Hook for the native `ServerInfoModel` (WP04): the plaintext parsing,
 * classification and paging stay native. It receives the exact validated
 * reply and returns false to reject it (treated like a failed refresh).
 */
fun interface ServerInfoHook {
    fun accept(reply: ServerInfoReply): Boolean

    companion object {
        val ACCEPT_ALL = ServerInfoHook { true }
    }
}

/** Why the last refresh failed; the text comes from resources by [code]. */
data class ServerInfoFailure(val code: String, val timedOut: Boolean = false)

/**
 * Server destination (28, 29, 44, 45). [last] is the last successful snapshot
 * and survives failures (45); without it a failure shows only the explanation
 * and Retry. The raw plaintext is shown exactly as received ([ServerInfoReply.result]).
 */
data class ServerInfoState(
    val last: ServerInfoReply? = null,
    val loading: Boolean = false,
    val failure: ServerInfoFailure? = null,
    val autoRefresh: Boolean = false,
)

/**
 * `get-server-info` with the tray rules: one request at a time, auto refresh
 * off by default, the next automatic refresh 5 s after the previous one
 * completed (including a failure), only while [state] is collected.
 */
class ServerInfoRepository(
    private val client: IpcClient,
    scope: CoroutineScope,
    private val hook: ServerInfoHook = ServerInfoHook.ACCEPT_ALL,
) {
    private val current = MutableStateFlow(ServerInfoState())
    private val inFlight = Mutex()

    val state: StateFlow<ServerInfoState> = flow {
        coroutineScope {
            launch {
                current.map { it.autoRefresh }.distinctUntilChanged().collectLatest { enabled ->
                    while (enabled) {
                        delay(IpcTimeouts.SERVER_INFO_AUTO_REFRESH_MS)
                        refresh()
                    }
                }
            }
            current.collect { emit(it) }
        }
    }.stateIn(scope, SharingStarted.WhileSubscribed(), ServerInfoState())

    val latest: ServerInfoState get() = current.value

    fun setAutoRefresh(enabled: Boolean) {
        current.value = current.value.copy(autoRefresh = enabled)
    }

    /** One refresh; ignored while another is in flight. */
    suspend fun refresh() {
        if (!inFlight.tryLock()) return
        try {
            current.value = current.value.copy(loading = true)
            val outcome = client.getServerInfo()
            current.value = when (outcome) {
                is IpcOutcome.Success -> if (hook.accept(outcome.value)) {
                    current.value.copy(last = outcome.value, loading = false, failure = null)
                } else {
                    current.value.copy(loading = false, failure = ServerInfoFailure(ErrorCode.RESPONSE_INVALID.wireName))
                }
                is IpcOutcome.ServiceError -> current.value.copy(loading = false, failure = ServerInfoFailure(outcome.code))
                IpcOutcome.TimedOut -> current.value.copy(
                    loading = false,
                    failure = ServerInfoFailure(ErrorCode.TIMEOUT.wireName, timedOut = true),
                )
                is IpcOutcome.Rejected -> current.value.copy(loading = false, failure = ServerInfoFailure(outcome.code.wireName))
            }
        } finally {
            inFlight.unlock()
        }
    }
}
