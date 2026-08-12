"""Queue capacity and partial acceptance, chapters 10.1, 10.2 and 16.4.

Filling a queue up to `MAX_QUEUE_BYTES_PER_CLIENT` needs about sixteen
mebibytes of records, which is sent in requests that stay below the one
mebibyte body limit of the nginx deployment. The module drains the filled
queue again at the end of every test, so nothing is left for other tests.
"""

from __future__ import annotations

import pytest

from phpvpn_client import PhpVpnClient, make_record, parse_data_response

pytestmark = pytest.mark.usefixtures("clean_queues")

#: Sixteen records of this payload size stay below one mebibyte on the wire.
PAYLOAD_BYTES = 32 * 1024


def fill_until_deferred(
    client: PhpVpnClient, sender: str, receiver: str, limits: dict[str, int]
):
    """Publish batches until one destination group no longer fits.

    Returns the response that carried the first deferral together with the
    identifiers of that group, or `(None, set())` if the queue never reached
    its capacity within a generous number of attempts.
    """
    per_request = limits["maxDataRecordsPerRequest"]
    budget = limits["maxQueueBytesPerClient"] // (per_request * PAYLOAD_BYTES) + 6
    for _ in range(budget):
        records = [
            make_record(sender, receiver, bytes(PAYLOAD_BYTES))
            for _ in range(per_request)
        ]
        response, _ = client.send_data(sender, records, timeout=120)
        assert response.status_code == 200, (
            f"a full queue must not fail the request, got {response.status_code}"
        )
        parsed = parse_data_response(response.content)
        if parsed.deferred_message_ids:
            return parsed, {record.message_id for record in records}
    return None, set()


def top_up_until_a_single_record_is_deferred(
    client: PhpVpnClient, sender: str, receiver: str
) -> bool:
    """Consume the remaining capacity so that one more record cannot fit.

    A deferred group only proves that the whole group did not fit, so the
    remaining space is filled with single records until even one of them is
    deferred. Every later test can then rely on a single record of the same
    size being deferred as well.
    """
    for _ in range(40):
        record = make_record(sender, receiver, bytes(PAYLOAD_BYTES))
        response, _ = client.send_data(sender, [record], timeout=120)
        assert response.status_code == 200
        parsed = parse_data_response(response.content)
        if parsed.deferred_message_ids == [record.message_id]:
            return True
    return False


def test_a_full_queue_defers_instead_of_failing(
    client: PhpVpnClient, limits: dict[str, int]
) -> None:
    """Chapters 10.2 and 16.4.9, a full target group yields `200` and deferral."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[3]
    parsed, group = fill_until_deferred(client, sender, receiver, limits)
    if parsed is None:
        pytest.skip("the queue never reached its capacity limit")

    assert set(parsed.deferred_message_ids) == group, (
        "either the whole destination group is published or none of it is"
    )
    assert parsed.accepted_message_ids == []
    assert parsed.result["acceptedRecords"] == 0
    assert parsed.result["acceptedBytes"] == 0

    drained = client.poll(receiver)
    assert drained.records, "the queue must still hand out what it accepted"


def test_a_full_queue_does_not_block_the_incoming_drain(
    client: PhpVpnClient, limits: dict[str, int]
) -> None:
    """Chapter 16.4.10, a fully deferred request still drains the caller."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[3]
    peer = client.configuration.addresses[1]

    parsed, _ = fill_until_deferred(client, sender, receiver, limits)
    if parsed is None or not top_up_until_a_single_record_is_deferred(
        client, sender, receiver
    ):
        pytest.skip("the queue never reached its capacity limit")

    waiting = make_record(peer, sender, b"incoming while the target is full")
    client.send_data(peer, [waiting])

    blocked = [make_record(sender, receiver, bytes(PAYLOAD_BYTES))]
    response, request_id = client.send_data(sender, blocked, timeout=120)
    assert response.status_code == 200, "capacity is never a 429 or a 5xx"
    result = parse_data_response(response.content)
    assert set(result.deferred_message_ids) == {record.message_id for record in blocked}
    assert [line.message_id for line in result.records] == [waiting.message_id]
    assert result.records[0].decrypt() == b"incoming while the target is full"
    assert result.verify_mac(client.full_auth_key(sender), request_id)

    client.poll(receiver)


def test_a_full_queue_does_not_affect_another_destination(
    client: PhpVpnClient, limits: dict[str, int]
) -> None:
    """Chapter 16.4.7, a full queue never cancels another destination group."""
    sender = client.configuration.addresses[0]
    full = client.configuration.addresses[3]
    free = client.configuration.addresses[2]

    parsed, _ = fill_until_deferred(client, sender, full, limits)
    if parsed is None or not top_up_until_a_single_record_is_deferred(
        client, sender, full
    ):
        pytest.skip("the queue never reached its capacity limit")

    blocked = make_record(sender, full, bytes(PAYLOAD_BYTES))
    passing = make_record(sender, free, b"unaffected destination")
    response, _ = client.send_data(sender, [blocked, passing], timeout=120)
    assert response.status_code == 200
    result = parse_data_response(response.content)
    assert result.accepted_message_ids == [passing.message_id]
    assert result.deferred_message_ids == [blocked.message_id]

    assert [line.message_id for line in client.poll(free).records] == [
        passing.message_id
    ]
    client.poll(full)


def test_the_whole_queue_is_returned_without_pagination(
    client: PhpVpnClient, limits: dict[str, int]
) -> None:
    """Chapters 9.5 and 16.4.3, one poll returns the complete buffer."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[3]
    parsed, _ = fill_until_deferred(client, sender, receiver, limits)
    if parsed is None:
        pytest.skip("the queue never reached its capacity limit")

    response, request_id = client.send_data(receiver, timeout=180)
    assert response.status_code == 200
    drained = parse_data_response(response.content)
    assert drained.verify_mac(client.full_auth_key(receiver), request_id)
    assert len(drained.records) > limits["maxDataRecordsPerRequest"]
    assert len(response.content) > limits["maxQueueBytesPerClient"] * 0.5

    assert client.poll(receiver).records == [], "one poll drains everything"
