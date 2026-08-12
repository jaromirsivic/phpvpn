"""Robustness, storage hygiene and the accepted replay risk.

Covers chapters 1.2, 4.2, 10.3, 10.5, 10.6, 13, 16.5 and 16.6.
"""

from __future__ import annotations

import os
import re
from pathlib import Path

import pytest

from phpvpn_client import (
    JSON_CONTENT_TYPE,
    NDJSON_CONTENT_TYPE,
    PhpVpnClient,
    Response,
    assert_protocol_headers,
    b64u_encode,
    build_auth_none,
    build_data_line,
    make_record,
    new_uuid4,
    parse_data_response,
)

#: Chapter 10.5, `YYYY-MM-DDTHH-MM-SS.sssZ_<bytes>_<src>_<uuid>` plus a state.
BATCH_NAME = re.compile(
    r"\A\d{4}-\d{2}-\d{2}T\d{2}-\d{2}-\d{2}\.\d{3}Z"
    r"_\d+_\d+\.\d+\.\d+\.\d+"
    r"_[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}"
    r"\.(receiving|data|sending)\Z"
)
CLIENT_DIRECTORY = re.compile(r"\A\d+_\d+_\d+_\d+\Z")

#: Names that would indicate a nonce or replay cache, forbidden by chapter 16.6.6.
REPLAY_MARKERS = ("nonce", "replay", "dedup", "seen", "history", "cache", "timestamp")


def no_server_error(response: Response) -> None:
    """Assert a hostile input produced a controlled answer, chapter 13."""
    assert response.status_code < 500, (
        f"{response.url} answered {response.status_code}: {response.text[:200]}"
    )
    assert response.status_code in (200, 400, 401, 404, 405, 413, 415), (
        f"unexpected status {response.status_code}"
    )
    lowered = response.content.lower()
    for marker in (b"fatal error", b"stack trace", b"warning:", b"notice:", b"#0 "):
        assert marker not in lowered, f"{marker!r} leaked into the response"


@pytest.mark.parametrize(
    "body",
    [
        pytest.param(b'{"type":"auth-none"', id="truncated-json"),
        pytest.param(b"\x00" * 1024, id="null-bytes"),
        pytest.param(bytes(range(256)) * 4, id="binary-junk"),
        pytest.param(b"A" * 200_000, id="very-long-single-line"),
        pytest.param(b'{"type":"auth-none","requestId":"' + b"a" * 100_000 + b'"}',
                     id="very-long-member"),
        pytest.param('{"type":"auth-none","requestId":"\u00e9\u4e2d"}'.encode(),
                     id="non-ascii"),
        pytest.param(b'{"type":"auth-none","requestId":null}', id="null-member"),
        pytest.param(b"[]", id="json-array"),
        pytest.param(b"null", id="json-null"),
        pytest.param(b'{"type":"auth-none","requestId":{"$ne":1}}', id="nested-object"),
    ],
)
def test_hostile_health_bodies_never_crash(client: PhpVpnClient, body: bytes) -> None:
    """Chapter 13, malformed input never yields `500` or a stack trace."""
    response, _ = client.health(body=body)
    no_server_error(response)


@pytest.mark.parametrize(
    "body",
    [
        pytest.param(b"\x00" * 4096, id="null-bytes"),
        pytest.param(os.urandom(4096), id="random-bytes"),
        pytest.param(b"A" * 200_000, id="very-long-single-line"),
        pytest.param(b"\n" * 1000, id="only-line-feeds"),
        pytest.param(b'{"type":"auth-full"}\n' * 100, id="repeated-auth-records"),
        pytest.param(b'{"type":"data"}\n' * 100, id="only-data-records"),
        pytest.param(b'{"type":"auth-full","src":"10.99.99.2"' + b"," * 5000 + b"}\n",
                     id="broken-object"),
    ],
)
def test_hostile_data_bodies_never_crash(client: PhpVpnClient, body: bytes) -> None:
    """Chapter 13, the NDJSON reader survives arbitrary bytes."""
    response = client.request(
        "POST", action="data", body=body, content_type=NDJSON_CONTENT_TYPE
    )
    no_server_error(response)


@pytest.mark.parametrize(
    "query",
    [
        pytest.param("action=health&action=data", id="repeated-action"),
        pytest.param("action[]=health", id="array-action"),
        pytest.param("action=health%00data", id="null-byte-in-action"),
        pytest.param("action=../phpvpn", id="path-traversal-action"),
        pytest.param("action=health&x=" + "a" * 5000, id="long-extra-parameter"),
        pytest.param("action=HEALTH%20", id="encoded-whitespace"),
    ],
)
def test_hostile_query_strings_never_crash(client: PhpVpnClient, query: str) -> None:
    """Chapter 4.1, only `?action=<name>` is meaningful and nothing crashes."""
    response = client.request(
        "POST",
        raw_query=query,
        body=build_auth_none(new_uuid4()),
        content_type=JSON_CONTENT_TYPE,
    )
    no_server_error(response)


