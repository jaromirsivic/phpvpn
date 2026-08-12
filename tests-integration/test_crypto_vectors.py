"""Known answer tests of the client side cryptography, chapter 16.3.1.

These tests never touch the network. They pin the primitives of this suite
against published vectors, so that a failure elsewhere can be attributed to the
server rather than to the verifier. RFC 5869 provides the HKDF vectors, RFC
4231 the HMAC vectors and the GCM specification the AES-256-GCM vectors.
"""

from __future__ import annotations

import hashlib
import hmac as hmac_module

import pytest
from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

from phpvpn_client import (
    auth_full_canonical,
    auth_semi_canonical,
    b64u_decode,
    b64u_encode,
    b64u_is_canonical,
    data_aad,
    data_mac_canonical,
    derive_full_auth_key,
    derive_health_key,
    derive_info_key,
    derive_semi_key,
    format_timestamp,
    health_aad,
    hkdf_sha256,
    hmac_sha256,
    normalize_server_secret,
    parse_timestamp,
    sha256_hex,
)


def test_hkdf_sha256_rfc5869_test_case_1() -> None:
    """RFC 5869 A.1, basic HKDF-SHA-256 with salt and info."""
    okm = hkdf_sha256(
        ikm=bytes.fromhex("0b" * 22),
        salt=bytes.fromhex("000102030405060708090a0b0c"),
        info=bytes.fromhex("f0f1f2f3f4f5f6f7f8f9"),
        length=42,
    )
    assert okm.hex() == (
        "3cb25f25faacd57a90434f64d0362f2a"
        "2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
        "34007208d5b887185865"
    )


def test_hkdf_sha256_rfc5869_test_case_3() -> None:
    """RFC 5869 A.3, HKDF-SHA-256 with an empty salt and empty info."""
    okm = hkdf_sha256(
        ikm=bytes.fromhex("0b" * 22), salt=bytes(32), info=b"", length=42
    )
    assert okm.hex() == (
        "8da4e775a563c18f715f802a063c5a31"
        "b8a11f5c5ee1879ec3454e5f3c738d2d"
        "9d201395faa4b61a96c8"
    )


@pytest.mark.parametrize(
    "key,message,expected",
    [
        pytest.param(
            bytes.fromhex("0b" * 20),
            b"Hi There",
            "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
            id="rfc4231-case-1",
        ),
        pytest.param(
            b"Jefe",
            b"what do ya want for nothing?",
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
            id="rfc4231-case-2",
        ),
    ],
)
def test_hmac_sha256_rfc4231(key: bytes, message: bytes, expected: str) -> None:
    """RFC 4231, HMAC-SHA-256 known answers."""
    assert hmac_sha256(key, message).hex() == expected


def test_aes_256_gcm_empty_plaintext_vector() -> None:
    """GCM test case 13, an empty plaintext yields only the tag."""
    key = bytes(32)
    iv = bytes(12)
    sealed = AESGCM(key).encrypt(iv, b"", b"")
    assert sealed.hex() == "530f8afbc74536b9a963b4f1c4cb738b"
    assert AESGCM(key).decrypt(iv, sealed, b"") == b""


def test_aes_256_gcm_single_block_vector() -> None:
    """GCM test case 14, one block of zeros under a zero key."""
    sealed = AESGCM(bytes(32)).encrypt(bytes(12), bytes(16), b"")
    assert sealed[:16].hex() == "cea7403d4d606b6e074ec5d3baf39d18"
    assert sealed[16:].hex() == "d0d1c8a799996bf0265b98b5d48ab919"


def test_aes_256_gcm_vector_with_additional_data() -> None:
    """GCM test case 16, a long plaintext with additional authenticated data."""
    key = bytes.fromhex(
        "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308"
    )
    iv = bytes.fromhex("cafebabefacedbaddecaf888")
    plaintext = bytes.fromhex(
        "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
        "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39"
    )
    aad = bytes.fromhex("feedfacedeadbeeffeedfacedeadbeefabaddad2")
    sealed = AESGCM(key).encrypt(iv, plaintext, aad)
    assert sealed[:-16].hex() == (
        "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa"
        "8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662"
    )
    assert sealed[-16:].hex() == "76fc6ece0f4e1768cddf8853bb2d551b"
    assert AESGCM(key).decrypt(iv, sealed, aad) == plaintext


