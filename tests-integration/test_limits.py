"""Syntactic limits, chapters 11.1, 16.2.3, 16.2.4 and 16.2.5.

The deployment behind nginx uses the `client_max_body_size` of
`testServer.nginxBodyLimitBytes` in `phpvpn.config.json` (one mebibyte), which
is far below `MAX_REQUEST_BODY_BYTES`. Bodies above that size are therefore
rejected by nginx before PHP ever runs. The suite asserts the
status code in that case and exercises the real application limit against the
second endpoint of the same `phpvpn.php`, the PHP built in server started by
`tools/start-test-server.ps1`, which allows an eight mebibyte body.
"""

from __future__ import annotations

import phpvpn_config  # tools/ is on sys.path through conftest.py
import pytest

from phpvpn_client import (
    DEFAULT_MAX_AUTH_RECORD_BYTES,
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

pytestmark = pytest.mark.usefixtures("clean_queues")

NGINX_BODY_LIMIT = int(phpvpn_config.optional("testServer.nginxBodyLimitBytes", 1024 * 1024))


def ciphertext_length_for(encoded_length: int) -> int:
    """Return the plaintext size whose base64url encoding has that length."""
    remainder = encoded_length % 4
    assert remainder != 1, "a base64url length of 1 modulo 4 is impossible"
    if remainder == 0:
        return encoded_length // 4 * 3
    if remainder == 2:
        return (encoded_length - 2) // 4 * 3 + 1
    return (encoded_length - 3) // 4 * 3 + 2


def body_of_exact_size(client: PhpVpnClient, src: str, dst: str, total: int) -> bytes:
    """Build a correctly signed `data` request body of exactly `total` bytes."""
    auth_length = len(client.build_auth_full_line(src, b"")[0])
    overhead = len(
        build_data_line(new_uuid4(), src, dst, b64u_encode(bytes(12)),
                        b64u_encode(bytes(16)), "")
    )

    remaining = total - auth_length - overhead
    assert remaining > 0, "the requested body is too small for one record"

    extra: list[bytes] = []
    if remaining % 4 == 1:
        # A base64url length of 1 modulo 4 does not exist, so a small second
        # record absorbs the difference.
        filler = make_record(src, dst, b"\x00\x01")
        extra.append(filler.line)
        remaining -= len(filler.line)
        if remaining % 4 == 1:
            filler = make_record(src, dst, b"\x00")
            extra[0] = filler.line
            remaining = total - auth_length - overhead - len(filler.line)
    assert remaining % 4 != 1

    record = make_record(src, dst, bytes(ciphertext_length_for(remaining)))
    lines = [record.line] + extra
    body, _ = client.build_data_body(src, lines)
    assert len(body) == total, f"built {len(body)} bytes, wanted {total}"
    return body


def test_exactly_the_maximum_number_of_records_is_accepted(
    client: PhpVpnClient, limits: dict[str, int]
) -> None:
    """Chapter 16.2.4, exactly the configured maximum passes."""
    sender = client.configuration.addresses[0]
    maximum = limits["maxDataRecordsPerRequest"]
    records = [make_record(sender, sender, b"limit") for _ in range(maximum)]
    response, _ = client.send_data(sender, records)
    assert response.status_code == 200
    parsed = parse_data_response(response.content)
    assert parsed.result["acceptedRecords"] == maximum
    assert len(parsed.records) == maximum


def test_one_record_above_the_maximum_is_rejected(
    client: PhpVpnClient, limits: dict[str, int]
) -> None:
    """Chapter 16.2.4, the record above the maximum returns `413`."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    maximum = limits["maxDataRecordsPerRequest"]
    records = [make_record(sender, receiver, b"limit") for _ in range(maximum + 1)]
    response, _ = client.send_data(sender, records)
    assert response.status_code == 413
    assert response.json() == {"type": "error", "code": "payload_too_large"}
    assert_protocol_headers(response, JSON_CONTENT_TYPE)
    assert client.poll(receiver).records == [], "a 413 must not change the storage"


def test_far_too_many_records_are_rejected(
    client: PhpVpnClient, limits: dict[str, int]
) -> None:
    """Chapter 11.1, the record count limit holds for a large excess too."""
    sender, receiver = client.configuration.addresses[0], client.configuration.addresses[1]
    records = [
        make_record(sender, receiver, b"x")
        for _ in range(limits["maxDataRecordsPerRequest"] * 3)
    ]
    response, _ = client.send_data(sender, records)
    assert response.status_code == 413
    assert client.poll(receiver).records == []


def auth_line_of_length(client: PhpVpnClient, src: str, length: int) -> bytes:
    """Build a first NDJSON line of exactly `length` bytes including the LF."""
    template = [
        ("type", "auth-full"),
        ("src", src),
        ("timestamp", client.clock.timestamp()),
        ("requestId", new_uuid4()),
        ("tag", b64u_encode(bytes(32))),
        ("pad", ""),
    ]
    base = len(compact_json(template)) + 1
    padding = length - base
    assert padding >= 0, "the requested auth line is shorter than the minimum"
    template[-1] = ("pad", "A" * padding)
    line = compact_json(template) + b"\n"
    assert len(line) == length
    return line


def test_auth_line_at_the_limit_is_not_rejected_for_size(client: PhpVpnClient) -> None:
    """Chapter 11.1, a first line of exactly `MAX_AUTH_RECORD_BYTES` fits.

    The line carries an unknown member, so the correct answer is the framing
    error `400` rather than the size error `413`.
    """
    sender = client.configuration.addresses[0]
    line = auth_line_of_length(client, sender, DEFAULT_MAX_AUTH_RECORD_BYTES)
    response, _ = client.send_data(sender, body=line)
    assert response.status_code == 400, response.status_code


def test_auth_line_above_the_limit_is_rejected(client: PhpVpnClient) -> None:
    """Chapter 11.1, a first line above `MAX_AUTH_RECORD_BYTES` returns `413`."""
    sender = client.configuration.addresses[0]
    line = auth_line_of_length(client, sender, DEFAULT_MAX_AUTH_RECORD_BYTES + 1)
    response, _ = client.send_data(sender, body=line)
    assert response.status_code == 413, response.status_code
    assert response.json() == {"type": "error", "code": "payload_too_large"}


def test_very_long_auth_line_is_rejected(client: PhpVpnClient) -> None:
    """Chapter 11.1, a grossly oversized first line is still a `413`."""
    sender = client.configuration.addresses[0]
    line = auth_line_of_length(client, sender, 64 * 1024)
    response, _ = client.send_data(sender, body=line)
    assert response.status_code == 413


def test_body_above_the_web_server_limit_is_rejected(client: PhpVpnClient) -> None:
    """Chapter 11.1, an oversized body never reaches the application logic.

    The nginx deployment enforces its own body limit first, so the status is
    asserted but the response may come from the web server.
    """
    sender = client.configuration.addresses[0]
    body = body_of_exact_size(client, sender, sender, NGINX_BODY_LIMIT + 4096)
    response, _ = client.send_data(sender, body=body)
    assert response.status_code == 413, response.status_code


def test_body_at_the_application_limit_is_accepted(
    large_body_client: PhpVpnClient | None, limits: dict[str, int]
) -> None:
    """Chapter 16.2.5, a body of exactly `MAX_REQUEST_BODY_BYTES` passes."""
    if large_body_client is None:
        pytest.skip("no endpoint that accepts a body of four mebibytes")
    sender = large_body_client.configuration.addresses[0]
    total = limits["maxRequestBodyBytes"]
    body = body_of_exact_size(large_body_client, sender, sender, total)
    response, _ = large_body_client.send_data(sender, body=body, timeout=120)
    assert response.status_code == 200, response.text[:200]
    parsed = parse_data_response(response.content)
    assert parsed.result["acceptedRecords"] >= 1
    assert len(parsed.records) >= 1


def test_body_one_byte_below_the_application_limit_is_accepted(
    large_body_client: PhpVpnClient | None, limits: dict[str, int]
) -> None:
    """Chapter 16.2.5, the `limit-1` edge of the whole body."""
    if large_body_client is None:
        pytest.skip("no endpoint that accepts a body of four mebibytes")
    sender = large_body_client.configuration.addresses[0]
    total = limits["maxRequestBodyBytes"] - 1
    body = body_of_exact_size(large_body_client, sender, sender, total)
    response, _ = large_body_client.send_data(sender, body=body, timeout=120)
    assert response.status_code == 200, response.text[:200]


def test_body_one_byte_above_the_application_limit_is_rejected(
    large_body_client: PhpVpnClient | None, limits: dict[str, int]
) -> None:
    """Chapter 16.2.5, the `limit+1` edge returns `413 payload_too_large`."""
    if large_body_client is None:
        pytest.skip("no endpoint that accepts a body of four mebibytes")
    sender = large_body_client.configuration.addresses[0]
    receiver = large_body_client.configuration.addresses[1]
    total = limits["maxRequestBodyBytes"] + 1
    body = body_of_exact_size(large_body_client, sender, receiver, total)
    response, _ = large_body_client.send_data(sender, body=body, timeout=120)
    assert response.status_code == 413, response.status_code
    assert response.json() == {"type": "error", "code": "payload_too_large"}
    assert large_body_client.poll(receiver).records == []


def test_a_single_record_may_use_the_whole_body(
    large_body_client: PhpVpnClient | None, limits: dict[str, int]
) -> None:
    """Chapter 16.2.3, one data record may fill the remaining body capacity."""
    if large_body_client is None:
        pytest.skip("no endpoint that accepts a body of four mebibytes")
    sender = large_body_client.configuration.addresses[0]
    total = limits["maxRequestBodyBytes"]
    body = body_of_exact_size(large_body_client, sender, sender, total)
    assert body.count(b"\n") == 2, "the body is the auth line plus one record"
    response, _ = large_body_client.send_data(sender, body=body, timeout=120)
    assert response.status_code == 200
    parsed = parse_data_response(response.content)
    assert len(parsed.records) == 1
    assert len(parsed.records[0].raw) > limits["maxRequestBodyBytes"] * 0.9


def test_published_limits_match_the_specification_defaults(
    limits: dict[str, int]
) -> None:
    """Chapter 11.1, the deployment publishes the documented default limits."""
    assert limits["maxDataRecordsPerRequest"] == 16
    assert limits["maxRequestBodyBytes"] == 983040
    assert limits["maxQueueBytesPerClient"] == 16777216
    assert limits["queueTtlSeconds"] == 60
    assert limits["minDataPollIntervalMs"] == 50
    assert limits["maxDataPollIntervalMs"] == 3200
