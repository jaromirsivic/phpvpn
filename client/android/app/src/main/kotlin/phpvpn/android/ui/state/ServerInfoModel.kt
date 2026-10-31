package phpvpn.android.ui.state

import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.JsonChecks
import phpvpn.android.bridge.ipc.MaskedContract
import phpvpn.android.bridge.ipc.ServerInfoResult
import phpvpn.android.bridge.ipc.StrictJson
import phpvpn.android.bridge.ipc.WireFormats

/** Port of `phpvpn::ServerClientState`; the order is the sort order of the list. */
enum class ServerClientState { VALID, INVALID, OFFLINE }

/** Port of `ServerInfoRow`: a remote client of the server (the local one is omitted). */
data class ServerInfoRow(
    val peer: String,
    val contract: String,
    val lastSeenMs: Long,
    val state: ServerClientState,
)

/** Port of `ServerInfoModel`: the classified rows of one verified snapshot. */
data class ServerInfoModel(
    /** The authenticated plaintext exactly as received; screen 29 shows it verbatim. */
    val infoPlaintext: String,
    val localAddress: String,
    val rows: List<ServerInfoRow>,
    val onlineClients: Int,
    val validOnlineClients: Int,
)

sealed interface ServerInfoModelResult {
    data class Ok(val model: ServerInfoModel) : ServerInfoModelResult

    /** The snapshot cannot be verified; [code] is the ErrorCode wire name. */
    data class Invalid(val code: String) : ServerInfoModelResult
}

/**
 * Builds the Server destination model from a validated `get-server-info`
 * result. WP04 replaces [KotlinServerInfoModel] with the JNI binding of the
 * native `buildServerInfoModel` (core/presentation/server_info_model.cpp)
 * through [phpvpn.android.AppContainer]; the screens only see this interface.
 */
fun interface ServerInfoModelSource {
    fun build(result: ServerInfoResult): ServerInfoModelResult
}

/**
 * Kotlin mirror of `buildServerInfoModel` and `ResponseParser::parseInfoPlaintext`
 * with the extended source (core/protocol/response_parser.cpp) and
 * `parseMaskedContracts` (core/protocol/info_extensions.cpp), so the list,
 * counts and validity match the desktop exactly until WP04 lands.
 */
object KotlinServerInfoModel : ServerInfoModelSource {
    private const val ONLINE_WINDOW_MS = 60_000L
    private const val MAX_PLAINTEXT_BYTES = 262_144L
    private const val MAX_CLIENTS = 253
    private const val MAX_MASKED_CONTRACTS = 252
    private val LIMITS = listOf(
        "maxDataRecordsPerRequest", "maxRequestBodyBytes", "maxQueueBytesPerClient",
        "queueTtlSeconds", "minDataPollIntervalMs", "maxDataPollIntervalMs",
    )
    private val INVALID = ServerInfoModelResult.Invalid(ErrorCode.RESPONSE_INVALID.wireName)

    private class Client(val address: Long, val lastSeenMs: Long, val masked: List<Pair<Long, String>>)

    override fun build(result: ServerInfoResult): ServerInfoModelResult {
        val local = WireFormats.parseIpv4(result.localAddress) ?: return INVALID
        val localContracts = maskedContracts(result.localMaskedCommunicationContracts, local) ?: return INVALID
        val info = parseInfoPlaintext(result.infoPlaintext, local) ?: return INVALID
        val (generatedAt, clients) = info
        var online = 0
        var validOnline = 0
        val rows = ArrayList<Pair<Long, ServerInfoRow>>()
        for (client in clients) {
            if (client.address == local) continue
            var state = ServerClientState.OFFLINE
            if (generatedAt - client.lastSeenMs <= ONLINE_WINDOW_MS) {
                online++
                state = ServerClientState.INVALID
                val found = localContracts.firstOrNull { it.first == client.address }
                if (found != null && client.masked.isNotEmpty() && found.second == client.masked.first().second) {
                    state = ServerClientState.VALID
                    validOnline++
                }
            }
            val peer = WireFormats.formatIpv4(client.address)
            rows += client.address to ServerInfoRow(peer, canonicalContractId(local, client.address), client.lastSeenMs, state)
        }
        rows.sortWith(compareBy<Pair<Long, ServerInfoRow>> { it.second.state.ordinal }.thenBy { it.first })
        return ServerInfoModelResult.Ok(
            ServerInfoModel(result.infoPlaintext, WireFormats.formatIpv4(local), rows.map { it.second }, online, validOnline),
        )
    }

