package phpvpn.android.ui.state

import java.util.WeakHashMap
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonElement
import phpvpn.android.bridge.ipc.StrictJson
import phpvpn.android.bridge.ipc.ServerInfoReply
import phpvpn.android.bridge.repository.ServerInfoFailure
import phpvpn.android.bridge.repository.ServerInfoRepository
import phpvpn.android.bridge.repository.ServerInfoState

/** Which variant of the Server destination is shown. */
enum class ServerVariant {
    /** First request still running; nothing is claimed. */
    LOADING,

    /** 28: verified rows. */
    LIST,

    /** 44: a valid response without remote clients (not an error). */
    EMPTY,

    /** 45 with data: the last success stays, marked stale with its refresh time. */
    STALE,

    /** 45 without data: only the explanation and Retry. */
    FAILED,
}

data class ServerUiState(
    val variant: ServerVariant = ServerVariant.LOADING,
    /** Host of the configured server URL for the eyebrow, or null. */
    val serverHost: String? = null,
    /** The last successful model; 28 and 29 always show this same snapshot. */
    val model: ServerInfoModel? = null,
    /** Wall-clock time of the last successful refresh (45 shows it). */
    val refreshedAtUtcMs: Long? = null,
    val loading: Boolean = false,
    val failure: ServerInfoFailure? = null,
    val autoRefresh: Boolean = false,
    val page: Int = 0,
    val pageCount: Int = 1,
    val pageRows: List<ServerInfoRow> = emptyList(),
    /** 29: the plaintext of [model] for reading (see [ServerViewModel.readableJson]). */
    val rawText: String = "",
)

/**
 * Server destination (28, 29, 44, 45). Opening the destination requests one
 * snapshot like the desktop window; auto refresh (off by default, 5 s after
 * the previous refresh completed) runs only while the screen collects
 * [state]. Classification goes through [ServerInfoModelSource].
 */
class ServerViewModel(
    private val repository: ServerInfoRepository,
    private val models: ServerInfoModelSource,
    config: ConfigSummaryRepository,
    private val wallClock: () -> Long = System::currentTimeMillis,
) : ViewModel() {
    private val page = MutableStateFlow(0)
    private var builtFrom: ServerInfoReply? = null
    private var built: ServerInfoModel? = null
    private var builtRaw: String = ""

    val state: StateFlow<ServerUiState> = combine(repository.state, page, config.state) { info, requestedPage, summary ->
        map(info, requestedPage, (summary as? ConfigSummaryState.Loaded)?.summary?.serverHost)
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(), ServerUiState())

    init {
        refresh()
    }

    /** Refresh now / Retry refresh: one request at a time (the repository ignores a second). */
    fun refresh() {
        viewModelScope.launch { repository.refresh() }
    }

    fun setAutoRefresh(enabled: Boolean) = repository.setAutoRefresh(enabled)

    fun showPage(index: Int) {
        page.value = index.coerceAtLeast(0)
    }

    private fun map(info: ServerInfoState, requestedPage: Int, host: String?): ServerUiState {
        val reply = info.last
        if (reply !== builtFrom) {
            builtFrom = reply
            built = reply?.let { (models.build(it.result) as? ServerInfoModelResult.Ok)?.model }
            builtRaw = built?.let { readableJson(it.infoPlaintext) }.orEmpty()
            if (reply != null) refreshedAt.getOrPut(reply) { wallClock() }
        }
        val model = built
        val count = TablePagination.pageCount(model?.rows?.size ?: 0)
        val current = requestedPage.coerceIn(0, count - 1)
        val variant = when {
            model == null && info.failure != null -> ServerVariant.FAILED
            model == null -> ServerVariant.LOADING
            info.failure != null -> ServerVariant.STALE
            model.rows.isEmpty() -> ServerVariant.EMPTY
            else -> ServerVariant.LIST
        }
        return ServerUiState(
            variant = variant,
            serverHost = host,
            model = model,
            refreshedAtUtcMs = reply?.let { refreshedAt[it] },
            loading = info.loading,
            failure = info.failure,
            autoRefresh = info.autoRefresh,
            page = current,
            pageCount = count,
            pageRows = model?.let { TablePagination.page(it.rows, current) } ?: emptyList(),
            rawText = builtRaw,
        )
    }

    companion object {
        private val pretty = Json {
            prettyPrint = true
            prettyPrintIndent = "  "
        }

        /**
         * 29 must be readable: a server that already sends line breaks (the
         * PHP server pretty-prints) is shown verbatim like the desktop window;
         * a single-line reply is indented. Member names, order and number
         * literals stay exactly as received (StrictJson keeps the literals).
         */
        fun readableJson(text: String): String {
            if (text.contains('\n')) return text
            val tree = StrictJson.parse(text) ?: return text
            return pretty.encodeToString(JsonElement.serializer(), tree)
        }

        // The repository outlives the view model; the refresh time belongs to
        // the reply, so a recreated screen keeps the true time (45).
        private val refreshedAt: MutableMap<ServerInfoReply, Long> = java.util.Collections.synchronizedMap(WeakHashMap())
    }
}
