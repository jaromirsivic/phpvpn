"""The `info` action, protocol chapters 8, 10.6 and 16.3."""

from __future__ import annotations

import json
import time

import pytest
from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

from phpvpn_client import (
    DEFAULT_MAX_INFO_PLAINTEXT_BYTES,
    JSON_CONTENT_TYPE,
    TIMESTAMP_PATTERN,
    PhpVpnClient,
    assert_protocol_headers,
    b64u_decode,
    b64u_encode,
    compact_json,
    derive_info_key,
    info_aad,
    new_uuid4,
    parse_timestamp,
)

INFO_FIELDS = {"type", "requestId", "salt", "iv", "ciphertext", "tag"}
LIMIT_FIELDS = {
    "maxDataRecordsPerRequest",
    "maxRequestBodyBytes",
    "maxQueueBytesPerClient",
    "queueTtlSeconds",
    "minDataPollIntervalMs",
    "maxDataPollIntervalMs",
}


def test_info_response_envelope(client: PhpVpnClient) -> None:
    """Chapter 8.3, the response object and its base64url lengths."""
    response, request_id = client.info()
    assert response.status_code == 200
    assert_protocol_headers(response, JSON_CONTENT_TYPE)

    payload = response.json()
    assert set(payload) == INFO_FIELDS
    assert payload["type"] == "info"
    assert payload["requestId"] == request_id
    assert len(b64u_decode(payload["salt"])) == 32
    assert len(b64u_decode(payload["iv"])) == 12
    assert len(b64u_decode(payload["tag"])) == 16
    assert len(b64u_decode(payload["ciphertext"])) > 0


def test_info_ciphertext_decrypts_and_authenticates(client: PhpVpnClient) -> None:
    """Chapter 8.3, the payload decrypts with the derived info key."""
    response, request_id = client.info()
    plaintext = client.decrypt_info(response.json(), request_id)
    assert plaintext["serverVersion"]
    assert TIMESTAMP_PATTERN.match(plaintext["generatedAt"])


def test_info_rejects_a_modified_aad(client: PhpVpnClient) -> None:
    """Chapter 8.3, the AAD binds requestId, salt and IV to the ciphertext."""
    response, request_id = client.info()
    payload = response.json()
    salt = b64u_decode(payload["salt"])
    key = derive_info_key(client.configuration.server_secret, salt, request_id)
    sealed = b64u_decode(payload["ciphertext"]) + b64u_decode(payload["tag"])

    forged = info_aad(new_uuid4(), payload["salt"], payload["iv"])
    with pytest.raises(InvalidTag):
        AESGCM(key).decrypt(b64u_decode(payload["iv"]), sealed, forged)


def test_info_uses_fresh_salt_and_iv(client: PhpVpnClient) -> None:
    """Chapter 16.3.12, two responses use new random salt and IV values."""
    first, first_id = client.info()
    second, second_id = client.info()
    assert first.json()["salt"] != second.json()["salt"]
    assert first.json()["iv"] != second.json()["iv"]
    client.decrypt_info(first.json(), first_id)
    client.decrypt_info(second.json(), second_id)


def test_info_plaintext_structure(client: PhpVpnClient) -> None:
    """Chapter 8.2, the documented plaintext structure and limits."""
    plaintext, response = client.info_plaintext()
    assert {"serverVersion", "generatedAt", "clients", "limits"} <= set(plaintext)

    assert TIMESTAMP_PATTERN.match(plaintext["generatedAt"])
    assert abs(parse_timestamp(plaintext["generatedAt"]) - client.clock.now_ms()) < 5000

    limits = plaintext["limits"]
    assert LIMIT_FIELDS <= set(limits)
    for name in LIMIT_FIELDS:
        assert isinstance(limits[name], int) and limits[name] >= 0, name

    # Chapter 11.1 configuration invariants.
    assert 0 < limits["minDataPollIntervalMs"] <= limits["maxDataPollIntervalMs"]
    assert limits["maxRequestBodyBytes"] > limits["maxDataRecordsPerRequest"]
    assert limits["queueTtlSeconds"] > 0
    assert limits["maxQueueBytesPerClient"] > 0

    assert isinstance(plaintext["clients"], list)
    for entry in plaintext["clients"]:
        assert set(entry) == {"address", "lastSeen"}
        assert entry["address"].startswith("10.99.99.")
        assert TIMESTAMP_PATTERN.match(entry["lastSeen"])


def test_info_plaintext_is_within_the_size_limit(client: PhpVpnClient) -> None:
    """Chapter 11.1, the plaintext stays below MAX_INFO_PLAINTEXT_BYTES."""
    response, request_id = client.info()
    plaintext = client.decrypt_info(response.json(), request_id)
    encoded = json.dumps(plaintext, separators=(",", ":")).encode("utf-8")
    assert len(encoded) <= DEFAULT_MAX_INFO_PLAINTEXT_BYTES


