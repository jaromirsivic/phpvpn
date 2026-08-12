"""Successful `data` transport, chapters 9, 10 and 16.4.

Every test in this module uses the `clean_queues` fixture, which drains the
queue of every configured client through ordinary authenticated polls before
and after the test. Nothing in the suite touches the buffer directory of the
deployment directly.
"""

from __future__ import annotations

import pytest

from phpvpn_client import (
    NDJSON_CONTENT_TYPE,
    PhpVpnClient,
    assert_protocol_headers,
    b64u_decode,
    b64u_encode,
    data_aad,
    data_mac_canonical,
    hmac_sha256,
    make_record,
    new_uuid4,
    parse_data_response,
    payload_key_for,
    sha256_hex,
)

RESULT_FIELDS = [
    "type",
    "requestId",
    "acceptedMessageIds",
    "deferredMessageIds",
    "acceptedRecords",
    "acceptedBytes",
]
RECORD_FIELDS = ["type", "messageId", "src", "dst", "iv", "tag", "ciphertext"]
MAC_FIELDS = ["type", "requestId", "tag"]

pytestmark = pytest.mark.usefixtures("clean_queues")


def test_empty_poll_returns_only_result_and_mac(client: PhpVpnClient) -> None:
    """Chapter 9.5, an empty buffer yields `data-result` and `data-mac`."""
    address = client.configuration.addresses[0]
    response, request_id = client.send_data(address)
    assert response.status_code == 200
    assert_protocol_headers(response, NDJSON_CONTENT_TYPE)

    parsed = parse_data_response(response.content)
    assert parsed.records == []
    assert list(parsed.result_order) == RESULT_FIELDS
    assert list(parsed.mac_order) == MAC_FIELDS
    assert parsed.result["requestId"] == request_id
    assert parsed.mac["requestId"] == request_id
    assert parsed.result["acceptedMessageIds"] == []
    assert parsed.result["deferredMessageIds"] == []
    assert parsed.result["acceptedRecords"] == 0
    assert parsed.result["acceptedBytes"] == 0
    assert parsed.verify_mac(client.full_auth_key(address), request_id)


def test_round_trip_between_two_clients(client: PhpVpnClient) -> None:
    """Chapter 9.5, the peer receives the exact bytes of every record."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    records = [make_record(sender, receiver, f"packet {index}".encode()) for index in range(3)]

    response, request_id = client.send_data(sender, records)
    assert response.status_code == 200
    sent = parse_data_response(response.content)
    assert sent.accepted_message_ids == [record.message_id for record in records]
    assert sent.records == [], "records for another client are not echoed back"

    delivered = client.poll(receiver)
    assert len(delivered.records) == len(records)
    by_id = {line.message_id: line for line in delivered.records}
    for record in records:
        line = by_id[record.message_id]
        assert line.raw + b"\n" == record.line, "the delivered bytes must be identical"
        assert list(line.order) == RECORD_FIELDS
        assert line.decrypt() == record.plaintext

    assert client.poll(receiver).records == [], "delivery is at most once"


def test_records_addressed_to_the_sender_return_in_the_same_response(
    client: PhpVpnClient,
) -> None:
    """Chapter 9.5, a record whose `dst` is the caller is drained immediately."""
    address = client.configuration.addresses[0]
    record = make_record(address, address, b"loopback")

    response, request_id = client.send_data(address, [record])
    parsed = parse_data_response(response.content)
    assert [line.message_id for line in parsed.records] == [record.message_id]
    assert parsed.accepted_message_ids == [record.message_id]
    assert parsed.records[0].raw + b"\n" == record.line
    assert parsed.records[0].decrypt() == b"loopback"
    assert parsed.verify_mac(client.full_auth_key(address), request_id)


def test_data_result_counts_accepted_records_and_bytes(client: PhpVpnClient) -> None:
    """Chapter 9.5, `acceptedBytes` counts serialized lines including the LF."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[2]
    records = [
        make_record(sender, receiver, bytes(size)) for size in (1, 64, 1500, 4096)
    ]
    response, request_id = client.send_data(sender, records)
    parsed = parse_data_response(response.content)

    assert parsed.result["acceptedRecords"] == len(records)
    assert parsed.accepted_message_ids == [record.message_id for record in records]
    assert parsed.deferred_message_ids == []
    assert parsed.result["acceptedBytes"] == sum(len(record.line) for record in records)


def test_accepted_and_deferred_cover_every_message_id(client: PhpVpnClient) -> None:
    """Chapter 9.5, the two lists are disjoint and cover the whole request."""
    sender = client.configuration.addresses[0]
    destinations = client.configuration.addresses[1:3]
    records = [
        make_record(sender, destination, f"to {destination}".encode())
        for destination in destinations
        for _ in range(2)
    ]
    response, _ = client.send_data(sender, records)
    parsed = parse_data_response(response.content)

    accepted = set(parsed.accepted_message_ids)
    deferred = set(parsed.deferred_message_ids)
    assert accepted & deferred == set()
    assert accepted | deferred == {record.message_id for record in records}


