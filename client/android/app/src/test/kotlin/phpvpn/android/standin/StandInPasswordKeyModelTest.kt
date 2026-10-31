package phpvpn.android.standin

import java.security.MessageDigest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import phpvpn.android.bridge.controller.PasswordKeyError

/** Recording SHA-256 like the Crypto of contracts_fixture.h (synthetic input only). */
internal class RecordingSha256 : Sha256 {
    var fail = false
    var calls = 0
    var lastInput = ByteArray(0)

    override fun digest(input: ByteArray, length: Int): ByteArray? {
        calls++
        lastInput = input.copyOf(length)
        if (fail) return null
        return MessageDigest.getInstance("SHA-256").digest(lastInput)
    }
}

/** Port of client/tests/unit/test_password_key.cpp for the Kotlin stand-in of `PasswordKeyModel`. */
class StandInPasswordKeyModelTest {
    private fun StandInPasswordKeyModel.keyText(): String = key().let { k -> k.reveal().also { k.wipe() } }

    private fun utf8(text: String) = text.toByteArray(Charsets.UTF_8)

    @Test
    fun independent_hash_vectors_exact_utf8_and_no_normalization() {
        val crypto = RecordingSha256()
        val model = StandInPasswordKeyModel(crypto)
        assertFalse(model.valid)
        val vectors = listOf(
            Triple("", "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
            Triple("abc", "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
            Triple(" aBc ", " aBc ", "af288d7200c8c3e47b6c68b0076ca80c28fc97f67c5ebbd6ec6c6b243b7dc413"),
            Triple("é", "é", "4a99557e4033c3539de2eb65472017cad5f9557f7a0625a09f1c3f6e2ba69c4c"),
            Triple("é", "é", "bf12767b0f2a56b2190075bae8169f656e3ce8d6357d4aff184bc6c7ea48f9f6"),
            Triple("😀", "😀", "f0443a342c5ef54783a111b51ba56c938e474c32324d90c3a60c9c8e3a37e2d9"),
            Triple("߿", "߿", "c57c6175c9087b0c1f7f849c840c8d1ab5b0c32163ffac03fb9f1913f222516d"),
            Triple("ࠀ", "ࠀ", "a36ae1af6063cc9281d4010e979a1e3fd3d9ee3e804b7093d2b00f6ee6388bec"),
            Triple("￿", "￿", "2c34e62119d9c5fc3aa11640bd7945d6bf309acc6e99d8054f54ae5110e343f6"),
            Triple("􏿿", "􏿿", "708b8add9f6b5b556a07b72973b10ff9a3ec30e002034ef24aa076fab4b40e50"),
        )
        for ((input, expectedUtf8, hash) in vectors) {
            assertEquals(PasswordKeyError.NONE, model.update(input.toCharArray()))
            assertTrue(model.valid)
            assertEquals(hash, model.keyText())
            assertEquals(utf8(expectedUtf8).toList(), crypto.lastInput.toList())
        }
        model.clear()
        assertTrue(!model.valid && model.keyText().isEmpty() && model.scalarCount == 0)
    }

    @Test
    fun ascii_and_non_bmp_254_255_256_reject_entire_edit_keep_result() {
        val crypto = RecordingSha256()
        val model = StandInPasswordKeyModel(crypto)
        for (emoji in listOf(false, true)) {
            for (count in listOf(254, 255, 256)) {
                val password = (if (emoji) "😀" else "a").repeat(count)
                val previous = model.keyText()
                val calls = crypto.calls
                val result = model.update(password.toCharArray())
                if (count == 256) {
                    assertEquals(PasswordKeyError.TOO_LONG, result)
                    assertEquals(previous, model.keyText())
                    assertEquals(255, model.scalarCount)
                    assertEquals(calls, crypto.calls)
                    assertTrue(model.valid)
                } else {
                    assertEquals(PasswordKeyError.NONE, result)
                    assertEquals(count, model.scalarCount)
                    assertEquals(count * (if (emoji) 4 else 1), crypto.lastInput.size)
                    val hash = if (emoji) {
                        if (count == 254) "779da693eb6cfc62fc66c6f5962c7e6b294173998686b860f2f50a99cd523a2c"
                        else "2f1e8352efc292038be0e4df7930791024f96f5400ecc81fbbba8f4f5d0521b1"
                    } else {
                        if (count == 254) "136496c2a16a22b58bbd01529b66d8510cc5a3ec61711cbaa298a52580b000d6"
                        else "b0f3323e7a3cad8ae6778340cc2a17ae0cb31c818df3767cda7c3dd423725e90"
                    }
                    assertEquals(hash, model.keyText())
                }
            }
        }
        var combining = "a".repeat(254) + "́"
        assertEquals(PasswordKeyError.NONE, model.update(combining.toCharArray()))
        assertEquals(255, model.scalarCount)
        combining += "́"
        assertEquals(PasswordKeyError.TOO_LONG, model.update(combining.toCharArray()))
        assertEquals(255, model.scalarCount)
    }

    @Test
    fun malformed_surrogates_invalidate_and_never_hash_prefix() {
        val crypto = RecordingSha256()
        val model = StandInPasswordKeyModel(crypto)
        val malformed = listOf(
            charArrayOf('\uD800'), charArrayOf('\uDBFF'), charArrayOf('\uDC00'), charArrayOf('\uDFFF'), charArrayOf('a', '\uD800'),
            charArrayOf('\uD800', 'a'), charArrayOf('\uD800', '\uD800'), charArrayOf('\uDC00', '\uD800'),
            charArrayOf('\uD800', '\uDC00', '\uDC00'), charArrayOf('\uD800', '\uDC00', '\uD800'),
        )
        for (bad in malformed) {
            assertEquals(PasswordKeyError.NONE, model.update("valid".toCharArray()))
            val calls = crypto.calls
            assertEquals(PasswordKeyError.INVALID_UTF16, model.update(bad))
            assertTrue(!model.valid && model.keyText().isEmpty())
            assertEquals(calls, crypto.calls)
        }
        assertEquals(PasswordKeyError.NONE, model.update(charArrayOf('\uD800', '\uDC00')))
        assertEquals(1, model.scalarCount)
        assertEquals(listOf(0xf0, 0x90, 0x80, 0x80).map { it.toByte() }, crypto.lastInput.toList())
        assertEquals(PasswordKeyError.NONE, model.update(charArrayOf('\uDBFF', '\uDFFF')))
        assertEquals(listOf(0xf4, 0x8f, 0xbf, 0xbf).map { it.toByte() }, crypto.lastInput.toList())
    }

    @Test
    fun single_line_control_set_explicit_length_and_crypto_failure() {
        val crypto = RecordingSha256()
        val model = StandInPasswordKeyModel(crypto)
        val controls = (0 until 0x20).map { it.toChar() } + (0x7f..0x9f).map { it.toChar() } + listOf(' ', ' ')
        for (c in controls) {
            assertEquals(PasswordKeyError.NONE, model.update("valid".toCharArray()))
            val calls = crypto.calls
            assertEquals(PasswordKeyError.UNSUPPORTED_CONTROL, model.update(charArrayOf('a', c, 'b')))
            assertTrue(!model.valid && model.keyText().isEmpty())
            assertEquals(calls, crypto.calls)
        }
        // Non-control combining, BOM and space characters remain exact input bytes.
        assertEquals(PasswordKeyError.NONE, model.update("﻿ ́".toCharArray()))
        assertEquals(listOf(0xef, 0xbb, 0xbf, 0xc2, 0xa0, 0xcc, 0x81).map { it.toByte() }, crypto.lastInput.toList())
        crypto.fail = true
        assertEquals(PasswordKeyError.CRYPTO_FAILED, model.update("new".toCharArray()))
        assertTrue(!model.valid && model.keyText().isEmpty())
        crypto.fail = false
        assertEquals(PasswordKeyError.NONE, model.update(CharArray(0)))
        assertTrue(model.valid)
    }

    @Test
    fun random_password_uses_the_unbiased_alphabet_and_keeps_the_current_one_on_failure() {
        val random = CountingRandom()
        val password = randomPassword(random)!!
        assertEquals(64, password.size)
        // Byte i maps to alphabet[i % 17] for the bytes below the 255 limit.
        assertEquals("ghjkmnpqrstuvwxyz".let { a -> (0 until 64).map { a[it % 17] }.joinToString("") }, String(password))
        random.fail = true
        assertEquals(null, randomPassword(random))
    }
}
