"""Queue time to live, chapters 10.4 and 16.4.13.

The expiry test has to wait longer than `QUEUE_TTL_SECONDS`, which is 60 s by
default. It is marked `slow` and still runs by default. Deselect it with
`-m "not slow"`.
"""

from __future__ import annotations

import time

import pytest

from phpvpn_client import PhpVpnClient, make_record, parse_data_response

pytestmark = pytest.mark.usefixtures("clean_queues")


def test_a_record_survives_well_inside_the_time_to_live(client: PhpVpnClient) -> None:
    """Chapter 10.4, the age of a batch starts at its first publication."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    record = make_record(sender, receiver, b"still fresh")
    client.send_data(sender, [record])

    time.sleep(5)
    delivered = client.poll(receiver)
    assert [line.message_id for line in delivered.records] == [record.message_id]
    assert delivered.records[0].decrypt() == b"still fresh"


@pytest.mark.slow
def test_an_uncollected_record_expires(
    client: PhpVpnClient, limits: dict[str, int], performance_recorder
) -> None:
    """Chapter 10.4, an expired batch is never delivered."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    ttl = limits["queueTtlSeconds"]
    record = make_record(sender, receiver, b"expires unread")

    response, _ = client.send_data(sender, [record])
    parsed = parse_data_response(response.content)
    assert parsed.accepted_message_ids == [record.message_id]

    started = time.monotonic()
    time.sleep(ttl + 5)
    performance_recorder("queue ttl wait", (time.monotonic() - started) * 1000, "s of TTL")

    delivered = client.poll(receiver)
    assert delivered.records == [], "an expired record must not be delivered"

    # A record published after the expiry is delivered normally, which proves
    # the queue itself is still healthy.
    fresh = make_record(sender, receiver, b"after the expiry")
    client.send_data(sender, [fresh])
    assert [line.message_id for line in client.poll(receiver).records] == [
        fresh.message_id
    ]


@pytest.mark.slow
def test_expiry_does_not_disturb_other_queues(
    client: PhpVpnClient, limits: dict[str, int]
) -> None:
    """Chapter 10.4, expiry is per batch and never touches a fresh queue."""
    sender = client.configuration.addresses[0]
    aging, fresh_target = (
        client.configuration.addresses[1],
        client.configuration.addresses[2],
    )
    ttl = limits["queueTtlSeconds"]

    old = make_record(sender, aging, b"will expire")
    client.send_data(sender, [old])

    time.sleep(ttl + 5)

    recent = make_record(sender, fresh_target, b"published later")
    client.send_data(sender, [recent])

    assert client.poll(aging).records == []
    assert [line.message_id for line in client.poll(fresh_target).records] == [
        recent.message_id
    ]
