"""Rejected `data` requests, chapters 6.2, 9.1, 9.2, 9.3, 13 and 16.2.

Authentication is verified before framing, chapter 9.2, so every request that
is meant to fail on framing is signed correctly first. The helpers below always
sign the exact transmitted bytes.
"""

from __future__ import annotations

import pytest

from phpvpn_client import (
    JSON_CONTENT_TYPE,
    PhpVpnClient,
    assert_protocol_headers,
    b64u_encode,
    build_data_line,
    compact_json,
    make_record,
    new_uuid4,
    parse_data_response,
)

UNKNOWN_SOURCE = "10.99.99.200"

pytestmark = pytest.mark.usefixtures("clean_queues")


def expect_status(response, status: int, content_type: str = JSON_CONTENT_TYPE) -> None:
    """Assert the status, the generic error body and the mandatory headers."""
    assert response.status_code == status, (
        f"expected {status}, got {response.status_code}: {response.text[:200]}"
    )
    assert_protocol_headers(response, content_type)
    document = response.json()
    assert set(document) == {"type", "code"}
    assert document["type"] == "error"


def unchanged_queue(client: PhpVpnClient, address: str) -> None:
    """Assert that a rejected request published nothing, chapter 17."""
    assert client.poll(address).records == [], "a rejected request must not publish"


# ---------------------------------------------------------------------------
# 401 unauthorized
# ---------------------------------------------------------------------------


def test_wrong_access_key_is_unauthorized(client: PhpVpnClient) -> None:
    """Chapter 7.2, the correct tag requires the client access key."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    record = make_record(sender, receiver, b"forged")
    response, _ = client.send_data(sender, [record], access_key=bytes(32))
    expect_status(response, 401)
    assert response.json()["code"] == "unauthorized"
    unchanged_queue(client, receiver)


def test_another_clients_access_key_is_unauthorized(client: PhpVpnClient) -> None:
    """Chapter 16.3.9, `src` selects exactly one access key."""
    sender, other = client.configuration.addresses[0], client.configuration.addresses[1]
    foreign = client.configuration.access_keys[other]
    response, _ = client.send_data(sender, access_key=foreign)
    expect_status(response, 401)


def test_wrong_server_secret_is_unauthorized(client: PhpVpnClient) -> None:
    """Chapter 16.3.5, both secrets are needed to produce a valid tag."""
    sender = client.configuration.addresses[0]
    response, _ = client.send_data(sender, server_secret=b"not the server secret")
    expect_status(response, 401)


def test_unknown_source_address_is_unauthorized(client: PhpVpnClient) -> None:
    """Chapter 9.2, an unknown client gets the generic `401` after a dummy key."""
    response, _ = client.send_data(UNKNOWN_SOURCE, access_key=bytes(32))
    expect_status(response, 401)
    assert response.json()["code"] == "unauthorized"


def test_modified_auth_tag_is_unauthorized(client: PhpVpnClient) -> None:
    """Chapter 9.2, the HMAC tag is compared in constant time."""
    sender = client.configuration.addresses[0]
    body, _ = client.build_data_body(sender)
    index = body.index(b'"tag":"') + len(b'"tag":"')
    flipped = b"B" if body[index : index + 1] == b"A" else b"A"
    tampered = body[:index] + flipped + body[index + 1 :]
    response, _ = client.send_data(sender, body=tampered)
    expect_status(response, 401)


def test_modified_payload_after_signing_is_unauthorized(client: PhpVpnClient) -> None:
    """Chapter 9.2, the tag covers every byte after the first LF."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    signed = make_record(sender, receiver, b"original")
    transmitted = make_record(sender, receiver, b"replaced")
    response, _ = client.send_data(
        sender,
        signed_body=signed.line,
        transmitted_body=transmitted.line,
        record_lines=[],
    )
    expect_status(response, 401)
    unchanged_queue(client, receiver)


def test_truncated_payload_after_signing_is_unauthorized(client: PhpVpnClient) -> None:
    """Chapter 9.2, removing a signed record invalidates the tag."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    records = [make_record(sender, receiver, f"record {i}".encode()) for i in range(2)]
    signed = b"".join(record.line for record in records)
    response, _ = client.send_data(
        sender, signed_body=signed, transmitted_body=records[0].line, record_lines=[]
    )
    expect_status(response, 401)
    unchanged_queue(client, receiver)


def test_record_source_must_match_the_authenticated_source(
    client: PhpVpnClient,
) -> None:
    """Chapter 9.3, a spoofed `src` in a data record returns `401`."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    spoofed = make_record(client.configuration.addresses[2], receiver, b"spoofed")
    response, _ = client.send_data(sender, [spoofed])
    expect_status(response, 401)
    unchanged_queue(client, receiver)


