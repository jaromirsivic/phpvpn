package phpvpn.android.standin

import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.controller.ContractColumn
import phpvpn.android.bridge.controller.ContractFocus
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.IPC_MAX_MESSAGE_BYTES
import phpvpn.android.bridge.ipc.IpcJson
import phpvpn.android.bridge.ipc.StrictJson
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.ipc.WireFormats
import phpvpn.android.debug.ConfigReplies

/** Values of a [StandInResult] that must have succeeded. */
internal fun <T> StandInResult<T>.ok(): T = (this as? StandInResult.Ok<T>)?.value ?: error("expected success, got $this")

internal fun StandInResult<*>.code(): ErrorCode? = (this as? StandInResult.Failed)?.code

/** `document()` of contracts_fixture.h: the editor template without communicationContracts. */
internal fun contractsDocument(local: String = "10.99.99.2", cidr: String = "10.99.99.0/24", contractsPresent: Boolean = false) =
    ConfigReplies.document(local, cidr, if (contractsPresent) emptyMap() else null)

/** `oneContractDocument()` of one_contract_fixture.h. */
internal fun oneContractDocument() =
    ConfigReplies.document(contracts = mapOf("10.99.99.3" to "918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d"))

internal fun secret(text: String) = SecretText.of(text)

/** Counting random source of contracts_fixture.h: byte i = i, optional failure. */
internal class CountingRandom : RandomSource {
    var fail = false
    var calls = 0
    var size = 0

    override fun fill(bytes: ByteArray): Boolean {
        calls++
        size = bytes.size
        for (i in bytes.indices) bytes[i] = i.toByte()
        return !fail
    }
}

/**
 * Port of client/tests/unit/test_contracts_model.cpp for the Kotlin stand-in
 * of `ContractsModel` (one test per C++ case, same names). The crypto counters
 * of the C++ fixture become [CountingRandom]; the model never hashes.
 */
class StandInContractsModelTest {
    private fun model(document: String = contractsDocument(), path: String = "", revision: String = "") =
        StandInContractsModel.create(document, path, revision).ok()

    private fun key(model: StandInContractsModel, ip: String): String = model.row(ip)!!.keyText.let { k -> k.reveal().also { k.wipe() } }

    @Test
    fun all_peers_numeric_order_self_and_address_boundaries() {
        for (local in listOf("10.99.99.2", "10.99.99.128", "10.99.99.254")) {
            val m = model(contractsDocument(local), "synthetic/config", "old-revision")
            val rows = m.rows()
            assertEquals(252, rows.size)
            assertNull(m.row(local))
            var previous = 0L
            for (row in rows) {
                val value = WireFormats.parseIpv4(row.remoteIp)!!
                assertTrue(previous < value)
                previous = value
                assertTrue(!row.enabled && row.keyText.isEmpty() && !row.keyValid && !row.keyErrorVisible && !row.dirty)
            }
            assertFalse(m.dirty)
            assertEquals("synthetic/config", m.configPath)
            assertEquals("old-revision", m.revisionToken)
        }
        for ((cidr, local) in listOf("0.0.0.0/29" to "0.0.0.2", "172.16.2.0/29" to "172.16.2.6", "255.255.255.248/29" to "255.255.255.254")) {
            assertEquals(4, model(contractsDocument(local, cidr)).rows().size)
        }
    }

    @Test
    fun invalid_range_and_repair_rejected_before_default_hashes() {
        for (cidr in listOf("0.0.0.0/0", "10.99.0.0/16", "10.99.98.0/23", "10.99.99.0/30", "10.99.99.0/31", "10.99.99.0/32", "10.99.99.1/24", "10.99.99.0/999999999999999")) {
            assertEquals(cidr, ErrorCode.CONFIG_INVALID_CIDR, StandInContractsModel.create(contractsDocument("10.99.99.2", cidr)).code())
        }
        assertEquals(ErrorCode.CONFIG_INVALID_CIDR, checkedContractPeerCount(null, null).code())
        for (local in listOf("10.99.99.0", "10.99.99.1", "10.99.99.255", "10.99.98.2")) {
            assertEquals(local, ErrorCode.CONFIG_INVALID_ADDRESS, StandInContractsModel.create(contractsDocument(local)).code())
        }
        val repairable = contractsDocument().replace("    \"accessKey\": \"0202020202020202020202020202020202020202020202020202020202020202\",\n", "")
        assertNotNull(StandInContractsModel.create(repairable).code())
    }

