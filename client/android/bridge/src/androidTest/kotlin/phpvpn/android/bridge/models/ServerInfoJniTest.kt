package phpvpn.android.bridge.models

import androidx.test.ext.junit.runners.AndroidJUnit4
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.ServerInfoReply
import phpvpn.android.bridge.ipc.ServerInfoResult
import phpvpn.android.bridge.ipc.StrictJson
import phpvpn.android.bridge.jni.NativeServerInfo
import phpvpn.android.bridge.jni.NativeServerInfoHook
import phpvpn.android.bridge.jni.ServerClientState
import phpvpn.android.bridge.jni.ServerInfoParse
import phpvpn.android.bridge.runtime.RuntimeHarness

/**
 * The native ServerInfoModel through JNI (WP04), ported from
 * client/tests/unit/test_server_info_model.cpp with the same synthetic
 * snapshot: peers aged 60000, 59999 and 60001 ms with matching and different
 * masked contracts, sent as the exact get-server-info reply.
 */
@RunWith(AndroidJUnit4::class)
class ServerInfoJniTest {
    private val id = "8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c30"
    private val now = Instant.parse("2026-09-13T12:00:00.000Z").toEpochMilli()
    private val iso = DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'").withZone(ZoneOffset.UTC)

    private fun address(value: Long) = "${value shr 24 and 0xff}.${value shr 16 and 0xff}.${value shr 8 and 0xff}.${value and 0xff}"

    private fun contract(peer: String, hash: String) = JsonObject(linkedMapOf("peer" to JsonPrimitive(peer), "hash" to JsonPrimitive(hash)))

    /** `fixture(count)` of the desktop test as a get-server-info reply; [localHashes] overrides hashes by index. */
    private fun reply(count: Int, localHashes: Map<Int, String> = emptyMap(), mutate: (String) -> String = { it }): String {
        val src = 0x0a636302L // 10.99.99.2
        val clients = mutableListOf<JsonObject>(
            JsonObject(linkedMapOf("address" to JsonPrimitive(address(src)), "lastSeen" to JsonPrimitive(iso.format(Instant.ofEpochMilli(now))), "maskedCommunicationContracts" to JsonArray(emptyList()))),
        )
        val local = mutableListOf<JsonObject>()
        for (i in 0 until count) {
            val peer = address(src + i + 1)
            val age = when (i % 3) { 0 -> 60_000L; 1 -> 59_999L; else -> 60_001L }
            clients += JsonObject(
                linkedMapOf(
                    "address" to JsonPrimitive(peer),
                    "lastSeen" to JsonPrimitive(iso.format(Instant.ofEpochMilli(now - age))),
                    "maskedCommunicationContracts" to JsonArray(listOf(contract(address(src), "a".repeat(64)))),
                ),
            )
            local += contract(peer, localHashes[i] ?: (if (i % 3 == 0) "a" else "b").repeat(64))
        }
        val limits = JsonObject(
            linkedMapOf(
                "maxDataRecordsPerRequest" to JsonPrimitive(16), "maxRequestBodyBytes" to JsonPrimitive(983040),
                "maxQueueBytesPerClient" to JsonPrimitive(16777216), "queueTtlSeconds" to JsonPrimitive(60),
                "minDataPollIntervalMs" to JsonPrimitive(50), "maxDataPollIntervalMs" to JsonPrimitive(3200),
            ),
        )
        val plaintext = mutate(
            JsonObject(
                linkedMapOf(
                    "serverVersion" to JsonPrimitive("test"), "generatedAt" to JsonPrimitive(iso.format(Instant.ofEpochMilli(now))),
                    "clients" to JsonArray(clients), "limits" to limits,
                ),
            ).toString(),
        )
        val result = JsonObject(
            linkedMapOf("infoPlaintext" to JsonPrimitive(plaintext), "localAddress" to JsonPrimitive(address(src)), "localMaskedCommunicationContracts" to JsonArray(local)),
        )
        return JsonObject(linkedMapOf("ipcVersion" to JsonPrimitive(1), "requestId" to JsonPrimitive(id), "result" to result)).toString()
    }

    private fun parsed(text: String) = (NativeServerInfo.parse(text.encodeToByteArray()) as ServerInfoParse.Parsed).view