    /** `canonicalContractId`: the lower address first, joined by an underscore. */
    fun canonicalContractId(a: Long, b: Long): String {
        val (low, high) = if (b < a) b to a else a to b
        return WireFormats.formatIpv4(low) + "_" + WireFormats.formatIpv4(high)
    }

    /** `validInfoAddress`: host part 2 to 254. */
    private fun validInfoAddress(address: Long) = (address and 255) in 2..254

    private fun maskedContracts(contracts: List<MaskedContract>, owner: Long): List<Pair<Long, String>>? {
        if (contracts.size > MAX_MASKED_CONTRACTS) return null
        val seen = HashSet<Long>()
        return contracts.map { contract ->
            val peer = WireFormats.parseIpv4(contract.peer) ?: return null
            if (!WireFormats.isLowercaseHex64(contract.hash) || !validInfoAddress(peer) || peer == owner ||
                (peer shr 8) != (owner shr 8) || !seen.add(peer)
            ) return null
            peer to contract.hash
        }
    }

    /** `parseMaskedContracts` over JSON (the extension member of each info client). */
    private fun maskedContracts(value: JsonElement?, owner: Long): List<Pair<Long, String>>? {
        val array = value as? JsonArray ?: return null
        if (array.size > MAX_MASKED_CONTRACTS) return null
        val parsed = array.map { item ->
            if (!JsonChecks.shape(item, "peer", "hash")) return null
            item as JsonObject
            MaskedContract(JsonChecks.string(item["peer"]) ?: return null, JsonChecks.string(item["hash"]) ?: return null)
        }
        return maskedContracts(parsed, owner)
    }

    private fun parseInfoPlaintext(text: String, extendedSrc: Long): Pair<Long, List<Client>>? {
        if (WireFormats.utf8Length(text) > MAX_PLAINTEXT_BYTES) return null
        val document = StrictJson.parse(text) as? JsonObject ?: return null
        JsonChecks.string(document["serverVersion"]) ?: return null
        val generatedAt = WireFormats.parseIso8601(JsonChecks.string(document["generatedAt"])) ?: return null
        val limits = document["limits"] as? JsonObject ?: return null
        val values = LIMITS.map { name -> JsonChecks.int64(limits[name])?.takeIf { it >= 0 } ?: return null }
        if (values[0] <= 0 || values[1] <= 0) return null
        val clients = document["clients"] as? JsonArray ?: return null
        if (clients.isEmpty() || clients.size > MAX_CLIENTS || !validInfoAddress(extendedSrc)) return null
        val seen = HashSet<Long>()
        val parsed = clients.map { element ->
            val client = element as? JsonObject ?: return null
            val address = WireFormats.parseIpv4(JsonChecks.string(client["address"])) ?: return null
            val lastSeen = WireFormats.parseIso8601(JsonChecks.string(client["lastSeen"])) ?: return null
            if (!seen.add(address)) return null
            if (!validInfoAddress(address) || (address shr 8) != (extendedSrc shr 8)) return null
            if (!client.containsKey("maskedCommunicationContracts")) return null
            val masked = maskedContracts(client["maskedCommunicationContracts"], address) ?: return null
            if (masked.size > (if (address == extendedSrc) 0 else 1)) return null
            if (masked.any { it.first != extendedSrc }) return null
            Client(address, lastSeen, masked)
        }
        return generatedAt to parsed
    }
}

/** Port of core/presentation/table_pagination.h: 20 rows per page, at most seven page buttons. */
object TablePagination {
    const val PAGE_SIZE = 20

    fun pageCount(rows: Int): Int = maxOf(1, (rows + PAGE_SIZE - 1) / PAGE_SIZE)

    fun pageNumbers(count: Int, current: Int): List<Int> {
        if (count <= 1) return listOf(0)
        val start = if (count <= 7) 1 else (current - 2).coerceIn(1, count - 6)
        val end = if (count <= 7) count - 2 else start + 4
        return listOf(0) + (start..end).toList() + (count - 1)
    }

    fun <T> page(rows: List<T>, page: Int): List<T> {
        val from = (page * PAGE_SIZE).coerceIn(0, rows.size)
        return rows.subList(from, minOf(rows.size, from + PAGE_SIZE))
    }
}