def test_data_mac_authenticates_the_result_line(client: PhpVpnClient) -> None:
    """Chapter 9.5, the MAC covers the exact `data-result` bytes."""
    address = client.configuration.addresses[0]
    record = make_record(address, address, b"mac coverage")
    response, request_id = client.send_data(address, [record])
    parsed = parse_data_response(response.content)
    key = client.full_auth_key(address)

    assert parsed.mac["tag"] == parsed.expected_mac(key, request_id)

    tampered = parsed.result_line.replace(b'"acceptedRecords":1', b'"acceptedRecords":2')
    assert tampered != parsed.result_line
    forged = b64u_encode(
        hmac_sha256(key, data_mac_canonical(request_id, parsed.pairs, tampered))
    )
    assert forged != parsed.mac["tag"], "a changed data-result must change the MAC"


def test_data_mac_authenticates_the_ordered_record_list(client: PhpVpnClient) -> None:
    """Chapter 16.3.19, count, order and identity of the records are covered."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    records = [make_record(sender, receiver, f"line {index}".encode()) for index in range(3)]
    client.send_data(sender, records)

    response, request_id = client.send_data(receiver)
    parsed = parse_data_response(response.content)
    key = client.full_auth_key(receiver)
    assert parsed.verify_mac(key, request_id)

    pairs = parsed.pairs
    variants = {
        "reordered": list(reversed(pairs)),
        "removed": pairs[:-1],
        "added": pairs + [(new_uuid4(), b64u_encode(bytes(16)))],
        "changed message id": [(new_uuid4(), pairs[0][1])] + pairs[1:],
        "changed gcm tag": [(pairs[0][0], b64u_encode(bytes(16)))] + pairs[1:],
    }
    for label, variant in variants.items():
        forged = b64u_encode(
            hmac_sha256(key, data_mac_canonical(request_id, variant, parsed.result_line))
        )
        assert forged != parsed.mac["tag"], f"{label} must invalidate the MAC"


def test_data_mac_is_bound_to_the_request_id(client: PhpVpnClient) -> None:
    """Chapter 9.5, the MAC and the result carry the current requestId."""
    address = client.configuration.addresses[0]
    response, request_id = client.send_data(address)
    parsed = parse_data_response(response.content)
    key = client.full_auth_key(address)
    assert parsed.verify_mac(key, request_id)
    assert not parsed.verify_mac(key, new_uuid4())


def test_data_mac_requires_the_client_access_key(client: PhpVpnClient) -> None:
    """Chapter 9.5, the response MAC uses the per client `fullAuthKey`."""
    address = client.configuration.addresses[0]
    other = client.configuration.addresses[1]
    response, request_id = client.send_data(address)
    parsed = parse_data_response(response.content)
    assert parsed.verify_mac(client.full_auth_key(address), request_id)
    assert not parsed.verify_mac(client.full_auth_key(other), request_id)


def test_mac_canonical_uses_decimal_record_count(client: PhpVpnClient) -> None:
    """Chapter 9.5, `DECIMAL(recordCount)` has no leading zero."""
    address = client.configuration.addresses[0]
    records = [make_record(address, address, b"count") for _ in range(2)]
    response, request_id = client.send_data(address, records)
    parsed = parse_data_response(response.content)
    canonical = data_mac_canonical(request_id, parsed.pairs, parsed.result_line)
    lines = canonical.split(b"\n")
    assert lines[0] == b"PHPVPN-DATA-RESPONSE"
    assert lines[1] == b"data"
    assert lines[2] == request_id.encode()
    assert lines[3] == str(len(parsed.records)).encode()
    assert lines[-1] == sha256_hex(parsed.result_line).encode()
    assert not canonical.endswith(b"\n")
    assert parsed.mac["tag"] == b64u_encode(
        hmac_sha256(client.full_auth_key(address), canonical)
    )


def test_response_records_are_authenticated_end_to_end(client: PhpVpnClient) -> None:
    """Chapter 9.4, every delivered record verifies under its own GCM tag."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    record = make_record(sender, receiver, b"end to end payload")
    client.send_data(sender, [record])

    delivered = client.poll(receiver).records[0]
    assert delivered.decrypt() == b"end to end payload"

    key = payload_key_for(sender, receiver)
    iv = b64u_decode(delivered.fields["iv"])
    sealed = b64u_decode(delivered.fields["ciphertext"]) + b64u_decode(delivered.fields["tag"])
    from cryptography.exceptions import InvalidTag
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM

    for field in ("messageId", "src", "dst"):
        forged = dict(delivered.fields)
        forged[field] = new_uuid4() if field == "messageId" else "10.99.99.254"
        aad = data_aad(forged["messageId"], forged["src"], forged["dst"], forged["iv"])
        with pytest.raises(InvalidTag):
            AESGCM(key).decrypt(iv, sealed, aad)


