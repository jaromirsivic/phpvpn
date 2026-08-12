"""Status page, routing and method handling, chapters 5, 5.1 and 16.1."""

from __future__ import annotations

import pytest

from phpvpn_client import (
    HTML_CONTENT_TYPE,
    JSON_CONTENT_TYPE,
    NDJSON_CONTENT_TYPE,
    PhpVpnClient,
    assert_protocol_headers,
    build_auth_none,
    new_uuid4,
)

ACTIONS = ("health", "info", "data")


def test_status_page_reports_the_running_server(client: PhpVpnClient) -> None:
    """Chapter 5.1, `GET` without an action returns the operational page."""
    response = client.request("GET")
    assert response.status_code == 200
    assert_protocol_headers(response, HTML_CONTENT_TYPE)
    assert "<h1>Server is running</h1>" in response.text


def test_status_page_hides_the_configuration(client: PhpVpnClient) -> None:
    """Chapter 5.1, the public page must not disclose the configuration."""
    body = client.request("GET").text.lower()
    for secret in ("accesskey", "server_secret", "buffer", "10.99.99.2"):
        assert secret not in body


def test_status_page_secret_warning_matches_the_configured_secret(
    client: PhpVpnClient,
) -> None:
    """Chapter 5.1, the red warning appears only for a predictable secret."""
    secret = client.configuration.server_secret
    predictable = secret == b"" or set(secret) <= {0x30} or set(secret) <= {0x00}
    body = client.request("GET").text
    warned = "Server is not secure" in body
    assert warned == predictable, (
        "the warning must appear exactly for an empty, all zero byte or all ASCII "
        "zero server secret"
    )


def test_head_on_the_status_page_returns_no_body(client: PhpVpnClient) -> None:
    """Chapter 5, `HEAD` is allowed on the script and returns no body."""
    response = client.request("HEAD")
    assert response.status_code == 200
    assert response.content == b""
    assert_protocol_headers(response, HTML_CONTENT_TYPE)


@pytest.mark.parametrize("method", ["POST", "PUT", "DELETE", "PATCH"])
def test_disallowed_method_on_the_script(client: PhpVpnClient, method: str) -> None:
    """Chapter 5, only `GET` and `HEAD` are allowed without an action."""
    response = client.request(method, body=b"", content_type=JSON_CONTENT_TYPE)
    assert response.status_code == 405
    assert response.headers.get("Allow") == "GET, HEAD"
    if method != "HEAD":
        assert response.json() == {"type": "error", "code": "method_not_allowed"}
    assert_protocol_headers(response, JSON_CONTENT_TYPE)


@pytest.mark.parametrize("action", ACTIONS)
@pytest.mark.parametrize("method", ["GET", "HEAD", "PUT", "DELETE"])
def test_disallowed_method_on_an_action(
    client: PhpVpnClient, action: str, method: str
) -> None:
    """Chapter 16.1.5, every action accepts `POST` only."""
    response = client.request(method, action=action)
    assert response.status_code == 405
    assert response.headers.get("Allow") == "POST"
    if method == "HEAD":
        assert response.content == b""
    else:
        assert response.json() == {"type": "error", "code": "method_not_allowed"}
    assert_protocol_headers(response, JSON_CONTENT_TYPE)


@pytest.mark.parametrize(
    "action", ["bogus", "Health", "HEALTH", "data ", "info2", "ping", "dat", "datax"]
)
@pytest.mark.parametrize("method", ["GET", "POST"])
def test_unknown_action_returns_not_found(
    client: PhpVpnClient, action: str, method: str
) -> None:
    """Chapter 5, an unknown action returns `404 not_found`."""
    response = client.request(
        method,
        raw_query=f"action={action}",
        body=b"{}" if method == "POST" else None,
        content_type=JSON_CONTENT_TYPE if method == "POST" else None,
    )
    assert response.status_code == 404
    assert response.json() == {"type": "error", "code": "not_found"}
    assert_protocol_headers(response, JSON_CONTENT_TYPE)