    @Test
    fun states_boundaries_count_sort_and_pages() {
        for (count in listOf(0, 1, 20, 21, 141, 252)) {
            val view = parsed(reply(count))
            assertEquals(count, view.rows.size)
            assertEquals((count + 2) / 3, view.validOnlineClients)
            assertEquals(count - count / 3, view.onlineClients)
            for (i in 1 until view.rows.size) {
                val a = view.rows[i - 1]
                val b = view.rows[i]
                assertTrue(a.state <= b.state)
            }
            assertTrue(NativeServerInfo.pageNumbers(NativeServerInfo.pageCount(count), 0).size <= 7)
            assertEquals(maxOf(1, (count + 19) / 20), view.pageCount)
        }
        val view = parsed(reply(3))
        assertEquals(listOf(ServerClientState.VALID, ServerClientState.INVALID, ServerClientState.OFFLINE), view.rows.map { it.state })
        assertEquals("10.99.99.2_10.99.99.3", view.rows[0].contract)
        val refreshed = parsed(reply(3, mapOf(1 to "a".repeat(64))))
        assertEquals(2, refreshed.validOnlineClients)
        assertEquals(view.infoPlaintext, refreshed.infoPlaintext)
        assertEquals(listOf(0), NativeServerInfo.pageNumbers(1, 0))
        assertEquals(listOf(0, 1, 2, 3, 4, 5, 12), NativeServerInfo.pageNumbers(13, 0))
        assertEquals(listOf(0, 5, 6, 7, 8, 9, 12), NativeServerInfo.pageNumbers(13, 7))
        val full = parsed(reply(252))
        assertEquals(13, full.pageCount)
        assertEquals(20, full.page(11).size)
        assertEquals(12, full.page(12).size)
    }

    @Test
    fun ipc_exact_schema_and_incompatible_server() {
        val view = parsed(reply(252))
        assertEquals(252, view.rows.size)
        val duplicate = reply(2).replace("\"localMaskedCommunicationContracts\":[", "\"localMaskedCommunicationContracts\":[{\"peer\":\"10.99.99.3\",\"hash\":\"${"a".repeat(64)}\"},")
        assertTrue(NativeServerInfo.parse(duplicate.encodeToByteArray()) is ServerInfoParse.Failed)
        val extra = reply(2).replace("{\"ipcVersion\":1,", "{\"unexpected\":true,\"ipcVersion\":1,")
        assertEquals(ServerInfoParse.Failed(ErrorCode.IPC_MALFORMED), NativeServerInfo.parse(extra.encodeToByteArray()))
        assertEquals(
            ServerInfoParse.Failed(ErrorCode.SERVER_VERSION_INCOMPATIBLE),
            NativeServerInfo.parse(RuntimeHarness.fixture("server-info-incompatible.json", id).encodeToByteArray()),
        )
        val fixture = parsed(RuntimeHarness.fixture("server-info.json", id))
        assertEquals("10.99.99.2", fixture.localAddress)
        assertEquals(0, fixture.rows.size)
    }

    @Test
    fun future_time_missing_contract_and_exact_status_text() {
        val future = parsed(reply(1) { it.replace("2026-09-13T11:59:00.000Z", "2026-09-13T12:01:00.000Z") })
        assertEquals(1, future.validOnlineClients)
        val noLocal = reply(1).replace(Regex("\"localMaskedCommunicationContracts\":\\[[^\\]]*\\]"), "\"localMaskedCommunicationContracts\":[]")
        val view = parsed(noLocal)
        assertEquals(ServerClientState.INVALID, view.rows.first().state)
        assertEquals(1, view.onlineClients)
        assertEquals(0, view.validOnlineClients)
        assertEquals("(1 online client, 0 valid contracts)", view.countText)
        assertEquals("Online - valid communication contract", NativeServerInfo.stateText(ServerClientState.VALID))
        assertEquals("Online - invalid communication contract", NativeServerInfo.stateText(ServerClientState.INVALID))
        assertEquals("Offline", NativeServerInfo.stateText(ServerClientState.OFFLINE))
    }

    @Test
    fun independent_count_pluralization_and_the_hook() {
        assertEquals("(168 online clients, 84 valid contracts)", parsed(reply(252)).countText)
        assertEquals("(0 online clients, 0 valid contracts)", parsed(reply(0)).countText)
        assertEquals("(1 online client, 1 valid contract)", parsed(reply(1)).countText)
        assertEquals("(2 online clients, 1 valid contract)", parsed(reply(2)).countText)

        val hook = NativeServerInfoHook()
        val text = reply(3)
        val root = StrictJson.parse(text) as JsonObject
        val result = phpvpn.android.bridge.ipc.IpcJson.decodeFromJsonElement(ServerInfoResult.serializer(), root["result"]!!)
        assertTrue(hook.accept(ServerInfoReply(result, text)))
        assertEquals(3, hook.latest.value!!.rows.size)
        val incompatible = RuntimeHarness.fixture("server-info-incompatible.json", id)
        assertTrue(!hook.accept(ServerInfoReply(result, incompatible)))
        assertEquals(ErrorCode.SERVER_VERSION_INCOMPATIBLE, hook.lastError.value)
        assertEquals(3, hook.latest.value!!.rows.size)
    }
}