def test_spoofed_source_is_rejected_for_every_record(client: PhpVpnClient) -> None:
    """Chapter 16.3.9, every data record must carry the authenticated `src`."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    good = make_record(sender, receiver, b"good")
    spoofed = make_record(client.configuration.addresses[3], receiver, b"bad")
    response, _ = client.send_data(sender, [good, spoofed])
    expect_status(response, 401)
    unchanged_queue(client, receiver)


def test_timestamp_outside_the_window_is_unauthorized(client: PhpVpnClient) -> None:
    """Chapter 9.2, a stale timestamp returns the generic `401`."""
    from phpvpn_client import format_timestamp

    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    stale = format_timestamp(client.clock.arrival_ms() - 120_000)
    record = make_record(sender, receiver, b"too old")
    response, _ = client.send_data(sender, [record], timestamp=stale)
    expect_status(response, 401)
    unchanged_queue(client, receiver)


# ---------------------------------------------------------------------------
# 400 bad_request, framing
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "mutation",
    [
        pytest.param("missing-final-lf", id="missing-final-lf"),
        pytest.param("empty-line", id="empty-line"),
        pytest.param("crlf", id="crlf-line-ending"),
        pytest.param("cr-only", id="cr-only-line-ending"),
        pytest.param("leading-empty-line", id="leading-empty-line"),
        pytest.param("trailing-empty-line", id="trailing-empty-line"),
    ],
)
def test_invalid_framing_is_rejected(client: PhpVpnClient, mutation: str) -> None:
    """Chapter 6.2, every record ends with exactly one LF."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    line = make_record(sender, receiver, b"framing").line
    payload = {
        "missing-final-lf": line[:-1],
        "empty-line": line[:-1] + b"\n\n",
        "crlf": line[:-1] + b"\r\n",
        "cr-only": line[:-1] + b"\r",
        "leading-empty-line": b"\n" + line,
        "trailing-empty-line": line + b"\n",
    }[mutation]
    response, _ = client.send_data(sender, record_lines=[payload])
    expect_status(response, 400)
    assert response.json()["code"] == "bad_request"
    unchanged_queue(client, receiver)


def test_auth_line_without_lf_is_rejected(client: PhpVpnClient) -> None:
    """Chapter 6.2, the `auth-full` record itself must end with LF."""
    sender = client.configuration.addresses[0]
    body, _ = client.build_data_body(sender)
    response, _ = client.send_data(sender, body=body[:-1])
    expect_status(response, 400)


def test_duplicate_message_id_in_one_request_is_rejected(
    client: PhpVpnClient,
) -> None:
    """Chapter 9.3, `messageId` must be unique inside one request."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    message_id = new_uuid4()
    first = make_record(sender, receiver, b"first", message_id=message_id)
    second = make_record(sender, receiver, b"second", message_id=message_id)
    response, _ = client.send_data(sender, [first, second])
    expect_status(response, 400)
    unchanged_queue(client, receiver)


def test_record_member_order_is_normative(client: PhpVpnClient) -> None:
    """Chapter 9.3, the member order of a `data` record is fixed."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    record = make_record(sender, receiver, b"order")
    reordered = compact_json(
        [
            ("type", "data"),
            ("messageId", record.message_id),
            ("src", record.src),
            ("dst", record.dst),
            ("tag", b64u_encode(record.tag)),
            ("iv", b64u_encode(record.iv)),
            ("ciphertext", b64u_encode(record.ciphertext)),
        ]
    ) + b"\n"
    response, _ = client.send_data(sender, record_lines=[reordered])
    expect_status(response, 400)
    unchanged_queue(client, receiver)


def test_member_after_ciphertext_is_rejected(client: PhpVpnClient) -> None:
    """Chapter 9.3, `ciphertext` is the last member of the object."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    record = make_record(sender, receiver, b"tail")
    line = record.line[:-2] + b',"extra":"x"}\n'
    response, _ = client.send_data(sender, record_lines=[line])
    expect_status(response, 400)


@pytest.mark.parametrize(
    "template",
    [
        pytest.param(b'{"type": "data"', id="space-after-colon"),
        pytest.param(b'{ "type":"data"', id="space-after-brace"),
        pytest.param(b'{"type":"data" ', id="space-before-comma"),
        pytest.param(b'{\n"type":"data"', id="newline-inside-object"),
        pytest.param(b'{"type":"data"\t', id="tab-inside-object"),
    ],
)
def test_whitespace_inside_a_record_is_rejected(
    client: PhpVpnClient, template: bytes
) -> None:
    """Chapter 9.3, a `data` record is compact JSON without whitespace."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    record = make_record(sender, receiver, b"whitespace")
    line = record.line.replace(b'{"type":"data"', template, 1)
    assert line != record.line
    response, _ = client.send_data(sender, record_lines=[line])
    expect_status(response, 400)
    unchanged_queue(client, receiver)


