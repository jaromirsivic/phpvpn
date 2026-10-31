package phpvpn.android.ui.state

import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.ipc.IpcJson
import phpvpn.android.bridge.ipc.MaskedContract
import phpvpn.android.bridge.ipc.ServerInfoResult
import phpvpn.android.bridge.ipc.WireFormats
import phpvpn.android.bridge.ipc.WireValidation

/**
 * Port of client/tests/unit/test_server_info_model.cpp (ServerInfo_*) and of
 * the pager part of test_server_info_layout.cpp, against the Kotlin mirror of
 * buildServerInfoModel and table_pagination.h.
 */
class ServerInfoModelTest {
    private val src = WireFormats.parseIpv4("10.99.99.2")!!
    private val now = WireFormats.parseIso8601("2026-09-13T12:00:00.000Z")!!

    private fun iso(ms: Long): String = java.time.Instant.ofEpochMilli(ms).toString().let {
        // Instant prints without millis when they are zero; the wire needs exactly .mmmZ.
        if (it.length == 20) it.dropLast(1) + ".000Z" else it
    }

    private fun client(address: Long, lastSeen: Long, masked: List<Pair<Long, String>>) = JsonObject(
        mapOf(
            "address" to JsonPrimitive(WireFormats.formatIpv4(address)),
            "lastSeen" to JsonPrimitive(iso(lastSeen)),
            "maskedCommunicationContracts" to JsonArray(
                masked.map { (peer, hash) -> JsonObject(mapOf("peer" to JsonPrimitive(WireFormats.formatIpv4(peer)), "hash" to JsonPrimitive(hash))) },
            ),
        ),
    )

    private fun plaintext(clients: List<JsonElement>, limits: Map<String, Long> = LIMITS): String = IpcJson.encodeToString(
        JsonElement.serializer(),
        JsonObject(
            mapOf(
                "serverVersion" to JsonPrimitive("test"),
                "generatedAt" to JsonPrimitive(iso(now)),
                "clients" to JsonArray(clients),
                "limits" to JsonObject(limits.mapValues { JsonPrimitive(it.value) }),
            ),
        ),
    )

    /** `fixture(count)` of the C++ test: ages 60000 / 59999 / 60001 and hashes a / b / b by i % 3. */
    private fun fixture(count: Int): ServerInfoResult {
        val clients = mutableListOf<JsonElement>(client(src, now, emptyList()))
        val local = mutableListOf<MaskedContract>()
        for (i in 0 until count) {
            val peer = src + i + 1
            val age = when (i % 3) { 0 -> 60_000L; 1 -> 59_999L; else -> 60_001L }
            clients += client(peer, now - age, listOf(src to "a".repeat(64)))
            local += MaskedContract(WireFormats.formatIpv4(peer), (if (i % 3 == 0) "a" else "b").repeat(64))
        }
        return ServerInfoResult(plaintext(clients), WireFormats.formatIpv4(src), local)
    }

    private fun build(result: ServerInfoResult): ServerInfoModel =
        (KotlinServerInfoModel.build(result) as? ServerInfoModelResult.Ok)?.model ?: throw AssertionError("model rejected")

    private fun invalid(result: ServerInfoResult) = KotlinServerInfoModel.build(result) is ServerInfoModelResult.Invalid

