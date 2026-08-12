"""The `health` action, protocol chapters 5.2, 7.3 and 16.3."""

from __future__ import annotations

import pytest

from phpvpn_client import (
    JSON_CONTENT_TYPE,
    TIMESTAMP_PATTERN,
    UUID4_PATTERN,
    PhpVpnClient,
    assert_protocol_headers,
    b64u_decode,
    b64u_encode,
    build_auth_none,
    compact_json,
    derive_health_key,
    health_aad,
    new_uuid4,
    parse_timestamp,
)
from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

HEALTH_FIELDS = {
    "serverVersion",
    "status",
    "description",
    "timestamp",
    "requestId",
    "iv",
    "tag",
}


def test_health_returns_exactly_the_specified_fields(client: PhpVpnClient) -> None:
    """Chapter 5.2 and 16.1.6, the response has exactly seven members."""
    response, request_id = client.health()
    assert response.status_code == 200
    assert_protocol_headers(response, JSON_CONTENT_TYPE)

    payload = response.json()
    assert set(payload) == HEALTH_FIELDS
    assert payload["status"] == "running"
    assert payload["description"] == "OK"
    assert payload["requestId"] == request_id
    assert isinstance(payload["serverVersion"], str) and payload["serverVersion"]
    assert len(b64u_decode(payload["iv"])) == 12
    assert len(b64u_decode(payload["tag"])) == 16


def test_health_timestamp_has_millisecond_precision(client: PhpVpnClient) -> None:
    """Chapter 5.2, the timestamp is exactly `YYYY-MM-DDTHH:MM:SS.sssZ`."""
    response, _ = client.health()
    timestamp = response.json()["timestamp"]
    assert len(timestamp) == 24
    assert TIMESTAMP_PATTERN.match(timestamp), timestamp
    # A parsed value close to the local clock proves it is a real UTC moment.
    assert abs(parse_timestamp(timestamp) - client.clock.now_ms()) < 5000


def test_health_tag_verifies_independently(client: PhpVpnClient) -> None:
    """Chapter 7.3, the GMAC verifies with an independent AES-256-GCM."""
    response, request_id = client.health()
    payload = response.json()
    assert client.verify_health(payload, request_id)


def test_health_tag_rejects_modified_aad(client: PhpVpnClient) -> None:
    """Chapter 16.3.21, changing any authenticated field invalidates the tag."""
    response, request_id = client.health()
    payload = response.json()
    key = derive_health_key(client.configuration.server_secret, request_id)
    iv = b64u_decode(payload["iv"])
    tag = b64u_decode(payload["tag"])

    for field in ("serverVersion", "status", "description", "timestamp", "iv"):
        modified = dict(payload)
        original = modified[field]
        modified[field] = original[:-1] + ("A" if original[-1] != "A" else "B")
        aad = health_aad(
            request_id,
            modified["serverVersion"],
            modified["status"],
            modified["description"],
            modified["timestamp"],
            modified["iv"],
        )
        with pytest.raises(InvalidTag):
            AESGCM(key).decrypt(iv, tag, aad)


def test_health_tag_rejects_a_different_request_id(client: PhpVpnClient) -> None:
    """Chapter 7.3, a valid tag bound to another requestId is invalid."""
    response, request_id = client.health()
    payload = response.json()
    assert not client.verify_health(payload, new_uuid4())


def test_health_tag_rejects_a_wrong_server_secret(client: PhpVpnClient) -> None:
    """Chapter 16.3.21, a tag checked with another secret is rejected."""
    response, request_id = client.health()
    payload = response.json()
    key = derive_health_key(b"a different server secret", request_id)
    aad = health_aad(
        request_id,
        payload["serverVersion"],
        payload["status"],
        payload["description"],
        payload["timestamp"],
        payload["iv"],
    )
    with pytest.raises(InvalidTag):
        AESGCM(key).decrypt(b64u_decode(payload["iv"]), b64u_decode(payload["tag"]), aad)