@pytest.mark.parametrize(
    "field,value",
    [
        pytest.param("iv", b64u_encode(bytes(11)), id="iv-11-bytes"),
        pytest.param("iv", b64u_encode(bytes(13)), id="iv-13-bytes"),
        pytest.param("iv", "", id="iv-empty"),
        pytest.param("tag", b64u_encode(bytes(15)), id="tag-15-bytes"),
        pytest.param("tag", b64u_encode(bytes(17)), id="tag-17-bytes"),
        pytest.param("tag", "", id="tag-empty"),
        pytest.param("ciphertext", "", id="ciphertext-empty"),
    ],
)
def test_wrong_value_length_is_rejected(
    client: PhpVpnClient, field: str, value: str
) -> None:
    """Chapter 9.3, IV is 12 bytes, tag is 16 bytes, ciphertext is not empty."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    record = make_record(sender, receiver, b"lengths")
    fields = {
        "type": "data",
        "messageId": record.message_id,
        "src": record.src,
        "dst": record.dst,
        "iv": b64u_encode(record.iv),
        "tag": b64u_encode(record.tag),
        "ciphertext": b64u_encode(record.ciphertext),
    }
    fields[field] = value
    line = build_data_line(
        fields["messageId"],
        fields["src"],
        fields["dst"],
        fields["iv"],
        fields["tag"],
        fields["ciphertext"],
    )
    response, _ = client.send_data(sender, record_lines=[line])
    expect_status(response, 400)
    unchanged_queue(client, receiver)


@pytest.mark.parametrize(
    "field,mutate",
    [
        pytest.param("tag", "non-canonical", id="tag-non-canonical"),
        pytest.param("tag", "padding", id="tag-padding"),
        pytest.param("tag", "plus", id="tag-plus-character"),
        pytest.param("tag", "slash", id="tag-slash-character"),
        pytest.param("tag", "modulo-one", id="tag-length-1-modulo-4"),
        pytest.param("iv", "padding", id="iv-padding"),
        pytest.param("iv", "plus", id="iv-plus-character"),
        pytest.param("iv", "space", id="iv-whitespace"),
        pytest.param("ciphertext", "non-canonical", id="ciphertext-non-canonical"),
        pytest.param("ciphertext", "padding", id="ciphertext-padding"),
        pytest.param("ciphertext", "modulo-one", id="ciphertext-length-1-modulo-4"),
    ],
)
def test_non_canonical_base64url_is_rejected(
    client: PhpVpnClient, field: str, mutate: str
) -> None:
    """Chapter 6.1 and 16.2.7, only canonical base64url is accepted.

    The ciphertext is five bytes long, so its encoding has three characters in
    the last group and two unused low bits that must be zero. The 16 byte tag
    encodes to 22 characters with four unused low bits.
    """
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    record = make_record(sender, receiver, b"12345")
    values = {
        "iv": b64u_encode(record.iv),
        "tag": b64u_encode(record.tag),
        "ciphertext": b64u_encode(record.ciphertext),
    }
    original = values[field]
    if mutate == "non-canonical":
        # Set an unused low bit of the final character, which a tolerant
        # decoder would silently ignore.
        alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
        last = alphabet.index(original[-1])
        values[field] = original[:-1] + alphabet[last | 1]
        assert values[field] != original
    elif mutate == "padding":
        values[field] = original + "="
    elif mutate == "plus":
        values[field] = "+" + original[1:]
    elif mutate == "slash":
        values[field] = "/" + original[1:]
    elif mutate == "space":
        values[field] = " " + original[1:]
    elif mutate == "modulo-one":
        values[field] = original + "AA" if len(original) % 4 == 3 else original + "A"
        while len(values[field]) % 4 != 1:
            values[field] += "A"

    line = build_data_line(
        record.message_id,
        record.src,
        record.dst,
        values["iv"],
        values["tag"],
        values["ciphertext"],
    )
    response, _ = client.send_data(sender, record_lines=[line])
    expect_status(response, 400)
    unchanged_queue(client, receiver)


def test_canonical_edge_encodings_are_accepted(client: PhpVpnClient) -> None:
    """Chapter 16.2.7, `_w` and `__8` pass the canonical validation."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    from phpvpn_client import b64u_decode

    assert b64u_decode("_w") == b"\xff"
    assert b64u_decode("__8") == b"\xff\xff"
    with pytest.raises(ValueError):
        b64u_decode("_x")
    with pytest.raises(ValueError):
        b64u_decode("__9")

    # A one byte and a two byte ciphertext exercise both remainder classes on
    # the wire. Both are canonical, so the server must accept them.
    for plaintext in (b"\x00", b"\x00\x01"):
        record = make_record(sender, receiver, plaintext)
        response, _ = client.send_data(sender, [record])
        assert response.status_code == 200
        delivered = client.poll(receiver)
        assert delivered.records[0].decrypt() == plaintext