    @Test
    fun ServerInfo_states_boundaries_count_sort_and_pages() {
        for (count in listOf(0, 1, 20, 21, 141, 252)) {
            val snapshot = fixture(count)
            val model = build(snapshot)
            assertEquals(count, model.rows.size)
            assertEquals((count + 2) / 3, model.validOnlineClients)
            assertEquals(count - count / 3, model.onlineClients)
            assertEquals(snapshot.infoPlaintext, model.infoPlaintext)
            for (i in 1 until model.rows.size) {
                val a = model.rows[i - 1]
                val b = model.rows[i]
                assertTrue(a.state <= b.state)
                if (a.state == b.state) assertTrue(WireFormats.parseIpv4(a.peer)!! < WireFormats.parseIpv4(b.peer)!!)
            }
            assertTrue(TablePagination.pageNumbers(TablePagination.pageCount(count), 0).size <= 7)
        }
        val snapshot = fixture(3)
        val model = build(snapshot)
        assertEquals(ServerClientState.VALID, model.rows[0].state)
        assertEquals(ServerClientState.INVALID, model.rows[1].state)
        assertEquals(ServerClientState.OFFLINE, model.rows[2].state)
        assertEquals("10.99.99.2_10.99.99.3", model.rows[0].contract)
        val local = snapshot.localMaskedCommunicationContracts.toMutableList()
        local[1] = local[1].copy(hash = "a".repeat(64))
        val refreshed = build(snapshot.copy(localMaskedCommunicationContracts = local))
        assertEquals(2, refreshed.validOnlineClients)
        assertEquals(model.infoPlaintext, refreshed.infoPlaintext)
    }

    @Test
    fun ServerInfo_ipc_exact_schema_and_private_snapshot() {
        val snapshot = fixture(252)
        assertEquals(252, build(snapshot).rows.size)
        // A duplicated local masked contract is rejected, like parseServerInfoResult.
        val duplicate = snapshot.copy(localMaskedCommunicationContracts = snapshot.localMaskedCommunicationContracts + snapshot.localMaskedCommunicationContracts.first())
        assertTrue(invalid(duplicate))
        // An unexpected member of the result is rejected by the closed schema.
        val result = IpcJson.encodeToJsonElement(ServerInfoResult.serializer(), snapshot) as JsonObject
        assertTrue(WireValidation.serverInfo(result))
        assertFalse(WireValidation.serverInfo(JsonObject(result + ("unexpected" to JsonPrimitive(true)))))
    }

    @Test
    fun ServerInfo_future_time_missing_contract_and_exact_status() {
        val snapshot = fixture(1)
        val old = iso(now - 60_000)
        assertEquals("2026-09-13T11:59:00.000Z", old)
        assertTrue(snapshot.infoPlaintext.contains(old))
        val future = snapshot.copy(infoPlaintext = snapshot.infoPlaintext.replace(old, "2026-09-13T12:01:00.000Z"))
        assertEquals(1, build(future).validOnlineClients)
        val noContract = build(future.copy(localMaskedCommunicationContracts = emptyList()))
        assertEquals(ServerClientState.INVALID, noContract.rows.first().state)
        assertEquals(1, noContract.onlineClients)
        assertEquals(0, noContract.validOnlineClients)
    }

    @Test
    fun ServerInfo_independent_counts() {
        val model = build(fixture(252))
        assertEquals(168, model.onlineClients)
        assertEquals(84, model.validOnlineClients)
        val empty = build(fixture(0))
        assertEquals(0, empty.onlineClients)
        assertEquals(0, empty.validOnlineClients)
        assertTrue(empty.rows.isEmpty())
    }

    @Test
    fun ServerInfo_local_client_is_omitted_and_plaintext_is_verified() {
        val base = fixture(2)
        assertTrue(build(base).rows.none { it.peer == "10.99.99.2" })
        // The server always lists the local client: an empty array cannot be verified.
        assertTrue(invalid(base.copy(infoPlaintext = plaintext(emptyList()))))
        // A client outside the /24 of the local address.
        assertTrue(invalid(base.copy(infoPlaintext = plaintext(listOf(client(src, now, emptyList()), client(WireFormats.parseIpv4("10.99.98.3")!!, now, emptyList()))))))
        // Duplicate client address.
        assertTrue(invalid(base.copy(infoPlaintext = plaintext(listOf(client(src, now, emptyList()), client(src, now, emptyList()))))))
        // The local client must not publish contracts; a peer at most one, and only with the local address.
        assertTrue(invalid(base.copy(infoPlaintext = plaintext(listOf(client(src, now, listOf(src + 1 to "a".repeat(64))))))))
        assertTrue(invalid(base.copy(infoPlaintext = plaintext(listOf(client(src, now, emptyList()), client(src + 1, now, listOf(src + 2 to "a".repeat(64))))))))
        // Unusable limits.
        assertTrue(invalid(base.copy(infoPlaintext = plaintext(listOf(client(src, now, emptyList())), LIMITS + ("maxDataRecordsPerRequest" to 0L)))))
        assertTrue(invalid(base.copy(infoPlaintext = plaintext(listOf(client(src, now, emptyList())), LIMITS + ("queueTtlSeconds" to -1L)))))
        assertTrue(invalid(base.copy(infoPlaintext = plaintext(listOf(client(src, now, emptyList())), LIMITS - "queueTtlSeconds"))))
        // Not JSON, or above 256 KiB.
        assertTrue(invalid(base.copy(infoPlaintext = "not json")))
        assertTrue(invalid(base.copy(infoPlaintext = " ".repeat(262_145))))
    }

