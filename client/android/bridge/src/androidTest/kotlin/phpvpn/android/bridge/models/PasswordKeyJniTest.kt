package phpvpn.android.bridge.models

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import phpvpn.android.bridge.controller.ContractTargetHandle
import phpvpn.android.bridge.jni.NativePasswordKeyModel
import phpvpn.android.bridge.jni.PasswordKeyError

/**
 * The native PasswordKeyModel through JNI (WP04), ported from
 * client/tests/unit/test_password_key.cpp: UTF-16 units go to the model
 * unconverted, so the SHA-256 vectors prove the exact UTF-8 the model hashes.
 * The crypto failure injection of the desktop test needs a fake provider and
 * is not portable to the real one.
 */
@RunWith(AndroidJUnit4::class)
class PasswordKeyJniTest {
    private fun key(model: NativePasswordKeyModel): String? = model.key()?.let { secret -> secret.reveal().also { secret.wipe() } }

    @Test
    fun independent_hash_vectors_exact_utf8_and_no_normalization() = NativePasswordKeyModel().use { model ->
        assertFalse(model.valid)
        val vectors = listOf(
            "" to "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "abc" to "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            " aBc " to "af288d7200c8c3e47b6c68b0076ca80c28fc97f67c5ebbd6ec6c6b243b7dc413",
            "é" to "4a99557e4033c3539de2eb65472017cad5f9557f7a0625a09f1c3f6e2ba69c4c",
            "é" to "bf12767b0f2a56b2190075bae8169f656e3ce8d6357d4aff184bc6c7ea48f9f6",
            "😀" to "f0443a342c5ef54783a111b51ba56c938e474c32324d90c3a60c9c8e3a37e2d9",
            "߿" to "c57c6175c9087b0c1f7f849c840c8d1ab5b0c32163ffac03fb9f1913f222516d",
            "ࠀ" to "a36ae1af6063cc9281d4010e979a1e3fd3d9ee3e804b7093d2b00f6ee6388bec",
            "￿" to "2c34e62119d9c5fc3aa11640bd7945d6bf309acc6e99d8054f54ae5110e343f6",
            "􏿿" to "708b8add9f6b5b556a07b72973b10ff9a3ec30e002034ef24aa076fab4b40e50",
        )
        for ((input, hash) in vectors) {
            assertEquals(PasswordKeyError.NONE, model.update(input.toCharArray()))
            assertTrue(model.valid)
            assertEquals(hash, key(model))
        }
        model.clear()
        assertTrue(!model.valid && model.key() == null && model.scalarCount == 0)
    }

    @Test
    fun ascii_and_non_bmp_254_255_256_reject_entire_edit_keep_result() = NativePasswordKeyModel().use { model ->
        for (emoji in listOf(false, true)) {
            for (count in listOf(254, 255, 256)) {
                val password = (if (emoji) "😀" else "a").repeat(count).toCharArray()
                val previous = key(model)
                val result = model.update(password)
                if (count == 256) {
                    assertEquals(PasswordKeyError.TOO_LONG, result)
                    assertEquals(previous, key(model))
                    assertEquals(255, model.scalarCount)
                    assertTrue(model.valid)
                } else {
                    assertEquals(PasswordKeyError.NONE, result)
                    assertEquals(count, model.scalarCount)
                    val hash = if (emoji) {
                        if (count == 254) "779da693eb6cfc62fc66c6f5962c7e6b294173998686b860f2f50a99cd523a2c" else "2f1e8352efc292038be0e4df7930791024f96f5400ecc81fbbba8f4f5d0521b1"
                    } else {
                        if (count == 254) "136496c2a16a22b58bbd01529b66d8510cc5a3ec61711cbaa298a52580b000d6" else "b0f3323e7a3cad8ae6778340cc2a17ae0cb31c818df3767cda7c3dd423725e90"
                    }
                    assertEquals(hash, key(model))
                }
                password.fill('\u0000')
            }
        }
        val combining = "a".repeat(254) + "́"
        assertEquals(PasswordKeyError.NONE, model.update(combining.toCharArray()))
        assertEquals(255, model.scalarCount)
        assertEquals(PasswordKeyError.TOO_LONG, model.update((combining + "́").toCharArray()))
        assertEquals(255, model.scalarCount)
        // Far longer than any valid value: refused on the length alone, before a copy.
        assertEquals(PasswordKeyError.TOO_LONG, model.update(CharArray(100_000) { 'a' }))
        assertEquals(255, model.scalarCount)
        assertEquals(PasswordKeyError.TOO_LONG, model.error)
    }