@pytest.mark.parametrize(
    "destination",
    [
        pytest.param("10.99.99.0", id="network-address"),
        pytest.param("10.99.99.1", id="gateway-address"),
        pytest.param("10.99.99.255", id="broadcast-address"),
        pytest.param("10.99.99.256", id="out-of-range-octet"),
        pytest.param("10.99.100.2", id="other-subnet"),
        pytest.param("10.99.99.02", id="leading-zero"),
        pytest.param("010.99.99.2", id="leading-zero-first-octet"),
        pytest.param("192.168.0.2", id="foreign-network"),
        pytest.param("10.99.99", id="three-octets"),
        pytest.param("10.99.99.2.2", id="five-octets"),
        pytest.param("", id="empty"),
        pytest.param("localhost", id="host-name"),
    ],
)
def test_invalid_destination_is_rejected(
    client: PhpVpnClient, destination: str
) -> None:
    """Chapters 3.1 and 9.3, `dst` must be a valid VPN address."""
    sender = client.configuration.addresses[0]
    record = make_record(sender, "10.99.99.3", b"routing")
    line = build_data_line(
        record.message_id,
        sender,
        destination,
        b64u_encode(record.iv),
        b64u_encode(record.tag),
        b64u_encode(record.ciphertext),
    )
    response, _ = client.send_data(sender, record_lines=[line])
    expect_status(response, 400)


def test_valid_destination_without_an_access_key_is_accepted(
    client: PhpVpnClient,
) -> None:
    """Chapter 3.1, any address of the range is a valid destination.

    The relay authenticates the sender only. A destination that has no
    configured access key is still a valid VPN address, so the record must be
    queued rather than rejected.
    """
    sender = client.configuration.addresses[0]
    unconfigured = "10.99.99.111"
    assert unconfigured not in client.configuration.access_keys
    record = make_record(sender, unconfigured, b"unconfigured destination")
    response, _ = client.send_data(sender, [record])
    assert response.status_code == 200
    parsed = parse_data_response(response.content)
    assert parsed.accepted_message_ids == [record.message_id]


@pytest.mark.parametrize(
    "source",
    [
        pytest.param("10.99.99.0", id="network-address"),
        pytest.param("10.99.99.255", id="broadcast-address"),
        pytest.param("10.99.99.02", id="leading-zero"),
        pytest.param("not-an-address", id="not-an-address"),
        pytest.param("", id="empty"),
    ],
)
def test_invalid_authenticated_source_is_rejected(
    client: PhpVpnClient, source: str
) -> None:
    """Chapters 9.2 and 13, a syntactically invalid `src` never authenticates.

    The specification allows two readings. Chapter 9.2 validates the syntax of
    `src` before any key derivation, which makes a malformed address a `400`
    value error, while the error table of chapter 13 lists an invalid `src`
    under `401`. Both are accepted here. The deployment answers `400`.
    """
    response, _ = client.send_data(source, access_key=bytes(32))
    assert response.status_code in (400, 401), (
        f"{source!r} produced {response.status_code}"
    )
    assert_protocol_headers(response, JSON_CONTENT_TYPE)


@pytest.mark.parametrize(
    "line",
    [
        pytest.param(b"{}\n", id="empty-object"),
        pytest.param(b'{"type":"data"}\n', id="only-type"),
        pytest.param(b'{"type":"unknown"}\n', id="unknown-type"),
        pytest.param(b'{"type":"auth-full"}\n', id="second-auth-record"),
        pytest.param(b"not json\n", id="not-json"),
        pytest.param(b'["type","data"]\n', id="json-array"),
        pytest.param(b'{"type":"data",}\n', id="trailing-comma"),
    ],
)
def test_malformed_record_object_is_rejected(client: PhpVpnClient, line: bytes) -> None:
    """Chapters 6.1 and 9.3, every record is a complete valid JSON object."""
    sender = client.configuration.addresses[0]
    response, _ = client.send_data(sender, record_lines=[line])
    expect_status(response, 400)