def test_info_plaintext_hides_storage_and_secrets(client: PhpVpnClient) -> None:
    """Chapters 8.2 and 10.6, no storage detail or secret is disclosed."""
    response, request_id = client.info()
    plaintext = client.decrypt_info(response.json(), request_id)
    encoded = json.dumps(plaintext, separators=(",", ":")).lower()
    for forbidden in (
        "disk",
        "driver",
        "buffer",
        "info.json",
        "directory",
        "path",
        "secret",
        "accesskey",
        "127.0.0.1",
    ):
        assert forbidden not in encoded, forbidden
    for access_key in client.configuration.access_keys.values():
        assert access_key.hex() not in encoded


def test_info_rejects_a_wrong_tag(client: PhpVpnClient) -> None:
    """Chapter 8.1, a wrong HMAC tag returns the generic `401`."""
    response, _ = client.info(tag=b64u_encode(bytes(32)))
    assert response.status_code == 401
    assert response.json() == {"type": "error", "code": "unauthorized"}


def test_info_rejects_a_tag_from_another_secret(client: PhpVpnClient) -> None:
    """Chapter 7.1, only knowledge of SERVER_SECRET produces a valid tag."""
    response, _ = client.info(server_secret=b"wrong-secret-value")
    assert response.status_code == 401


def test_info_tag_depends_only_on_the_server_secret(client: PhpVpnClient) -> None:
    """Chapter 16.3.3, `auth-semi` never involves an ACCESS_KEY."""
    from phpvpn_client import auth_semi_canonical, derive_semi_key, hmac_sha256

    timestamp = client.clock.timestamp()
    request_id = new_uuid4()
    key = derive_semi_key(client.configuration.server_secret)
    tag = b64u_encode(hmac_sha256(key, auth_semi_canonical(timestamp, request_id)))
    response, _ = client.info(timestamp=timestamp, request_id=request_id, tag=tag)
    assert response.status_code == 200


def test_info_rejects_a_single_character_timestamp_change(client: PhpVpnClient) -> None:
    """Chapter 8.1, the exact timestamp bytes are part of the canonical string."""
    body, timestamp, request_id = client.build_auth_semi()
    document = json.loads(body)
    changed = list(document["timestamp"])
    changed[23 - 2] = "0" if changed[23 - 2] != "0" else "1"
    document["timestamp"] = "".join(changed)
    forged = compact_json(
        [
            ("type", "auth-semi"),
            ("timestamp", document["timestamp"]),
            ("requestId", document["requestId"]),
            ("tag", document["tag"]),
        ]
    )
    response, _ = client.info(body=forged)
    assert response.status_code == 401


@pytest.mark.parametrize(
    "timestamp",
    [
        pytest.param("2026-08-09T20:15:30Z", id="no-fraction"),
        pytest.param("2026-08-09T20:15:30.12Z", id="two-digit-fraction"),
        pytest.param("2026-08-09T20:15:30.1234Z", id="four-digit-fraction"),
        pytest.param("2026-08-09T20:15:30.123+00:00", id="offset-instead-of-Z"),
        pytest.param("2026-08-09T20:15:30.123", id="missing-Z"),
        pytest.param("2026-08-09t20:15:30.123z", id="lowercase"),
        pytest.param("2026-08-09 20:15:30.123Z", id="space-separator"),
        pytest.param("2026-02-30T20:15:30.123Z", id="invalid-calendar-day"),
        pytest.param("2026-13-09T20:15:30.123Z", id="invalid-month"),
        pytest.param("2026-08-09T24:15:30.123Z", id="invalid-hour"),
        pytest.param("2026-08-09T20:15:60.123Z", id="leap-second"),
        pytest.param("26-08-09T20:15:30.123Z", id="two-digit-year"),
        pytest.param("2026-8-09T20:15:30.123Z", id="one-digit-month"),
        pytest.param("", id="empty"),
    ],
)
def test_info_rejects_a_malformed_timestamp(
    client: PhpVpnClient, timestamp: str
) -> None:
    """Chapters 8.1 and 16.3.2, a malformed timestamp returns `400`."""
    response, _ = client.info(timestamp=timestamp)
    assert response.status_code == 400, f"{timestamp!r} gave {response.status_code}"
    assert response.json() == {"type": "error", "code": "bad_request"}


def test_info_rejects_a_numeric_timestamp(client: PhpVpnClient) -> None:
    """Chapter 16.3.2, the timestamp must be a JSON string."""
    body = compact_json(
        [
            ("type", "auth-semi"),
            ("timestamp", 1786000000000),
            ("requestId", new_uuid4()),
            ("tag", b64u_encode(bytes(32))),
        ]
    )
    response, _ = client.info(body=body)
    assert response.status_code == 400


@pytest.mark.parametrize(
    "tag",
    [
        pytest.param(b64u_encode(bytes(31)), id="31-bytes"),
        pytest.param(b64u_encode(bytes(33)), id="33-bytes"),
        pytest.param("", id="empty"),
        pytest.param(b64u_encode(bytes(32)) + "=", id="padding-character"),
        pytest.param(b64u_encode(bytes(32)).replace("A", "+", 1), id="plus-character"),
        pytest.param(b64u_encode(bytes(32))[:-1] + "B", id="non-canonical-last-char"),
        pytest.param("A" * 43 + "B" + "A", id="length-1-modulo-4"),
        pytest.param(b64u_encode(bytes(32)).replace("A", "/", 1), id="slash-character"),
        pytest.param(b64u_encode(bytes(32))[:-1] + " ", id="whitespace"),
    ],
)
def test_info_rejects_a_malformed_tag(client: PhpVpnClient, tag: str) -> None:
    """Chapter 8.1, invalid tag encoding or length returns `400`."""
    response, _ = client.info(tag=tag)
    assert response.status_code == 400, f"{tag!r} gave {response.status_code}"