@pytest.mark.parametrize("position", ["key", "iv", "aad", "ciphertext", "tag"])
def test_aes_256_gcm_rejects_a_single_bit_change(position: str) -> None:
    """Chapter 16.3.17, one changed bit anywhere invalidates the decryption."""
    key = bytearray(bytes.fromhex("11" * 32))
    iv = bytearray(bytes.fromhex("22" * 12))
    aad = bytearray(b"PHPVPN-DATA")
    sealed = bytearray(AESGCM(bytes(key)).encrypt(bytes(iv), b"payload", bytes(aad)))

    target = {"key": key, "iv": iv, "aad": aad, "ciphertext": sealed, "tag": sealed}[
        position
    ]
    index = 0 if position != "tag" else len(sealed) - 1
    target[index] ^= 0x01

    with pytest.raises(InvalidTag):
        AESGCM(bytes(key)).decrypt(bytes(iv), bytes(sealed), bytes(aad))


@pytest.mark.parametrize(
    "text,raw",
    [
        pytest.param("", b"", id="empty"),
        pytest.param("_w", b"\xff", id="single-byte-ff"),
        pytest.param("__8", b"\xff\xff", id="two-bytes-ffff"),
        pytest.param("AA", b"\x00", id="single-zero-byte"),
        pytest.param("-_8", b"\xfb\xff", id="both-special-characters"),
    ],
)
def test_canonical_base64url_round_trip(text: str, raw: bytes) -> None:
    """Chapter 6.1, decode and re-encode returns the original text."""
    assert b64u_is_canonical(text)
    assert b64u_decode(text) == raw
    assert b64u_encode(raw) == text


@pytest.mark.parametrize(
    "text",
    ["_x", "__9", "A", "AAAAA", "AA==", "AA=", "A+", "A/", "A ", "A\n", "AA\t", "\u00e9A"],
)
def test_non_canonical_base64url_is_rejected(text: str) -> None:
    """Chapter 16.2.7, tolerant decoding is not sufficient."""
    assert not b64u_is_canonical(text)
    with pytest.raises(ValueError):
        b64u_decode(text)


def test_server_secret_normalization() -> None:
    """Chapter 16.3.15, only an exactly empty secret is normalized."""
    assert normalize_server_secret(b"") == b"0" * 32
    assert normalize_server_secret(b"0") == b"0"
    assert normalize_server_secret(bytes(32)) == bytes(32)

    assert derive_semi_key(b"") == derive_semi_key(b"0" * 32)
    assert derive_semi_key(b"0") != derive_semi_key(b"0" * 32)
    assert derive_semi_key(bytes(32)) != derive_semi_key(b"0" * 32)

    for secret in (b"", b"0" * 32):
        assert derive_health_key(secret, "c22cd6e8-14a9-4d83-97b7-54c26a72d1f0") == (
            derive_health_key(b"0" * 32, "c22cd6e8-14a9-4d83-97b7-54c26a72d1f0")
        )
        assert derive_full_auth_key(secret, bytes(32), "10.99.99.2") == (
            derive_full_auth_key(b"0" * 32, bytes(32), "10.99.99.2")
        )


def test_auth_semi_key_derivation_structure() -> None:
    """Chapter 7.1, the derivation is rebuilt member by member."""
    secret = b"integration secret"
    salt = hashlib.sha256(b"phpvpn/auth-semi/salt").digest()
    expected = hkdf_sha256(secret, salt, b"phpvpn/auth-semi/key", 32)
    assert derive_semi_key(secret) == expected
    assert len(expected) == 32


def test_auth_full_key_derivation_structure() -> None:
    """Chapter 7.2, salt material, info and key material of `auth-full`."""
    secret = b"integration secret"
    access_key = bytes.fromhex("a1" * 32)
    src = "10.99.99.2"

    salt_material = b"phpvpn/auth-full/salt" + b"\x00" + src.encode("ascii")
    assert salt_material == b"phpvpn/auth-full/salt\x0010.99.99.2"
    salt = hmac_module.new(secret, salt_material, hashlib.sha256).digest()
    assert len(salt) == 32
    info = b"phpvpn/auth-full/key" + b"\x00" + src.encode("ascii")
    expected = hkdf_sha256(access_key, salt, info, 32)

    assert derive_full_auth_key(secret, access_key, src) == expected
    assert len(expected) == 32

    # Chapter 16.3.5, both secrets and the address change the result.
    assert derive_full_auth_key(secret + b"x", access_key, src) != expected
    assert derive_full_auth_key(secret, bytes(32), src) != expected
    assert derive_full_auth_key(secret, access_key, "10.99.99.3") != expected