    @Test
    fun TablePagination_matches_table_pagination_h() {
        assertEquals(1, TablePagination.pageCount(0))
        assertEquals(1, TablePagination.pageCount(20))
        assertEquals(2, TablePagination.pageCount(21))
        assertEquals(13, TablePagination.pageCount(252))
        assertEquals(listOf(0), TablePagination.pageNumbers(1, 0))
        assertEquals(listOf(0, 1), TablePagination.pageNumbers(2, 1))
        assertEquals(listOf(0, 1, 2, 3, 4, 5, 6), TablePagination.pageNumbers(7, 3))
        assertEquals(listOf(0, 1, 2, 3, 4, 5, 12), TablePagination.pageNumbers(13, 0))
        assertEquals(listOf(0, 4, 5, 6, 7, 8, 12), TablePagination.pageNumbers(13, 6))
        assertEquals(listOf(0, 7, 8, 9, 10, 11, 12), TablePagination.pageNumbers(13, 12))
        assertEquals(20, TablePagination.page((1..45).toList(), 0).size)
        assertEquals(5, TablePagination.page((1..45).toList(), 2).size)
        assertTrue(TablePagination.page((1..45).toList(), 9).isEmpty())
    }

    /**
     * The geometric checks of ServerInfo_layout_dpi_small_screen_and_footer do
     * not apply to Compose; what carries over is that the pager never needs
     * more than seven page buttons plus first/previous/next/last, for every
     * page of every list size the server can return (0..252 remote clients).
     */
    @Test
    fun ServerInfo_layout_pager_fits_for_every_page() {
        for (rows in 0..252) {
            val count = TablePagination.pageCount(rows)
            for (current in 0 until count) {
                val pages = TablePagination.pageNumbers(count, current)
                assertTrue(pages.size <= 7)
                assertEquals(0, pages.first())
                assertEquals(count - 1, pages.last())
                assertEquals(pages.sorted().distinct(), pages)
                assertTrue(current in pages || count > 7)
            }
        }
    }

    @Test
    fun rawView29IsReadableWithoutRenamingOrRewritingLiterals() {
        val pretty = "{\n    \"serverVersion\": \"x\"\n}"
        assertEquals(pretty, ServerViewModel.readableJson(pretty))
        val compact = """{"serverVersion":"x","n":18446744073709551615,"f":1.50e3,"clients":[{"address":"10.0.0.2"}]}"""
        val shown = ServerViewModel.readableJson(compact)
        assertTrue(shown.contains("\n  \"serverVersion\": \"x\""))
        assertTrue(shown.contains("18446744073709551615"))
        assertTrue(shown.contains("1.50e3"))
        assertTrue(shown.indexOf("serverVersion") < shown.indexOf("clients"))
        assertEquals("not json", ServerViewModel.readableJson("not json"))
    }

    private companion object {
        val LIMITS = linkedMapOf(
            "maxDataRecordsPerRequest" to 16L, "maxRequestBodyBytes" to 983040L, "maxQueueBytesPerClient" to 16777216L,
            "queueTtlSeconds" to 60L, "minDataPollIntervalMs" to 50L, "maxDataPollIntervalMs" to 3200L,
        )
    }
}