    @Test
    fun malformed_surrogates_invalidate_and_never_hash_prefix() = NativePasswordKeyModel().use { model ->
        val malformed = listOf(
            charArrayOf('\ud800'), charArrayOf('\udbff'), charArrayOf('\udc00'), charArrayOf('\udfff'), charArrayOf('a', '\ud800'),
            charArrayOf('\ud800', 'a'), charArrayOf('\ud800', '\ud800'), charArrayOf('\udc00', '\ud800'),
            charArrayOf('\ud800', '\udc00', '\udc00'), charArrayOf('\ud800', '\udc00', '\ud800'),
        )
        for (bad in malformed) {
            assertEquals(PasswordKeyError.NONE, model.update("valid".toCharArray()))
            assertEquals(PasswordKeyError.INVALID_UTF16, model.update(bad))
            assertTrue(!model.valid && model.key() == null)
        }
        assertEquals(PasswordKeyError.NONE, model.update(charArrayOf('\ud800', '\udc00')))
        assertEquals(1, model.scalarCount)
        assertEquals(PasswordKeyError.NONE, model.update(charArrayOf('\udbff', '\udfff')))
        assertEquals("708b8add9f6b5b556a07b72973b10ff9a3ec30e002034ef24aa076fab4b40e50", key(model))
    }

    @Test
    fun single_line_control_set_and_exact_bytes() = NativePasswordKeyModel().use { model ->
        val controls = (0 until 0x20).map { it.toChar() } + (0x7f..0x9f).map { it.toChar() } + listOf(' ', ' ')
        for (control in controls) {
            assertEquals(PasswordKeyError.NONE, model.update("valid".toCharArray()))
            assertEquals(PasswordKeyError.UNSUPPORTED_CONTROL, model.update(charArrayOf('a', control, 'b')))
            assertTrue(!model.valid && model.key() == null)
        }
        // BOM, no-break space and a combining mark stay exact input bytes (EF BB BF C2 A0 CC 81).
        assertEquals(PasswordKeyError.NONE, model.update("﻿ ́".toCharArray()))
        assertEquals(3, model.scalarCount)
        assertEquals(PasswordKeyError.NONE, model.update(CharArray(0)))
        assertTrue(model.valid)
    }

    @Test
    fun random_and_default_passwords_of_the_dialog() {
        val first = NativePasswordKeyModel.randomPassword()!!
        val second = NativePasswordKeyModel.randomPassword()!!
        for (password in listOf(first, second)) {
            assertEquals(64, password.length)
            assertTrue(password.reveal().all { it in "ghjkmnpqrstuvwxyz" })
        }
        assertNotEquals(first.reveal(), second.reveal())
        NativePasswordKeyModel().use { model ->
            first.read { assertEquals(PasswordKeyError.NONE, model.update(it)) }
            assertTrue(model.valid)
        }
        first.wipe()
        second.wipe()
        val target = object : ContractTargetHandle {
            override val remoteIp = "10.99.99.3"
            override val contractId = "10.99.99.2_10.99.99.3"
        }
        assertEquals("10.99.99.2_10.99.99.3", NativePasswordKeyModel.defaultPassword(target).reveal())
        val closed = NativePasswordKeyModel()
        closed.close()
        try {
            closed.update("x".toCharArray())
            assertNull("a closed handle answered")
        } catch (_: IllegalStateException) {
        }
    }
}
