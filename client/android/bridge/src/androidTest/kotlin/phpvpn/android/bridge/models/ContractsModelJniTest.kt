package phpvpn.android.bridge.models

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import org.junit.runner.RunWith
import phpvpn.android.bridge.controller.ContractColumn
import phpvpn.android.bridge.controller.ContractFocus
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.jni.NativeContractsModel
import phpvpn.android.bridge.jni.NativeModelException
import phpvpn.android.bridge.models.ModelDocuments.document
import phpvpn.android.bridge.models.ModelDocuments.secret

/**
 * The native ContractsModel through its JNI handle (WP04), ported from
 * client/tests/unit/test_contracts_model.cpp. The real crypto provider stands
 * in for the counting fake of the desktop test, so the cases that count hash
 * calls or inject provider failures are reduced to their observable result.
 */
@RunWith(AndroidJUnit4::class)
class ContractsModelJniTest {
    private fun create(text: String, path: String = "", revision: String = ""): NativeContractsModel =
        NativeContractsModel.create(secret(text), path, revision)

    private fun refused(text: String): ErrorCode = try {
        create(text).close()
        fail("the document was accepted")
        ErrorCode.NONE
    } catch (error: NativeModelException) {
        error.code
    }

    @Test
    fun all_peers_numeric_order_self_and_address_boundaries() {
        for (local in listOf("10.99.99.2", "10.99.99.128", "10.99.99.254")) {
            create(document(local), "synthetic/config", "old-revision").use { model ->
                val rows = model.rows()
                assertEquals(252, rows.size)
                assertNull(model.row(local))
                var previous = -1L
                for (row in rows) {
                    val value = row.remoteIp.split('.').fold(0L) { acc, part -> acc * 256 + part.toLong() }
                    assertTrue(previous < value)
                    previous = value
                    assertTrue(!row.enabled && row.keyText.isEmpty() && !row.keyValid && !row.keyErrorVisible && !row.dirty)
                    row.keyText.wipe()
                }
                assertFalse(model.dirty)
                assertEquals("synthetic/config", model.configPath)
                assertEquals("old-revision", model.revisionToken)
                assertEquals(local, model.localIp)
            }
        }
        for ((cidr, local) in listOf("0.0.0.0/29" to "0.0.0.2", "172.16.2.0/29" to "172.16.2.6", "255.255.255.248/29" to "255.255.255.254")) {
            create(document(local, cidr)).use { assertEquals(4, it.rows().size) }
        }
    }