def test_health_uses_a_fresh_iv_for_the_same_request_id(client: PhpVpnClient) -> None:
    """Chapter 5.2, the IV is 12 fresh CSPRNG bytes for every response."""
    request_id = new_uuid4()
    first, _ = client.health(request_id=request_id)
    second, _ = client.health(request_id=request_id)
    assert first.json()["requestId"] == request_id
    assert second.json()["requestId"] == request_id
    assert first.json()["iv"] != second.json()["iv"]
    assert first.json()["tag"] != second.json()["tag"]
    assert client.verify_health(first.json(), request_id)
    assert client.verify_health(second.json(), request_id)


def test_health_iv_is_canonical_base64url(client: PhpVpnClient) -> None:
    """Chapter 6.1, every base64url value of the response is canonical."""
    response, _ = client.health()
    payload = response.json()
    assert b64u_encode(b64u_decode(payload["iv"])) == payload["iv"]
    assert b64u_encode(b64u_decode(payload["tag"])) == payload["tag"]


@pytest.mark.parametrize(
    "body",
    [
        pytest.param(
            compact_json([("type", "auth-none"), ("requestId", new_uuid4()), ("extra", 1)]),
            id="extra-member",
        ),
        pytest.param(compact_json([("type", "auth-none")]), id="missing-requestId"),
        pytest.param(compact_json([("requestId", new_uuid4())]), id="missing-type"),
        pytest.param(
            compact_json([("type", "auth-semi"), ("requestId", new_uuid4())]),
            id="wrong-type-value",
        ),
        pytest.param(
            compact_json(
                [
                    ("type", "auth-none"),
                    ("requestId", new_uuid4()),
                    ("timestamp", "2026-08-09T20:15:30.123Z"),
                ]
            ),
            id="timestamp-member-present",
        ),
        pytest.param(
            compact_json([("type", "auth-none"), ("requestId", new_uuid4()), ("tag", "AA")]),
            id="tag-member-present",
        ),
        pytest.param(
            compact_json([("type", "auth-none"), ("requestId", "x")]),
            id="non-uuid-requestId",
        ),
        pytest.param(
            compact_json(
                [("type", "auth-none"), ("requestId", "c22cd6e8-14a9-1d83-97b7-54c26a72d1f0")]
            ),
            id="uuid-version-1",
        ),
        pytest.param(
            compact_json([("type", "auth-none"), ("requestId", new_uuid4().upper())]),
            id="uppercase-uuid",
        ),
        pytest.param(build_auth_none(new_uuid4()) + b"\n", id="trailing-lf"),
        pytest.param(build_auth_none(new_uuid4()) + b"x", id="trailing-junk"),
        pytest.param(
            build_auth_none(new_uuid4()) + build_auth_none(new_uuid4()), id="two-objects"
        ),
        pytest.param(b"", id="empty-body"),
        pytest.param(b'"auth-none"', id="not-an-object"),
        pytest.param(b'{"type":"auth-none"', id="truncated-json"),
        pytest.param(
            b'{"type":"auth-none","requestId":"%s","requestId":"%s"}'
            % (new_uuid4().encode(), new_uuid4().encode()),
            id="duplicate-member",
        ),
        pytest.param(b'{"type":"auth-none","requestId":1}', id="numeric-requestId"),
    ],
)
def test_health_rejects_malformed_bodies(client: PhpVpnClient, body: bytes) -> None:
    """Chapter 16.2.9, `auth-none` has exactly `type` and `requestId`."""
    response, _ = client.health(body=body)
    assert response.status_code == 400, f"got {response.status_code} for {body!r}"
    assert response.json() == {"type": "error", "code": "bad_request"}
    assert_protocol_headers(response, JSON_CONTENT_TYPE)


def test_health_request_id_is_echoed_verbatim(client: PhpVpnClient) -> None:
    """Chapter 5.2, the response carries the exact requestId of the request."""
    request_id = new_uuid4()
    assert UUID4_PATTERN.match(request_id)
    response, _ = client.health(request_id=request_id)
    assert response.json()["requestId"] == request_id