@pytest.mark.parametrize(
    "charset",
    ["utf-7", "iso-8859-2", "utf-8, utf-8", "\"utf-8\"", "utf-8; boundary=x"],
)
def test_unusual_charset_parameters(client: PhpVpnClient, charset: str) -> None:
    """Chapter 13, a strange media type parameter never crashes the server."""
    response, _ = client.health(content_type=f"application/json; charset={charset}")
    no_server_error(response)


@pytest.mark.parametrize(
    "address",
    [
        "10.99.99.2/../..",
        "..%2f..%2f10.99.99.2",
        "../../10_99_99_2",
        "10.99.99.2\\..\\..",
        "10_99_99_2",
        "10.99.99.2/info.json",
        "/etc/passwd",
        "C:\\windows\\system32",
        "10.99.99.2%00",
        ".",
        "..",
    ],
)
def test_path_traversal_in_the_source_address(
    client: PhpVpnClient, address: str
) -> None:
    """Chapter 10.5, storage paths derive only from a validated VPN address."""
    response, _ = client.send_data(address, access_key=bytes(32))
    no_server_error(response)
    assert response.status_code in (400, 401), response.status_code


@pytest.mark.parametrize(
    "address",
    [
        "10.99.99.2/../..",
        "..%2f..%2f10.99.99.3",
        "../../10_99_99_3",
        "10.99.99.3/info.json",
        "/etc/passwd",
        "10.99.99.3%00",
        "..",
    ],
)
def test_path_traversal_in_the_destination_address(
    client: PhpVpnClient, address: str, clean_queues: None
) -> None:
    """Chapter 9.3, an invalid `dst` never reaches the storage layer."""
    sender = client.configuration.addresses[0]
    record = make_record(sender, "10.99.99.3", b"traversal")
    line = build_data_line(
        record.message_id,
        sender,
        address,
        b64u_encode(record.iv),
        b64u_encode(record.tag),
        b64u_encode(record.ciphertext),
    )
    response, _ = client.send_data(sender, record_lines=[line])
    no_server_error(response)
    assert response.status_code == 400, response.status_code


def buffer_root(client: PhpVpnClient) -> Path:
    path = client.configuration.buffer_directory
    if not path.is_dir():
        pytest.skip(f"the buffer directory {path} is not visible to the test host")
    return path


def test_buffer_directory_has_no_replay_artifacts(client: PhpVpnClient) -> None:
    """Chapter 16.6.6, no nonce or replay cache exists in the storage root."""
    root = buffer_root(client)
    for entry in root.rglob("*"):
        name = entry.name.lower()
        for marker in REPLAY_MARKERS:
            assert marker not in name, f"{entry} looks like a replay artifact"


def test_buffer_directory_layout(client: PhpVpnClient, clean_queues: None) -> None:
    """Chapters 10.5 and 16.4.17, only per client directories and markers."""
    root = buffer_root(client)
    address = client.configuration.addresses[0]
    client.send_data(address)

    expected = root / address.replace(".", "_")
    assert expected.is_dir(), "the per client directory uses underscores"

    for entry in root.iterdir():
        if entry.is_dir():
            assert CLIENT_DIRECTORY.match(entry.name), entry.name
            continue
        assert entry.name in {".sweep", ".htaccess", "web.config", "index.html"}, (
            f"unexpected file in the storage root: {entry.name}"
        )

    for directory in root.iterdir():
        if not directory.is_dir():
            continue
        for entry in directory.iterdir():
            if entry.name in {"info.json", ".lock"}:
                continue
            assert BATCH_NAME.match(entry.name), (
                f"unexpected file in a client queue: {entry.name}"
            )


def test_batch_file_name_carries_the_expiry(
    client: PhpVpnClient, limits: dict[str, int], clean_queues: None
) -> None:
    """Chapter 16.4.16, the batch name holds exactly one UTC expiry time."""
    root = buffer_root(client)
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    record = make_record(sender, receiver, b"named batch")
    client.send_data(sender, [record])

    directory = root / receiver.replace(".", "_")
    batches = [entry for entry in directory.iterdir() if BATCH_NAME.match(entry.name)]
    assert batches, "the published batch must be visible in the destination queue"

    from phpvpn_client import parse_timestamp

    for batch in batches:
        stamp, size, source, _ = batch.name.split("_", 3)
        # The batch name uses hyphens instead of colons in the time part.
        expiry = parse_timestamp(f"{stamp[:10]}T{stamp[11:23].replace('-', ':')}Z")
        ttl = limits["queueTtlSeconds"] * 1000
        assert 0 < expiry - client.clock.now_ms() <= ttl + 5000
        assert int(size) > 0
        assert source == sender