def test_delivery_is_at_most_once_per_record(client: PhpVpnClient) -> None:
    """Chapter 9.5, a drained record is never offered again."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    record = make_record(sender, receiver, b"only once")
    client.send_data(sender, [record])

    first = client.poll(receiver)
    assert [line.message_id for line in first.records] == [record.message_id]
    for _ in range(3):
        assert client.poll(receiver).records == []


def test_a_response_may_carry_more_records_than_the_request_limit(
    client: PhpVpnClient, limits: dict[str, int]
) -> None:
    """Chapter 16.4.3, the response is not paginated and not capped at 16."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    maximum = limits["maxDataRecordsPerRequest"]
    sent = []
    for batch in range(2):
        records = [
            make_record(sender, receiver, f"batch {batch} record {index}".encode())
            for index in range(maximum)
        ]
        response, _ = client.send_data(sender, records)
        parsed = parse_data_response(response.content)
        assert parsed.result["acceptedRecords"] == maximum
        sent.extend(records)

    delivered = client.poll(receiver)
    assert len(delivered.records) == 2 * maximum > maximum
    assert {line.message_id for line in delivered.records} == {
        record.message_id for record in sent
    }


def test_batches_published_for_several_destinations_are_independent(
    client: PhpVpnClient,
) -> None:
    """Chapter 10.2, destination groups are processed independently."""
    sender = client.configuration.addresses[0]
    receivers = client.configuration.addresses[1:4]
    records = {
        receiver: make_record(sender, receiver, f"for {receiver}".encode())
        for receiver in receivers
    }
    response, _ = client.send_data(sender, list(records.values()))
    parsed = parse_data_response(response.content)
    assert set(parsed.accepted_message_ids) == {r.message_id for r in records.values()}

    for receiver, record in records.items():
        delivered = client.poll(receiver)
        assert [line.message_id for line in delivered.records] == [record.message_id]
        assert delivered.records[0].decrypt() == f"for {receiver}".encode()


def test_a_client_only_receives_its_own_records(client: PhpVpnClient) -> None:
    """Chapter 10.1, a queue belongs to the destination address."""
    sender = client.configuration.addresses[0]
    receiver = client.configuration.addresses[1]
    other = client.configuration.addresses[2]
    record = make_record(sender, receiver, b"private")
    client.send_data(sender, [record])

    assert client.poll(other).records == []
    assert [line.message_id for line in client.poll(receiver).records] == [
        record.message_id
    ]


def test_polling_drains_even_when_the_request_also_sends(client: PhpVpnClient) -> None:
    """Chapter 9.5, sending and draining happen in the same request."""
    first, second = client.configuration.addresses[0], client.configuration.addresses[1]
    incoming = make_record(second, first, b"waiting for you")
    client.send_data(second, [incoming])

    outgoing = make_record(first, second, b"and back")
    response, request_id = client.send_data(first, [outgoing])
    parsed = parse_data_response(response.content)

    assert parsed.accepted_message_ids == [outgoing.message_id]
    assert [line.message_id for line in parsed.records] == [incoming.message_id]
    assert parsed.records[0].decrypt() == b"waiting for you"
    assert parsed.verify_mac(client.full_auth_key(first), request_id)


def test_maximum_number_of_records_is_accepted(
    client: PhpVpnClient, limits: dict[str, int]
) -> None:
    """Chapter 16.2.4, exactly `maxDataRecordsPerRequest` records pass."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    maximum = limits["maxDataRecordsPerRequest"]
    records = [make_record(sender, receiver, bytes([index])) for index in range(maximum)]
    response, _ = client.send_data(sender, records)
    assert response.status_code == 200
    parsed = parse_data_response(response.content)
    assert parsed.result["acceptedRecords"] == maximum

    delivered = client.poll(receiver)
    assert len(delivered.records) == maximum


def test_response_lines_are_canonical_base64url(client: PhpVpnClient) -> None:
    """Chapter 6.1, every base64url value of a response is canonical."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    client.send_data(sender, [make_record(sender, receiver, b"canonical")])
    delivered = client.poll(receiver)
    for line in delivered.records:
        for field in ("iv", "tag", "ciphertext"):
            text = line.fields[field]
            assert b64u_encode(b64u_decode(text)) == text
    assert b64u_encode(b64u_decode(delivered.mac["tag"])) == delivered.mac["tag"]
    assert len(b64u_decode(delivered.mac["tag"])) == 32