    @Test
    fun all_enabled_252_rows_publish_and_multiple_errors_are_counted() {
        val m = model(contractsDocument("10.20.30.10", "10.20.30.0/24"))
        assertEquals("10.20.30.2", m.rows().first().remoteIp)
        assertEquals("10.20.30.2_10.20.30.10", m.target("10.20.30.2")!!.contractId)
        for (row in m.rows()) {
            assertNull(m.setKey(row.remoteIp, secret("a".repeat(64))))
            assertNull(m.setEnabled(row.remoteIp, true))
        }
        val candidate = m.prepareSave(StandInEnvelopeSizer).ok()
        assertEquals(252, (candidate.dom[StandInContractsDocument.CONTRACTS] as JsonObject).size)
        assertTrue(m.resumeEditing(m.saveGeneration))
        val first = m.rows().first().remoteIp
        val last = m.rows().last().remoteIp
        assertNull(m.setKey(first, secret("")))
        assertNull(m.setKey(last, secret("bad")))
        assertEquals(2, m.blockingErrors)
        assertEquals(first, m.firstBlockingFocus!!.remoteIp)
        assertNull(m.setEnabled(first, false))
        assertEquals(1, m.blockingErrors)
        assertEquals(last, m.firstBlockingFocus!!.remoteIp)
        assertNull(m.setEnabled(last, false))
        assertTrue(m.canPrepareSave)
    }

    @Test
    fun exact_existing_key_shared_defaults_and_pair_vectors() {
        val m = model(oneContractDocument())
        assertTrue(m.row("10.99.99.3")!!.enabled)
        assertEquals("918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d", key(m, "10.99.99.3"))
        assertEquals("", key(m, "10.99.99.10"))
        assertFalse(m.row("10.99.99.10")!!.enabled)
        val reverse = model(contractsDocument("10.99.99.3"))
        assertEquals("", key(reverse, "10.99.99.2"))
        val changed = oneContractDocument().replace("918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d", "b".repeat(64))
        assertEquals("b".repeat(64), key(model(changed), "10.99.99.3"))
        val peer = "10.99.99.3"
        val stored = key(m, peer)
        assertNull(m.setEnabled(peer, false))
        assertEquals(stored, key(m, peer))
        assertNull(m.setEnabled(peer, true))
        assertFalse(m.dirty)
        assertNull(m.setEnabled(peer, false))
        val saved = m.prepareSave(StandInEnvelopeSizer).ok()
        val reloaded = saved.text().let { text -> model(text.reveal()).also { text.wipe() } }
        assertEquals("", key(reloaded, peer))
        assertTrue(!reloaded.row(peer)!!.enabled && !reloaded.dirty)
    }

    @Test
    fun key_validation_all_rows_dirty_restore_and_focus() {
        val m = model()
        val peer = "10.99.99.254"
        val original = key(m, peer)
        val focus = ContractFocus("10.99.99.10", ContractColumn.PASSWORD)
        assertNull(m.setFocus(focus))
        for (bad in listOf("a".repeat(63), "a".repeat(65), "A".repeat(64), "g".repeat(64), " $original", "a\u0000b")) {
            assertNull(m.setKey(peer, secret(bad)))
            assertEquals(bad, key(m, peer))
            assertFalse(m.row(peer)!!.keyValid)
            assertTrue(m.canPrepareSave)
            assertNull(m.setEnabled(peer, true))
            assertFalse(m.canPrepareSave)
            assertEquals(1, m.blockingErrors)
            assertEquals(ContractFocus(peer, ContractColumn.KEY), m.firstBlockingFocus)
            assertEquals(ErrorCode.CONFIG_INVALID_KEY, m.prepareSave(StandInEnvelopeSizer).code())
            assertNull(m.setEnabled(peer, false))
        }
        assertNull(m.setKey(peer, secret("b".repeat(64))))
        assertTrue(!m.row(peer)!!.dirty && !m.dirty)
        assertNull(m.setEnabled(peer, true))
        assertTrue(m.row(peer)!!.dirty && m.dirty)
        assertNull(m.setEnabled(peer, false))
        assertFalse(m.dirty)
        assertEquals("b".repeat(64), key(m, peer))
        assertNull(m.setKey(peer, secret(original)))
        assertFalse(m.dirty)
        assertEquals(focus, m.focus)
        assertNull(m.setKey(peer, m.row(peer)!!.keyText)) // alias-safe replacement
        assertFalse(m.dirty)
        // An unpaired surrogate is the UTF-16 counterpart of invalid UTF-8.
        assertEquals(ErrorCode.CONFIG_ENCODING_ERROR, m.setKey(peer, secret("\uD800")))
        assertEquals(ErrorCode.INVALID_ARGUMENT, m.setEnabled(m.localIp, true))
        assertEquals(ErrorCode.INVALID_ARGUMENT, m.setFocus(ContractFocus(m.localIp, ContractColumn.KEY)))
    }