    @Test
    fun invalid_range_and_repair_rejected_before_default_hashes() {
        for (cidr in listOf("0.0.0.0/0", "10.99.0.0/16", "10.99.98.0/23", "10.99.99.0/30", "10.99.99.0/31", "10.99.99.0/32", "10.99.99.1/24", "10.99.99.0/999999999999999")) {
            assertEquals(cidr, ErrorCode.CONFIG_INVALID_CIDR, refused(document("10.99.99.2", cidr)))
        }
        for (local in listOf("10.99.99.0", "10.99.99.1", "10.99.99.255", "10.99.98.2")) {
            assertEquals(local, ErrorCode.CONFIG_INVALID_ADDRESS, refused(document(local)))
        }
        refused(document().replace(Regex(""","accessKey":"[0-9a-f]+""""), ""))
    }

    @Test
    fun all_enabled_252_rows_and_multiple_errors_are_counted() {
        create(document("10.20.30.10", "10.20.30.0/24")).use { m ->
            assertEquals("10.20.30.2", m.rows().first().remoteIp)
            val pair = m.target("10.20.30.2")!!
            assertEquals("10.20.30.2_10.20.30.10", pair.contractId)
            for (row in m.rows()) {
                assertNull(m.setKey(row.remoteIp, secret("a".repeat(64))))
                assertNull(m.setEnabled(row.remoteIp, true))
            }
            assertTrue(m.canPrepareSave)
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
    }

    @Test
    fun exact_existing_key_and_shared_defaults() {
        create(ModelDocuments.oneContractDocument()).use { m ->
            assertTrue(m.row("10.99.99.3")!!.enabled)
            assertEquals(ModelDocuments.ONE_CONTRACT_KEY, m.row("10.99.99.3")!!.keyText.reveal())
            assertEquals("", m.row("10.99.99.10")!!.keyText.reveal())
            assertFalse(m.row("10.99.99.10")!!.enabled)
            val peer = "10.99.99.3"
            val stored = m.row(peer)!!.keyText.reveal()
            assertNull(m.setEnabled(peer, false))
            assertEquals(stored, m.row(peer)!!.keyText.reveal())
            assertNull(m.setEnabled(peer, true))
            assertFalse(m.dirty)
        }
        create(document("10.99.99.3")).use { assertTrue(it.row("10.99.99.2")!!.keyText.isEmpty()) }
        create(ModelDocuments.oneContractDocument("b".repeat(64))).use { assertEquals("b".repeat(64), it.row("10.99.99.3")!!.keyText.reveal()) }
    }

    @Test
    fun key_validation_dirty_restore_and_focus() {
        create(document()).use { m ->
            val peer = "10.99.99.254"
            val original = m.row(peer)!!.keyText.reveal()
            val focus = ContractFocus("10.99.99.10", ContractColumn.PASSWORD)
            assertNull(m.setFocus(focus))
            for (bad in listOf("a".repeat(63), "a".repeat(65), "A".repeat(64), "g".repeat(64), " $original", "a\u0000b")) {
                assertNull(m.setKey(peer, secret(bad)))
                assertEquals(bad, m.row(peer)!!.keyText.reveal())
                assertFalse(m.row(peer)!!.keyValid)
                assertTrue(m.canPrepareSave)
                assertNull(m.setEnabled(peer, true))
                assertFalse(m.canPrepareSave)
                assertEquals(1, m.blockingErrors)
                assertEquals(ContractFocus(peer, ContractColumn.KEY), m.firstBlockingFocus)
                assertNull(m.setEnabled(peer, false))
            }
            assertNull(m.setKey(peer, secret("b".repeat(64))))
            assertTrue(!m.row(peer)!!.dirty && !m.dirty)
            assertNull(m.setEnabled(peer, true))
            assertTrue(m.row(peer)!!.dirty && m.dirty)
            assertNull(m.setEnabled(peer, false))
            assertFalse(m.dirty)
            assertNull(m.setKey(peer, secret(original)))
            assertFalse(m.dirty)
            assertEquals(focus, m.focus)
            assertNull(m.setFocus(null))
            assertNull(m.focus)
            // A lone surrogate is not UTF-8 text; the local address is not a row.
            assertEquals(ErrorCode.CONFIG_INVALID_KEY, m.setKey(peer, secret("\ud800")))
            assertEquals(ErrorCode.INVALID_ARGUMENT, m.setEnabled(m.localIp, true))
            assertEquals(ErrorCode.INVALID_ARGUMENT, m.setFocus(ContractFocus(m.localIp, ContractColumn.KEY)))
            assertEquals(ErrorCode.INVALID_ARGUMENT, m.setEnabled("10.99.99.010", true))
        }
    }

    @Test
    fun total_key_budget_exact_plus_one() {
        create(document()).use { m ->
            val peer = m.rows().first().remoteIp
            val budget = 512 * 1024L
            val available = budget - m.keyTextBytes + m.row(peer)!!.keyText.length
            val large = "a".repeat(available.toInt())
            assertNull(m.setKey(peer, secret(large)))
            assertEquals(budget, m.keyTextBytes)
            assertEquals(ErrorCode.CONFIG_TOO_LARGE, m.setKey(peer, secret(large + "a")))
            assertEquals(large.length, m.row(peer)!!.keyText.length)
            assertEquals(ErrorCode.CONFIG_TOO_LARGE, m.setKey(m.rows().last().remoteIp, secret("a".repeat(65))))
            assertTrue(m.canPrepareSave)
        }
    }

    @Test
    fun random_confirmation_and_restored_key_is_clean() {
        create(document()).use { m ->
            val target = m.target("10.99.99.10")!!
            assertEquals("10.99.99.2_10.99.99.10", target.contractId)
            repeat(3) { assertNull(m.confirmRandom(target, false)) }
            assertFalse(m.dirty)
            val old = m.row(target.remoteIp)!!.keyText.reveal()
            assertNull(m.confirmRandom(target, true))
            val generated = m.row(target.remoteIp)!!.keyText.reveal()
            assertTrue(generated, Regex("[0-9a-f]{64}").matches(generated))
            assertFalse(m.row(target.remoteIp)!!.enabled)
            assertNull(m.setKey(target.remoteIp, secret(old)))
            assertFalse(m.dirty)
        }
    }

    @Test
    fun password_key_applies_and_targets_of_another_model_are_refused() {
        val peer = "10.99.99.10"
        val original = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
        create(document()).use { a ->
            create(document()).use { b ->
                val first = a.target(peer)!!
                val same = a.target(peer)!!
                val other = a.target("10.99.99.3")!!
                assertNull(a.setFocus(ContractFocus(other.remoteIp, ContractColumn.RANDOM)))
                assertNull(a.applyPasswordKey(first, secret("a".repeat(64))))
                assertNull(a.applyPasswordKey(other, secret("c".repeat(64))))
                assertNull(a.applyPasswordKey(same, secret("b".repeat(64))))
                assertEquals("b".repeat(64), a.row(peer)!!.keyText.reveal())
                assertEquals("c".repeat(64), a.row(other.remoteIp)!!.keyText.reveal())
                assertEquals(ErrorCode.INVALID_ARGUMENT, b.applyPasswordKey(first, secret(original)))
                assertNull(a.setEnabled(peer, true))
                assertNull(a.applyPasswordKey(first, secret(original)))
                assertTrue(a.dirty)
                assertTrue(b.canPrepareSave)
            }
        }
    }

    @Test
    fun closed_model_handle_is_refused() {
        val model = create(document())
        model.close()
        try {
            model.rows()
            fail("a closed handle answered")
        } catch (_: IllegalStateException) {
        }
    }
}
