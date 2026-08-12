"""Authentication timestamp window, chapters 8.1, 9.2, 11.1 and 16.3.7.

The server compares the request timestamp with its own clock and accepts an
absolute deviation of at most `AUTH_TIMESTAMP_SKEW_SECONDS`, 60 s by default.
The suite measures the offset between the local clock and the server clock, so
a request can be aimed at a chosen deviation.

Aiming at exactly the boundary is inherently imprecise over real HTTP, because
the deviation the server sees is the intended one minus the residual delay
between sending the request and evaluating its timestamp. The
`skew_calibration` fixture measures both accepted edges without assuming the
size of the window, which yields the window itself as the half sum of the
edges and the residual delay as their half difference. The residual delay is
then compensated on the shared clock.

What remains is a jitter of a few milliseconds that also drifts slowly during
a run, so a single request can not prove the difference between an inclusive
and an exclusive comparison at the exact millisecond. This module therefore
states the boundary requirement in two complementary ways: the bisected window
must equal 60.000 s within 100 ms, and a deviation of 250 ms inside the
boundary must be accepted while 250 ms outside it must be rejected. Asserting
exactly 60.000 s and 60.001 s per request is left out on purpose, because over
real HTTP such a test measures the network rather than the server.
"""

from __future__ import annotations

import pytest

from phpvpn_client import PhpVpnClient, format_timestamp

SKEW_MS = 60_000

#: Distance from the boundary used by the accept and reject probes. It has to
#: absorb the drift of the residual delay during the run, which was observed
#: between 0 ms and 15 ms on the reference deployment, with a comfortable
#: margin. It is still three orders of magnitude below a one second error.
PROBE_MARGIN_MS = 250

#: Tolerance of the bisected window measurement itself.
MEASUREMENT_TOLERANCE_MS = 100


def probe(client: PhpVpnClient, action: str, skew_ms: int) -> int:
    """Send one authenticated request whose timestamp is `skew_ms` off."""
    timestamp = format_timestamp(client.clock.arrival_ms() + skew_ms)
    if action == "info":
        response, _ = client.info(timestamp=timestamp)
    else:
        address = client.configuration.addresses[-1]
        response, _ = client.send_data(address, timestamp=timestamp)
    return response.status_code


@pytest.mark.parametrize("action", ["info", "data"])
@pytest.mark.parametrize("skew_ms", [-59_000, -1_000, 0, 1_000, 59_000])
def test_timestamp_inside_the_window_is_accepted(
    client: PhpVpnClient, action: str, skew_ms: int, skew_calibration
) -> None:
    """Chapter 11.1, a deviation clearly inside the window is accepted."""
    assert probe(client, action, skew_ms) == 200


@pytest.mark.parametrize("action", ["info", "data"])
@pytest.mark.parametrize("direction", [-1, 1])
def test_timestamp_at_the_boundary_is_accepted(
    client: PhpVpnClient, action: str, direction: int, skew_calibration
) -> None:
    """Chapter 16.3.7, a deviation of 60 s is accepted in both directions."""
    assert probe(client, action, direction * (SKEW_MS - PROBE_MARGIN_MS)) == 200


@pytest.mark.parametrize("action", ["info", "data"])
@pytest.mark.parametrize("direction", [-1, 1])
def test_timestamp_beyond_the_boundary_is_rejected(
    client: PhpVpnClient, action: str, direction: int, skew_calibration
) -> None:
    """Chapter 16.3.7, a deviation above 60 s returns the generic `401`."""
    assert probe(client, action, direction * (SKEW_MS + PROBE_MARGIN_MS)) == 401


@pytest.mark.parametrize("action", ["info", "data"])
@pytest.mark.parametrize("skew_ms", [-61_000, -300_000, 61_000, 300_000])
def test_timestamp_far_outside_the_window_is_rejected(
    client: PhpVpnClient, action: str, skew_ms: int, skew_calibration
) -> None:
    """Chapter 13, a stale or future timestamp returns the generic `401`."""
    assert probe(client, action, skew_ms) == 401


@pytest.mark.parametrize("action", ["info", "data"])
def test_effective_skew_window_is_sixty_seconds(
    action: str, skew_calibration, performance_recorder
) -> None:
    """Chapter 11.1, the measured window is 60.000 s in both directions."""
    past_edge, future_edge = skew_calibration[action]
    window = (past_edge + future_edge) / 2
    residual = (future_edge - past_edge) / 2
    performance_recorder(f"{action} skew window measured", window, "ms")
    performance_recorder(f"{action} arrival estimate residual", residual, "ms")
    assert abs(window - SKEW_MS) <= MEASUREMENT_TOLERANCE_MS, (
        f"the accepted window is {window} ms, expected 60000 ms "
        f"(edges {past_edge} and {future_edge})"
    )


@pytest.mark.parametrize("action", ["info", "data"])
def test_a_replayed_request_inside_the_window_is_accepted_again(
    client: PhpVpnClient, action: str
) -> None:
    """Chapter 16.6.1, an identical request inside the window is reprocessed."""
    if action == "info":
        body, _, _ = client.build_auth_semi()
        first, _ = client.info(body=body)
        second, _ = client.info(body=body)
    else:
        address = client.configuration.addresses[-1]
        body, _ = client.build_data_body(address)
        first, _ = client.send_data(address, body=body)
        second, _ = client.send_data(address, body=body)
    assert first.status_code == 200
    assert second.status_code == 200, "a replay inside the window is tolerated"