    @Test
    fun total_key_budget_exact_plus_one_and_size_overflow() {
        val m = model()
        val peer = m.rows().first().remoteIp
        val available = MAX_CONTRACT_KEY_TEXT_BYTES - m.keyTextBytes + key(m, peer).length
        val large = "a".repeat(available.toInt())
        assertNull(m.setKey(peer, secret(large)))
        assertEquals(MAX_CONTRACT_KEY_TEXT_BYTES, m.keyTextBytes)
        assertEquals(ErrorCode.CONFIG_TOO_LARGE, m.setKey(peer, secret(large + "a")))
        assertEquals(large, key(m, peer))
        assertEquals(ErrorCode.CONFIG_TOO_LARGE, m.setKey(m.rows().last().remoteIp, secret("a".repeat(65))))
        assertTrue(m.canPrepareSave) // inactive oversized key never goes to JSON
        val candidate = m.prepareSave(StandInEnvelopeSizer).ok()
        assertTrue(candidate.text().let { it.length.also { _ -> it.wipe() } } < 1024)
        assertEquals(ErrorCode.CONFIG_TOO_LARGE, checkedContractKeyTextSize(64, 64, Long.MAX_VALUE).code())
        assertEquals(ErrorCode.CONFIG_TOO_LARGE, checkedContractKeyTextSize(Long.MAX_VALUE, 0, 1).code())
        assertEquals(ErrorCode.CONFIG_TOO_LARGE, checkedContractKeyTextSize(0, 1, 0).code())
    }