def test_health_key_derivation_structure() -> None:
    """Chapter 7.3, the per requestId health key."""
    secret = b"integration secret"
    request_id = "c22cd6e8-14a9-4d83-97b7-54c26a72d1f0"
    salt = hashlib.sha256(
        b"phpvpn/health-response/salt\x00" + request_id.encode()
    ).digest()
    info = b"phpvpn/health-response/key\x00" + request_id.encode()
    assert derive_health_key(secret, request_id) == hkdf_sha256(secret, salt, info, 32)


def test_info_key_derivation_structure() -> None:
    """Chapter 8.3, the info response key uses the transmitted salt."""
    secret = b"integration secret"
    request_id = "4e94de3a-b9ad-4bc3-9297-82b98a19a143"
    salt = bytes.fromhex("5a" * 32)
    expected = hkdf_sha256(secret, salt, b"phpvpn/info-response/" + request_id.encode(), 32)
    assert derive_info_key(secret, salt, request_id) == expected


def test_canonical_strings_are_exactly_specified() -> None:
    """Chapters 7.3, 8.1, 9.2, 9.4 and 9.5, byte exact canonical strings."""
    assert auth_semi_canonical("2026-08-09T20:15:30.123Z", "rid") == (
        b"PHPVPN-AUTH-SEMI\nPOST\ninfo\n2026-08-09T20:15:30.123Z\nrid"
    )
    assert auth_full_canonical("10.99.99.2", "2026-08-09T20:15:30.123Z", "rid", b"") == (
        b"PHPVPN-AUTH-FULL\nPOST\ndata\n10.99.99.2\n2026-08-09T20:15:30.123Z\nrid\n"
        + sha256_hex(b"").encode()
    )
    assert data_aad("mid", "10.99.99.2", "10.99.99.4", "IV") == (
        b"PHPVPN-DATA\ndata\nmid\n10.99.99.2\n10.99.99.4\nIV"
    )
    assert health_aad("rid", "1.0.0", "running", "OK", "T", "IV") == (
        b"PHPVPN-HEALTH-RESPONSE\nhealth\nrid\n1.0.0\nrunning\nOK\nT\nIV"
    )


def test_data_mac_canonical_layout() -> None:
    """Chapter 9.5, the record count block and the trailing hash."""
    empty = data_mac_canonical("rid", [], b'{"type":"data-result"}')
    assert empty == (
        b"PHPVPN-DATA-RESPONSE\ndata\nrid\n0\n"
        + sha256_hex(b'{"type":"data-result"}').encode()
    )
    assert not empty.endswith(b"\n")

    two = data_mac_canonical("rid", [("m1", "t1"), ("m2", "t2")], b"result")
    assert two.split(b"\n")[3] == b"2"
    assert two.split(b"\n")[4:8] == [b"m1", b"t1", b"m2", b"t2"]


@pytest.mark.parametrize(
    "epoch_ms,text",
    [
        pytest.param(0, "1970-01-01T00:00:00.000Z", id="epoch"),
        pytest.param(1786000530123, "2026-08-06T07:15:30.123Z", id="millisecond"),
        pytest.param(1786000530000, "2026-08-06T07:15:30.000Z", id="whole-second"),
        pytest.param(1786000530999, "2026-08-06T07:15:30.999Z", id="last-millisecond"),
    ],
)
def test_timestamp_round_trip(epoch_ms: int, text: str) -> None:
    """Chapter 8.1, the exact 24 character UTC representation."""
    assert format_timestamp(epoch_ms) == text
    assert parse_timestamp(text) == epoch_ms
    assert len(text) == 24


@pytest.mark.parametrize(
    "text",
    [
        "2026-08-09T20:15:30Z",
        "2026-08-09T20:15:30.12Z",
        "2026-08-09T20:15:30.1234Z",
        "2026-08-09T20:15:30.123+00:00",
        "2026-08-09t20:15:30.123z",
        "2026-08-09 20:15:30.123Z",
    ],
)
def test_malformed_timestamps_are_rejected(text: str) -> None:
    """Chapter 16.3.2, the accepted timestamp shape is exact."""
    with pytest.raises(ValueError):
        parse_timestamp(text)
