"""Concurrent access to one queue, chapters 10.1, 10.3 and 16.4.

Every thread uses its own HTTP session and its own client instance, because a
`requests.Session` is not designed for concurrent use.
"""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor

import pytest
import requests

from phpvpn_client import (
    Configuration,
    PhpVpnClient,
    ServerClock,
    make_record,
    parse_data_response,
)

pytestmark = pytest.mark.usefixtures("clean_queues")


def worker_client(configuration: Configuration, clock: ServerClock) -> PhpVpnClient:
    """Build an independent client for one worker thread."""
    instance = PhpVpnClient(configuration, session=requests.Session())
    instance.clock = clock
    return instance


def test_parallel_polls_never_deliver_a_record_twice(client: PhpVpnClient) -> None:
    """Chapter 10.3, a batch is taken over atomically by one worker only."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    published: list[str] = []
    for _ in range(6):
        records = [make_record(sender, receiver, b"concurrent") for _ in range(8)]
        response, _ = client.send_data(sender, records)
        parsed = parse_data_response(response.content)
        assert parsed.result["acceptedRecords"] == len(records)
        published.extend(record.message_id for record in records)

    def poll_once() -> list[str]:
        instance = worker_client(client.configuration, client.clock)
        try:
            return [line.message_id for line in instance.poll(receiver).records]
        finally:
            instance.session.close()

    with ThreadPoolExecutor(max_workers=8) as pool:
        results = list(pool.map(lambda _: poll_once(), range(8)))

    delivered = [message_id for result in results for message_id in result]
    assert len(delivered) == len(set(delivered)), "a record was delivered twice"
    assert set(delivered) <= set(published)
    assert set(delivered) == set(published), "no published record may be lost"


def test_parallel_senders_towards_one_destination(client: PhpVpnClient) -> None:
    """Chapter 16.4.2, two senders towards one target never lose a batch."""
    receiver = client.configuration.addresses[0]
    senders = client.configuration.addresses[1:4]

    def send(sender: str) -> list[str]:
        instance = worker_client(client.configuration, client.clock)
        try:
            accepted: list[str] = []
            for _ in range(4):
                records = [make_record(sender, receiver, b"parallel") for _ in range(4)]
                response, _ = instance.send_data(sender, records)
                assert response.status_code == 200
                parsed = parse_data_response(response.content)
                assert parsed.deferred_message_ids == []
                accepted.extend(parsed.accepted_message_ids)
            return accepted
        finally:
            instance.session.close()

    with ThreadPoolExecutor(max_workers=len(senders)) as pool:
        accepted = [item for result in pool.map(send, senders) for item in result]

    assert len(accepted) == len(set(accepted))

    delivered: list[str] = []
    for _ in range(6):
        batch = client.poll(receiver)
        delivered.extend(line.message_id for line in batch.records)
        if len(delivered) >= len(accepted):
            break

    assert len(delivered) == len(set(delivered)), "a record was delivered twice"
    assert set(delivered) == set(accepted), (
        "every accepted identifier must be delivered exactly once"
    )


def test_concurrent_sending_and_polling_stay_consistent(client: PhpVpnClient) -> None:
    """Chapter 10.3, a batch published during a poll stays for the next poll."""
    receiver = client.configuration.addresses[0]
    sender = client.configuration.addresses[1]
    accepted: list[str] = []
    delivered: list[str] = []

    def sender_task() -> list[str]:
        instance = worker_client(client.configuration, client.clock)
        try:
            ids: list[str] = []
            for _ in range(10):
                record = make_record(sender, receiver, b"interleaved")
                response, _ = instance.send_data(sender, [record])
                parsed = parse_data_response(response.content)
                ids.extend(parsed.accepted_message_ids)
            return ids
        finally:
            instance.session.close()

    def poller_task() -> list[str]:
        instance = worker_client(client.configuration, client.clock)
        try:
            ids: list[str] = []
            for _ in range(20):
                ids.extend(line.message_id for line in instance.poll(receiver).records)
            return ids
        finally:
            instance.session.close()

    with ThreadPoolExecutor(max_workers=4) as pool:
        futures = [pool.submit(sender_task) for _ in range(2)]
        futures += [pool.submit(poller_task) for _ in range(2)]
        results = [future.result() for future in futures]

    accepted = results[0] + results[1]
    delivered = results[2] + results[3]
    for _ in range(4):
        delivered.extend(line.message_id for line in client.poll(receiver).records)

    assert len(delivered) == len(set(delivered)), "a record was delivered twice"
    assert set(delivered) == set(accepted), "no accepted record may be lost"


def test_concurrent_health_requests_are_independent(client: PhpVpnClient) -> None:
    """Chapter 5.2, `auth-none` keeps no state between concurrent requests."""

    def one() -> bool:
        instance = worker_client(client.configuration, client.clock)
        try:
            response, request_id = instance.health()
            return response.status_code == 200 and instance.verify_health(
                response.json(), request_id
            )
        finally:
            instance.session.close()

    with ThreadPoolExecutor(max_workers=8) as pool:
        assert all(pool.map(lambda _: one(), range(16)))