    @Test
    fun random_confirmation_failure_and_restored_key_is_clean() {
        val m = model()
        val random = CountingRandom()
        val target = m.target("10.99.99.10")!!
        assertEquals("10.99.99.2_10.99.99.10", target.contractId)
        repeat(3) { assertNull(m.confirmRandom(target, false, random)) }
        assertEquals(0, random.calls)
        assertFalse(m.dirty)
        random.fail = true
        val old = key(m, target.remoteIp)
        assertEquals(ErrorCode.CRYPTO_FAILED, m.confirmRandom(target, true, random))
        assertEquals(old, key(m, target.remoteIp))
        assertFalse(m.dirty)
        random.fail = false
        assertNull(m.confirmRandom(target, true, random))
        assertEquals(32, random.size)
        assertEquals("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key(m, target.remoteIp))
        assertFalse(m.row(target.remoteIp)!!.enabled)
        assertNull(m.setKey(target.remoteIp, secret(old)))
        assertFalse(m.dirty)
    }

    @Test
    fun enabled_same_generated_key_is_clean_and_different_result_is_dirty() {
        val peer = "10.99.99.10"
        val original = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
        val seed = model()
        assertNull(seed.setEnabled(peer, true))
        assertNull(seed.setKey(peer, secret(original)))
        val saved = seed.prepareSave(StandInEnvelopeSizer).ok()
        val m = saved.text().let { text -> model(text.reveal()).also { text.wipe() } }
        val random = CountingRandom()
        val target = m.target(peer)!!
        assertNull(m.confirmRandom(target, true, random))
        assertEquals(1, random.calls)
        assertEquals(original, key(m, peer))
        assertTrue(m.row(peer)!!.enabled)
        assertTrue(!m.row(peer)!!.dirty && !m.dirty)
        assertNull(m.applyPasswordKey(target, secret(original)))
        assertFalse(m.dirty)
        assertNull(m.applyPasswordKey(target, secret("a".repeat(64))))
        assertTrue(m.row(peer)!!.dirty && m.dirty)
        assertNull(m.applyPasswordKey(target, secret(original)))
        assertFalse(m.dirty)
        assertNull(m.setEnabled(peer, false))
        assertTrue(m.dirty) // removing an originally enabled contract is a change
        assertNull(m.setEnabled(peer, true))
        assertFalse(m.dirty)
    }

    @Test
    fun multiple_targets_last_confirmed_frozen_candidate_and_stale_generations() {
        val m = model()
        assertNull(m.setKey("10.99.99.10", secret("d".repeat(64))))
        val b = model()
        val first = m.target("10.99.99.10")!!
        val same = m.target(first.remoteIp)!!
        val other = m.target("10.99.99.3")!!
        assertEquals("10.99.99.2_10.99.99.10", first.contractId)
        assertEquals(first.contractId, same.contractId)
        assertEquals("10.99.99.2_10.99.99.3", other.contractId)
        assertNull(m.setFocus(ContractFocus(other.remoteIp, ContractColumn.RANDOM)))
        val original = key(m, first.remoteIp)
        assertNull(m.applyPasswordKey(first, secret(original)))
        assertFalse(m.dirty) // disabled working keys do not affect the JSON
        assertNull(m.applyPasswordKey(first, secret("a".repeat(64))))
        assertNull(m.applyPasswordKey(other, secret("c".repeat(64))))
        assertNull(m.applyPasswordKey(same, secret("b".repeat(64))))
        assertEquals("b".repeat(64), key(m, first.remoteIp))
        assertEquals("c".repeat(64), key(m, other.remoteIp))
        assertEquals(ErrorCode.INVALID_ARGUMENT, b.applyPasswordKey(first, secret(original)))
        assertNull(m.setEnabled(first.remoteIp, true))
        val before = m.saveGeneration
        val frozen = m.prepareSave(StandInEnvelopeSizer).ok()
        assertEquals(before + 1, m.saveGeneration)
        val snapshot = frozen.text().reveal()
        assertTrue(m.frozen)
        assertEquals(ErrorCode.INVALID_ARGUMENT, m.setEnabled(first.remoteIp, false))
        assertEquals(ErrorCode.INVALID_ARGUMENT, m.applyPasswordKey(other, secret(original)))
        assertEquals(ErrorCode.INVALID_ARGUMENT, m.prepareSave(StandInEnvelopeSizer).code())
        assertFalse(m.resumeEditing(before))
        assertTrue(m.resumeEditing(before + 1))
        assertEquals(ErrorCode.INVALID_ARGUMENT, m.applyPasswordKey(first, secret(original)))
        val fresh = m.target(first.remoteIp)!!
        assertNull(m.applyPasswordKey(fresh, secret(original)))
        m.close()
        assertTrue(m.closed && m.rows().isEmpty() && m.candidate == null && m.focus == null)
        assertEquals(ErrorCode.INVALID_ARGUMENT, m.applyPasswordKey(fresh, secret(original)))
        assertTrue(b.canPrepareSave)
        // The released candidate is wiped when it leaves the model; the copy taken before stays intact.
        assertTrue(snapshot.contains("\"10.99.99.10\""))
    }

    @Test
    fun candidate_local_failure_keeps_draft_generation_and_children() {
        val m = model()
        val target = m.target("10.99.99.3")!!
        val generation = m.saveGeneration
        assertEquals(ErrorCode.INVALID_ARGUMENT, m.prepareSave(null).code())
        assertEquals(ErrorCode.IPC_MESSAGE_TOO_LARGE, m.prepareSave { StandInResult.Ok(IPC_MAX_MESSAGE_BYTES + 1L) }.code())
        assertEquals(ErrorCode.INVALID_ARGUMENT, m.prepareSave { StandInResult.Failed(ErrorCode.INVALID_ARGUMENT) }.code())
        assertEquals(generation, m.saveGeneration)
        assertFalse(m.frozen)
        assertNull(m.applyPasswordKey(target, secret("a".repeat(64))))
    }

    @Test
    fun candidate_patches_only_contracts_and_matches_regardless_of_member_order() {
        // contracts_document.cpp: absent stays absent without enabled rows, present stays {} after removing all.
        val absent = model(contractsDocument())
        val unchanged = absent.prepareSave(StandInEnvelopeSizer).ok()
        assertFalse(unchanged.dom.containsKey(StandInContractsDocument.CONTRACTS))
        val present = model(oneContractDocument())
        assertNull(present.setEnabled("10.99.99.3", false))
        val emptied = present.prepareSave(StandInEnvelopeSizer).ok()
        assertEquals(JsonObject(emptyMap()), emptied.dom[StandInContractsDocument.CONTRACTS])
        // matchesDocument ignores object order but not values or presence.
        val root = StrictJson.parse(contractsDocument()) as JsonObject
        val members = LinkedHashMap<String, JsonElement>()
        members[StandInContractsDocument.CONTRACTS] = JsonObject(emptyMap())
        for ((name, value) in root.entries.reversed()) members[name] = value
        val reordered = IpcJson.encodeToString(JsonElement.serializer(), JsonObject(members))
        assertEquals(true, emptied.matchesDocument(reordered).ok())
        assertEquals(false, emptied.matchesDocument(contractsDocument()).ok())
        assertEquals(ErrorCode.CONFIG_INVALID_JSON, emptied.matchesDocument("{").code())
    }
}
