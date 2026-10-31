package phpvpn.android.bridge.jni

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.ServerInfoReply
import phpvpn.android.bridge.repository.ServerInfoHook

/** `ServerClientState`, same order: online with a matching contract, online without, offline. */
enum class ServerClientState { VALID, INVALID, OFFLINE }

/** One row of `ServerInfoModel::rows` (sorted by state, then address). */
data class ServerInfoRow(val peer: String, val contract: String, val lastSeenMs: Long, val state: ServerClientState)

/**
 * The classified `get-server-info` reply (core/presentation/server_info_model.h):
 * rows without the own address, online within 60 s of `generatedAt`, contract
 * validity by the masked hash, and the verified plaintext exactly as received.
 * [countText] is the English desktop text; the UI builds its own from the
 * counts and resources.
 */
data class ServerInfoView(
    val localAddress: String,
    val rows: List<ServerInfoRow>,
    val onlineClients: Int,
    val validOnlineClients: Int,
    val countText: String,
    val infoPlaintext: String,
) {
    /** `tablePageCount`: pages of [PAGE_SIZE] rows, at least one. */
    val pageCount: Int get() = NativeServerInfo.pageCount(rows.size)

    /** The rows of page [page] (0-based). */
    fun page(page: Int): List<ServerInfoRow> = rows.drop(page * NativeServerInfo.PAGE_SIZE).take(NativeServerInfo.PAGE_SIZE)

    override fun toString(): String = "ServerInfoView(rows=${rows.size}, online=$onlineClients, valid=$validOnlineClients)"
}

/** Outcome of [NativeServerInfo.parse]. */
sealed interface ServerInfoParse {
    data class Parsed(val view: ServerInfoView) : ServerInfoParse

    /** `server_version_incompatible` for that error envelope, otherwise the contract error. */
    data class Failed(val code: ErrorCode) : ServerInfoParse
}

/** `ServerInfoModel` over JNI: classification and pagination stay native (plan "Přenosné modely"). */
object NativeServerInfo {
    /** `kTablePageSize`. */
    const val PAGE_SIZE = 20

    /** `parseServerInfoResult` + `buildServerInfoModel` of one exact reply. */
    fun parse(reply: ByteArray): ServerInfoParse {
        val error = arrayOfNulls<ByteArray>(1)
        val handle = ModelNatives.serverInfoParse(reply, error)
        if (handle == 0L) return ServerInfoParse.Failed(nativeError(error[0]) ?: ErrorCode.IPC_MALFORMED)
        val resource = NativeResource(handle) { ModelNatives.serverInfoDestroy(it) }
        return try {
            resource.withHandle { model ->
            val counts = ModelNatives.serverInfoCounts(model) ?: longArrayOf(0, 0, 0, 0)
            val addresses = ModelNatives.serverInfoRowAddresses(model) ?: IntArray(0)
            val states = ModelNatives.serverInfoRowStates(model) ?: IntArray(0)
            val lastSeen = ModelNatives.serverInfoRowLastSeen(model) ?: LongArray(0)
            val rows = addresses.indices.map { index ->
                ServerInfoRow(
                    peer = Ipv4Text.toText(addresses[index]),
                    contract = ModelNatives.serverInfoRowContract(model, index)?.decodeToString() ?: "",
                    lastSeenMs = lastSeen[index],
                    state = ServerClientState.entries[states[index]],
                )
            }
            ServerInfoParse.Parsed(
                ServerInfoView(
                    localAddress = Ipv4Text.toText(counts[3].toInt()),
                    rows = rows,
                    onlineClients = counts[1].toInt(),
                    validOnlineClients = counts[2].toInt(),
                    countText = ModelNatives.serverInfoText(model, 0)?.decodeToString() ?: "",
                    infoPlaintext = ModelNatives.serverInfoText(model, 1)?.decodeToString() ?: "",
                ),
            )
            }
        } finally {
            resource.close()
        }
    }

    /** `serverClientStateText` (English desktop text). */
    fun stateText(state: ServerClientState): String = ModelNatives.serverInfoStateText(state.ordinal)?.decodeToString() ?: ""

    /** `tablePageCount`. */
    fun pageCount(rows: Int): Int = ModelNatives.serverInfoPageCount(rows)

    /** `tablePageNumbers`: at most seven page indexes around [current], first and last always present. */
    fun pageNumbers(count: Int, current: Int): List<Int> = ModelNatives.serverInfoPageNumbers(count, current)?.toList() ?: listOf(0)
}

/**
 * The [ServerInfoHook] of WP06 backed by the native model: a reply the native
 * classification refuses is rejected (the repository then treats it like a
 * failed refresh, screen 45), an accepted one is published in [latest] for
 * the Server screens (WP08).
 */
class NativeServerInfoHook : ServerInfoHook {
    private val current = MutableStateFlow<ServerInfoView?>(null)
    private val failure = MutableStateFlow<ErrorCode?>(null)

    /** The last accepted classification; survives a later failure like the repository's snapshot. */
    val latest: StateFlow<ServerInfoView?> = current.asStateFlow()

    /** The code of the last refused reply, cleared by the next accepted one. */
    val lastError: StateFlow<ErrorCode?> = failure.asStateFlow()

    override fun accept(reply: ServerInfoReply): Boolean = when (val parsed = NativeServerInfo.parse(reply.rawReply.encodeToByteArray())) {
        is ServerInfoParse.Parsed -> {
            current.value = parsed.view
            failure.value = null
            true
        }
        is ServerInfoParse.Failed -> {
            failure.value = parsed.code
            false
        }
    }
}
