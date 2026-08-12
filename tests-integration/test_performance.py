"""Latency sanity checks, chapter 11.2.

The numbers are measured and printed in the terminal summary. Only a generous
upper bound is asserted, because a tight threshold would make the suite flaky
on a shared machine.
"""

from __future__ import annotations

import statistics

import pytest

from phpvpn_client import PhpVpnClient, make_record, parse_data_response

#: Generous ceiling. The recommended client request timeout is 15 s.
MAXIMUM_MS = 5000.0

pytestmark = [pytest.mark.perf, pytest.mark.usefixtures("clean_queues")]


def report(recorder, label: str, samples: list[float], detail: str = "") -> None:
    """Record the median, the mean and the maximum of a sample series."""
    recorder(f"{label} median", statistics.median(samples), detail)
    recorder(f"{label} mean", statistics.fmean(samples), detail)
    recorder(f"{label} max", max(samples), detail)
    assert max(samples) < MAXIMUM_MS, f"{label} took {max(samples):.0f} ms"


def test_health_latency(client: PhpVpnClient, performance_recorder) -> None:
    """Chapter 5.2, a `health` request is a short stateless round trip."""
    samples = []
    for _ in range(20):
        response, request_id = client.health()
        assert response.status_code == 200
        assert client.verify_health(response.json(), request_id)
        samples.append(response.elapsed_ms)
    report(performance_recorder, "health", samples, "20 requests")


def test_info_latency(client: PhpVpnClient, performance_recorder) -> None:
    """Chapter 8, `info` encrypts a small plaintext per request."""
    samples = []
    for _ in range(10):
        response, request_id = client.info()
        assert response.status_code == 200
        client.decrypt_info(response.json(), request_id)
        samples.append(response.elapsed_ms)
    report(performance_recorder, "info", samples, "10 requests")


def test_empty_data_poll_latency(client: PhpVpnClient, performance_recorder) -> None:
    """Chapter 11.2, an empty poll answers immediately and never waits."""
    address = client.configuration.addresses[0]
    samples = []
    for _ in range(20):
        response, request_id = client.send_data(address)
        assert response.status_code == 200
        samples.append(response.elapsed_ms)
    report(performance_recorder, "empty data poll", samples, "20 requests")


def test_full_data_request_latency(
    client: PhpVpnClient, limits: dict[str, int], performance_recorder
) -> None:
    """Chapter 11.1, the maximum number of records with sizeable payloads."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    maximum = limits["maxDataRecordsPerRequest"]
    # Base64url expands the payload by four thirds. Sixteen records of this
    # size stay below the one mebibyte body limit of the nginx deployment.
    payload_bytes = 32 * 1024

    send_samples = []
    receive_samples = []
    body_bytes = 0
    for _ in range(5):
        records = [
            make_record(sender, receiver, bytes(payload_bytes)) for _ in range(maximum)
        ]
        body, _ = client.build_data_body(sender, [record.line for record in records])
        body_bytes = len(body)
        response, _ = client.send_data(sender, body=body)
        assert response.status_code == 200
        parsed = parse_data_response(response.content)
        assert parsed.result["acceptedRecords"] == maximum
        send_samples.append(response.elapsed_ms)

        response, request_id = client.send_data(receiver)
        assert response.status_code == 200
        drained = parse_data_response(response.content)
        assert len(drained.records) == maximum
        assert drained.verify_mac(client.full_auth_key(receiver), request_id)
        receive_samples.append(response.elapsed_ms)

    detail = f"{maximum} records, {body_bytes} byte body"
    report(performance_recorder, "full data send", send_samples, detail)
    report(performance_recorder, "full data drain", receive_samples, detail)