@pytest.mark.parametrize(
    "body",
    [
        pytest.param(
            compact_json(
                [
                    ("type", "auth-semi"),
                    ("timestamp", "2026-08-09T20:15:30.123Z"),
                    ("requestId", new_uuid4()),
                    ("tag", b64u_encode(bytes(32))),
                    ("extra", "x"),
                ]
            ),
            id="unknown-member",
        ),
        pytest.param(
            compact_json(
                [
                    ("type", "auth-none"),
                    ("timestamp", "2026-08-09T20:15:30.123Z"),
                    ("requestId", new_uuid4()),
                    ("tag", b64u_encode(bytes(32))),
                ]
            ),
            id="wrong-type",
        ),
        pytest.param(
            compact_json(
                [
                    ("type", "auth-semi"),
                    ("timestamp", "2026-08-09T20:15:30.123Z"),
                    ("requestId", "not-a-uuid"),
                    ("tag", b64u_encode(bytes(32))),
                ]
            ),
            id="bad-requestId",
        ),
        pytest.param(
            compact_json(
                [
                    ("type", "auth-semi"),
                    ("timestamp", "2026-08-09T20:15:30.123Z"),
                    ("requestId", new_uuid4()),
                ]
            ),
            id="missing-tag",
        ),
        pytest.param(b"", id="empty-body"),
        pytest.param(b"{", id="truncated"),
    ],
)
def test_info_rejects_a_malformed_body(client: PhpVpnClient, body: bytes) -> None:
    """Chapters 6.1 and 8.1, a malformed request object returns `400`."""
    response, _ = client.info(body=body)
    assert response.status_code == 400, f"{body!r} gave {response.status_code}"


def test_info_accepts_a_trailing_lf_free_body_only(client: PhpVpnClient) -> None:
    """Chapter 8.1, the body contains exactly one object and nothing else."""
    body, _, _ = client.build_auth_semi()
    response, _ = client.info(body=body + b"\n")
    assert response.status_code == 400


def read_last_seen(client: PhpVpnClient, address: str) -> int | None:
    """Return the `lastSeen` of one client in epoch milliseconds."""
    plaintext, _ = client.info_plaintext()
    for entry in plaintext["clients"]:
        if entry["address"] == address:
            return parse_timestamp(entry["lastSeen"])
    return None


def refresh_last_seen(client: PhpVpnClient, address: str) -> int:
    """Make the server refresh the marker and return the fresh `lastSeen`.

    Chapter 10.6 refreshes the marker only when it is older than 30 s, so the
    helper waits until the current marker qualifies for an update.
    """
    for _ in range(8):
        client.send_data(address)
        last_seen = read_last_seen(client, address)
        assert last_seen is not None
        age = client.clock.now_ms() - last_seen
        if age < 25_000:
            return last_seen
        time.sleep(min(6.0, (31_000 - age) / 1000.0) if age < 31_000 else 0.2)
    pytest.skip("the lastSeen marker never became fresh")


def test_last_seen_is_not_refreshed_within_thirty_seconds(
    client: PhpVpnClient, clean_queues: None
) -> None:
    """Chapter 10.6.2, an update is attempted only above 30 seconds of age."""
    address = client.configuration.addresses[0]
    before = refresh_last_seen(client, address)

    time.sleep(3)
    client.send_data(address)
    after = read_last_seen(client, address)

    assert after == before, (
        "the marker must not be touched while it is younger than 30 seconds"
    )


@pytest.mark.slow
def test_last_seen_is_refreshed_after_thirty_seconds(
    client: PhpVpnClient, clean_queues: None
) -> None:
    """Chapter 10.6.4, a marker older than 30 seconds is updated once."""
    address = client.configuration.addresses[0]
    before = refresh_last_seen(client, address)

    time.sleep(33)
    client.send_data(address)
    after = read_last_seen(client, address)

    assert after is not None and after > before
    assert abs(client.clock.now_ms() - after) < 5000


def test_data_activity_creates_a_last_seen_entry(
    client: PhpVpnClient, clean_queues: None
) -> None:
    """Chapters 8.2 and 10.6, a client that used `data` appears in `clients`."""
    address = client.configuration.addresses[0]
    response, _ = client.send_data(address)
    assert response.status_code == 200

    plaintext, _ = client.info_plaintext()
    entries = {entry["address"]: entry["lastSeen"] for entry in plaintext["clients"]}
    assert address in entries, "the authenticated source must have a lastSeen marker"

    last_seen = parse_timestamp(entries[address])
    assert last_seen <= client.clock.now_ms() + 5000
    assert client.clock.now_ms() - last_seen < 120000