@pytest.mark.parametrize(
    "content_type",
    [
        "text/plain",
        "application/x-www-form-urlencoded",
        "multipart/form-data; boundary=x",
        "application/x-ndjson; charset=utf-8",
        "application/json; charset=utf-16",
        "",
    ],
)
def test_health_rejects_a_wrong_content_type(
    client: PhpVpnClient, content_type: str
) -> None:
    """Chapter 13, a wrong Content-Type returns `415`."""
    response, _ = client.health(content_type=content_type)
    assert response.status_code == 415
    assert response.json() == {"type": "error", "code": "unsupported_media_type"}


def test_empty_action_value_is_treated_as_no_action(client: PhpVpnClient) -> None:
    """Chapter 5 does not define an empty `action` value.

    The deployment treats `?action=` exactly like a request without an action,
    which is the only self consistent reading. The test pins that behaviour
    down rather than asserting an undocumented `404`.
    """
    page = client.request("GET", raw_query="action=")
    assert page.status_code == 200
    assert "<h1>Server is running</h1>" in page.text

    posted = client.request("POST", raw_query="action=", body=b"")
    assert posted.status_code == 405
    assert posted.headers.get("Allow") == "GET, HEAD"


@pytest.mark.parametrize(
    "content_type",
    ["text/plain", "application/json; charset=utf-8", "application/json"],
)
def test_data_rejects_a_wrong_content_type(
    client: PhpVpnClient, content_type: str
) -> None:
    """Chapter 9.1, the `data` action requires the NDJSON media type."""
    address = client.configuration.addresses[0]
    body, _ = client.build_data_body(address)
    response = client.request(
        "POST", action="data", body=body, content_type=content_type
    )
    assert response.status_code == 415
    assert response.json() == {"type": "error", "code": "unsupported_media_type"}


def test_info_rejects_a_wrong_content_type(client: PhpVpnClient) -> None:
    """Chapter 8.1, the `info` action requires the JSON media type."""
    body, _, _ = client.build_auth_semi()
    response = client.request(
        "POST", action="info", body=body, content_type=NDJSON_CONTENT_TYPE
    )
    assert response.status_code == 415
    assert response.json() == {"type": "error", "code": "unsupported_media_type"}


def test_forged_forwarding_headers_do_not_change_the_result(
    client: PhpVpnClient,
) -> None:
    """Chapter 16.1.2, proxy headers never influence the application result."""
    request_id = new_uuid4()
    plain, _ = client.health(request_id=request_id)
    forged = client.request(
        "POST",
        action="health",
        body=build_auth_none(request_id),
        content_type=JSON_CONTENT_TYPE,
        headers={
            "X-Forwarded-For": "10.99.99.254",
            "X-Forwarded-Proto": "https",
            "X-Forwarded-Host": "attacker.example",
            "Forwarded": "for=10.99.99.254;proto=https",
            "X-Real-IP": "10.99.99.254",
        },
    )
    assert forged.status_code == plain.status_code == 200
    first = plain.json()
    second = forged.json()
    assert first["requestId"] == second["requestId"] == request_id
    assert first["serverVersion"] == second["serverVersion"]
    assert client.verify_health(second, request_id)


def test_forged_forwarding_headers_do_not_change_data_authentication(
    client: PhpVpnClient, clean_queues: None
) -> None:
    """Chapter 4.1, authentication never derives from transport metadata."""
    address = client.configuration.addresses[0]
    body, request_id = client.build_data_body(address)
    response = client.request(
        "POST",
        action="data",
        body=body,
        content_type=NDJSON_CONTENT_TYPE,
        headers={"X-Forwarded-For": "127.0.0.2", "X-Forwarded-Proto": "https"},
    )
    assert response.status_code == 200
    assert_protocol_headers(response, NDJSON_CONTENT_TYPE)
