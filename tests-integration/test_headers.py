"""Mandatory response headers on every response, chapters 4.1 and 13.

Every response the client returns is checked automatically by
`phpvpn_client.assert_protocol_headers`. This module collects one response of
every status the suite can produce and applies the shared helper explicitly, so
the header contract is covered as a first class requirement rather than only as
a side effect of the other tests.
"""

from __future__ import annotations

import pytest

from phpvpn_client import (
    HTML_CONTENT_TYPE,
    JSON_CONTENT_TYPE,
    NDJSON_CONTENT_TYPE,
    REQUIRED_HEADERS,
    PhpVpnClient,
    Response,
    assert_protocol_headers,
    b64u_encode,
    make_record,
)


def collect_responses(client: PhpVpnClient) -> list[tuple[str, Response, str]]:
    """Produce one response per status code and content type of the protocol."""
    address = client.configuration.addresses[0]
    samples: list[tuple[str, Response, str]] = []

    samples.append(("status page", client.request("GET"), HTML_CONTENT_TYPE))
    samples.append(("status page HEAD", client.request("HEAD"), HTML_CONTENT_TYPE))
    samples.append(("health 200", client.health()[0], JSON_CONTENT_TYPE))
    samples.append(
        ("health 400", client.health(body=b"{}")[0], JSON_CONTENT_TYPE)
    )
    samples.append(
        (
            "health 415",
            client.health(content_type="text/plain")[0],
            JSON_CONTENT_TYPE,
        )
    )
    samples.append(
        ("405 on the script", client.request("POST", body=b""), JSON_CONTENT_TYPE)
    )
    samples.append(
        ("405 on health", client.request("GET", action="health"), JSON_CONTENT_TYPE)
    )
    samples.append(
        ("404 unknown action", client.request("POST", action="nope", body=b""),
         JSON_CONTENT_TYPE)
    )
    samples.append(("info 200", client.info()[0], JSON_CONTENT_TYPE))
    samples.append(
        ("info 401", client.info(tag=b64u_encode(bytes(32)))[0], JSON_CONTENT_TYPE)
    )
    samples.append(
        (
            "info 400",
            client.info(timestamp="2026-13-45T99:99:99.999Z")[0],
            JSON_CONTENT_TYPE,
        )
    )
    samples.append(("data 200", client.send_data(address)[0], NDJSON_CONTENT_TYPE))
    samples.append(
        (
            "data 401",
            client.send_data(address, access_key=bytes(32))[0],
            JSON_CONTENT_TYPE,
        )
    )
    samples.append(
        (
            "data 400",
            client.send_data(address, record_lines=[b"{}\n"])[0],
            JSON_CONTENT_TYPE,
        )
    )

    record = make_record(address, address, b"x" * 32)
    too_many = [record.line] + [
        make_record(address, address, b"y" * 16).line for _ in range(16)
    ]
    samples.append(
        (
            "data 413",
            client.send_data(address, record_lines=too_many)[0],
            JSON_CONTENT_TYPE,
        )
    )
    return samples


def test_every_status_carries_the_mandatory_headers(client: PhpVpnClient) -> None:
    """Chapter 4.1, the three cache and sniffing headers are unconditional."""
    for label, response, content_type in collect_responses(client):
        assert_protocol_headers(response, content_type)
        for name, expected in REQUIRED_HEADERS.items():
            assert response.headers.get(name) == expected, f"{label} lacks {name}"


def test_error_responses_use_the_generic_error_object(client: PhpVpnClient) -> None:
    """Chapter 13, an error body is a small generic JSON object."""
    for label, response, _ in collect_responses(client):
        if response.status_code < 400 or response.method == "HEAD":
            continue
        document = response.json()
        assert set(document) == {"type", "code"}, label
        assert document["type"] == "error", label
        assert document["code"] in {
            "bad_request",
            "unauthorized",
            "not_found",
            "method_not_allowed",
            "payload_too_large",
            "unsupported_media_type",
            "queue_busy",
            "queue_unavailable",
            "server_misconfigured",
            "internal_error",
        }, label


def test_no_response_leaks_implementation_details(client: PhpVpnClient) -> None:
    """Chapter 13, no path, secret or PHP detail may appear in a response."""
    secret = client.configuration.server_secret
    for label, response, _ in collect_responses(client):
        assert "X-Powered-By" not in response.headers, label
        assert secret not in response.content, label
        for access_key in client.configuration.access_keys.values():
            assert access_key.hex().encode() not in response.content, label
        lowered = response.content.lower()
        if label == "status page":
            # The protocol explicitly requires this exact public default-secret
            # warning. Exempt only that sentence, never arbitrary PHP paths.
            lowered = lowered.replace(
                b"server is not secure. please modify server_secret in the phpvpn.php",
                b"",
            )
        markers = (b".php", b"php/", b"php-cgi", b"d:/", b"d:\\", b"c:\\",
                   b"exception", b"stack trace", b" on line ")
        for marker in markers:
            assert marker not in lowered, f"{label} leaked {marker!r}"


def test_405_carries_allow_and_503_carries_retry_after(client: PhpVpnClient) -> None:
    """Chapters 5 and 11.2, `Allow` on 405 and `Retry-After` on 503."""
    for label, response, _ in collect_responses(client):
        if response.status_code == 405:
            assert response.headers.get("Allow"), label
        if response.status_code == 503:
            assert response.headers.get("Retry-After"), label


def test_a_service_unavailable_response_carries_retry_after(
    client: PhpVpnClient,
) -> None:
    """Chapter 11.2, a `503` tells the client how long to back off.

    A `503 queue_busy` requires the storage lock wait of `LOCK_TIMEOUT_MS` to
    expire, chapter 13. The test provokes heavy contention on one queue and
    skips when the deployment never reaches that state, because the protocol
    does not require a `503` to be reachable.
    """
    from concurrent.futures import ThreadPoolExecutor

    import requests as requests_module

    address = client.configuration.addresses[0]

    def hammer(_: int) -> Response:
        worker = PhpVpnClient(client.configuration, session=requests_module.Session())
        worker.clock = client.clock
        records = [make_record(address, address, bytes(4096)) for _ in range(8)]
        try:
            response, _ = worker.send_data(address, records)
            return response
        finally:
            worker.session.close()

    with ThreadPoolExecutor(max_workers=16) as pool:
        responses = list(pool.map(hammer, range(48)))

    unavailable = [item for item in responses if item.status_code == 503]
    if not unavailable:
        pytest.skip("the deployment never returned 503 under contention")
    for response in unavailable:
        assert response.headers.get("Retry-After"), "a 503 must carry Retry-After"
        assert response.json()["code"] in {
            "queue_busy",
            "queue_unavailable",
            "server_misconfigured",
        }


def test_content_type_matches_the_action(client: PhpVpnClient) -> None:
    """Chapters 5.2, 8.3 and 9.5, the success media type of each action."""
    address = client.configuration.addresses[0]
    assert client.request("GET").content_type == HTML_CONTENT_TYPE
    assert client.health()[0].content_type == JSON_CONTENT_TYPE
    assert client.info()[0].content_type == JSON_CONTENT_TYPE
    assert client.send_data(address)[0].content_type == NDJSON_CONTENT_TYPE