def test_client_marker_is_an_empty_file(client: PhpVpnClient, clean_queues: None) -> None:
    """Chapter 16.5.2, `info.json` is a zero length marker."""
    root = buffer_root(client)
    address = client.configuration.addresses[0]
    client.send_data(address)
    marker = root / address.replace(".", "_") / "info.json"
    assert marker.is_file(), "the authenticated source gets a lastSeen marker"
    assert marker.stat().st_size == 0, "the marker carries no content"


def test_storage_is_not_reachable_over_http(client: PhpVpnClient) -> None:
    """Chapter 16.5.8, buffers and markers are not served over HTTP."""
    base = client.configuration.base_url.rsplit("/", 1)[0]
    for suffix in (
        "/phpvpn-buffer/10_99_99_2/info.json",
        "/phpvpn/10_99_99_2/info.json",
        "/../phpvpn-buffer/10_99_99_2/info.json",
        "/phpvpn-buffer/",
    ):
        raw = client.session.get(base + suffix, timeout=10)
        assert raw.status_code in (400, 403, 404), (
            f"{suffix} answered {raw.status_code}"
        )
        assert b"info.json" not in raw.content


def test_a_replayed_send_creates_a_second_copy(
    client: PhpVpnClient, clean_queues: None
) -> None:
    """Chapter 10.3, a replay inside the window publishes the data again."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    record = make_record(sender, receiver, b"replayed")
    body, request_id = client.build_data_body(sender, [record.line])

    first, _ = client.send_data(sender, body=body)
    second, _ = client.send_data(sender, body=body)
    assert first.status_code == second.status_code == 200
    assert parse_data_response(first.content).accepted_message_ids == [record.message_id]
    assert parse_data_response(second.content).accepted_message_ids == [record.message_id]

    delivered = client.poll(receiver)
    ids = [line.message_id for line in delivered.records]
    assert ids == [record.message_id, record.message_id], (
        "the same messageId is stored twice, chapter 6.3 forbids deduplication"
    )


def test_a_replayed_poll_drains_the_victim_queue(
    client: PhpVpnClient, clean_queues: None
) -> None:
    """Chapter 16.6.7, the documented and accepted plain HTTP replay risk."""
    sender, victim = client.configuration.addresses[0], client.configuration.addresses[1]
    record = make_record(sender, victim, b"stolen ciphertext")

    captured, request_id = client.build_data_body(victim)
    client.send_data(sender, [record])

    attacker, _ = client.send_data(victim, body=captured)
    assert attacker.status_code == 200
    stolen = parse_data_response(attacker.content)
    assert [line.message_id for line in stolen.records] == [record.message_id]

    legitimate = client.poll(victim)
    assert legitimate.records == [], (
        "the relay does not restore a queue drained by a replayed poll"
    )


def test_a_health_response_cannot_be_replayed_for_a_new_request(
    client: PhpVpnClient,
) -> None:
    """Chapter 16.6.9, an old response fails on the requestId mismatch."""
    old_response, old_id = client.health()
    old_payload = old_response.json()
    assert client.verify_health(old_payload, old_id)

    fresh_id = new_uuid4()
    assert not client.verify_health(old_payload, fresh_id)

    forged = dict(old_payload, requestId=fresh_id)
    assert not client.verify_health(forged, fresh_id), (
        "changing the requestId invalidates the GMAC"
    )


def test_repeated_identifiers_are_not_compared_with_history(
    client: PhpVpnClient, clean_queues: None
) -> None:
    """Chapter 16.6.4, `messageId`, IV and tag are never matched historically."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    message_id = new_uuid4()
    record = make_record(sender, receiver, b"same identifier", message_id=message_id)

    for _ in range(3):
        response, _ = client.send_data(sender, [record])
        assert response.status_code == 200
        assert parse_data_response(response.content).accepted_message_ids == [message_id]

    delivered = client.poll(receiver)
    assert [line.message_id for line in delivered.records] == [message_id] * 3
    assert all(line.decrypt() == b"same identifier" for line in delivered.records)


def test_error_responses_stay_generic(client: PhpVpnClient) -> None:
    """Chapter 13, an error never reveals whether a client exists."""
    known = client.configuration.addresses[0]
    unknown = "10.99.99.201"
    from_known, _ = client.send_data(known, access_key=bytes(32))
    from_unknown, _ = client.send_data(unknown, access_key=bytes(32))
    assert from_known.status_code == from_unknown.status_code == 401
    assert from_known.content == from_unknown.content
    assert_protocol_headers(from_unknown, JSON_CONTENT_TYPE)