@pytest.mark.parametrize(
    "message_id",
    [
        pytest.param("not-a-uuid", id="not-a-uuid"),
        pytest.param("d5b543a9-9fc3-1c98-916c-ea0198f67a79", id="version-1"),
        pytest.param("D5B543A9-9FC3-4C98-916C-EA0198F67A79", id="uppercase"),
        pytest.param("", id="empty"),
    ],
)
def test_invalid_message_id_is_rejected(client: PhpVpnClient, message_id: str) -> None:
    """Chapter 6.3, `messageId` is a lowercase UUIDv4."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    record = make_record(sender, receiver, b"identifier")
    line = build_data_line(
        message_id,
        sender,
        receiver,
        b64u_encode(record.iv),
        b64u_encode(record.tag),
        b64u_encode(record.ciphertext),
    )
    response, _ = client.send_data(sender, record_lines=[line])
    expect_status(response, 400)


@pytest.mark.parametrize(
    "members",
    [
        pytest.param(
            [
                ("type", "auth-full"),
                ("src", "10.99.99.2"),
                ("timestamp", "2026-08-09T20:15:30.123Z"),
                ("requestId", new_uuid4()),
                ("tag", b64u_encode(bytes(32))),
                ("extra", 1),
            ],
            id="unknown-member",
        ),
        pytest.param(
            [
                ("type", "auth-full"),
                ("src", "10.99.99.2"),
                ("timestamp", "2026-08-09T20:15:30.123Z"),
                ("requestId", new_uuid4()),
            ],
            id="missing-tag",
        ),
        pytest.param(
            [
                ("type", "auth-full"),
                ("timestamp", "2026-08-09T20:15:30.123Z"),
                ("requestId", new_uuid4()),
                ("tag", b64u_encode(bytes(32))),
            ],
            id="missing-src",
        ),
        pytest.param(
            [
                ("type", "auth-semi"),
                ("src", "10.99.99.2"),
                ("timestamp", "2026-08-09T20:15:30.123Z"),
                ("requestId", new_uuid4()),
                ("tag", b64u_encode(bytes(32))),
            ],
            id="wrong-type",
        ),
        pytest.param(
            [
                ("type", "auth-full"),
                ("src", "10.99.99.2"),
                ("timestamp", "2026-08-09T20:15:30.123Z"),
                ("requestId", "not-a-uuid"),
                ("tag", b64u_encode(bytes(32))),
            ],
            id="bad-request-id",
        ),
        pytest.param(
            [
                ("type", "auth-full"),
                ("src", "10.99.99.2"),
                ("timestamp", "not a timestamp"),
                ("requestId", new_uuid4()),
                ("tag", b64u_encode(bytes(32))),
            ],
            id="bad-timestamp",
        ),
        pytest.param(
            [
                ("type", "auth-full"),
                ("src", "10.99.99.2"),
                ("timestamp", "2026-08-09T20:15:30.123Z"),
                ("requestId", new_uuid4()),
                ("tag", b64u_encode(bytes(31))),
            ],
            id="short-tag",
        ),
    ],
)
def test_malformed_auth_record_is_rejected(client: PhpVpnClient, members) -> None:
    """Chapters 6.1 and 9.2, the `auth-full` record has a closed field set."""
    sender = client.configuration.addresses[0]
    body = compact_json(members) + b"\n"
    response, _ = client.send_data(sender, body=body)
    expect_status(response, 400)


def test_body_without_an_auth_record_is_rejected(client: PhpVpnClient) -> None:
    """Chapter 9.1, the first record of the body is exactly one `auth-full`."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    record = make_record(sender, receiver, b"no auth")
    response, _ = client.send_data(sender, body=record.line)
    expect_status(response, 400)
    unchanged_queue(client, receiver)


def test_empty_body_is_rejected(client: PhpVpnClient) -> None:
    """Chapter 9.1, an empty body carries no `auth-full` record."""
    sender = client.configuration.addresses[0]
    response, _ = client.send_data(sender, body=b"")
    expect_status(response, 400)


def test_duplicate_json_member_in_the_auth_record_is_rejected(
    client: PhpVpnClient,
) -> None:
    """Chapter 6.1, duplicate top level member names are detected."""
    sender = client.configuration.addresses[0]
    line, _, _ = client.build_auth_full_line(sender, b"")
    duplicated = line[:-2] + b',"src":"10.99.99.3"}\n'
    response, _ = client.send_data(sender, body=duplicated)
    expect_status(response, 400)
