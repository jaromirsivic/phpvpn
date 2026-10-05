"""Shared fixtures for the phpvpn integration suite.

The suite talks to the deployed PHP relay over real HTTP. The deployment is
described by the JSON file at `testServer.deploymentDescription` of the global
configuration `phpvpn.config.json`, which is produced by `tools/deploy-test.ps1`.
"""

from __future__ import annotations

import os
from pathlib import Path
import sys
from typing import Iterator

import pytest
import requests

from phpvpn_client import Configuration, PhpVpnClient, ServerClock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import phpvpn_config  # noqa: E402

#: Deployment description of the private test server (tools/deploy-test.ps1).
DEFAULT_SERVER_CONFIG = phpvpn_config.optional("testServer.deploymentDescription")
if DEFAULT_SERVER_CONFIG:
    DEFAULT_SERVER_CONFIG = str(phpvpn_config.path("testServer.deploymentDescription"))

#: Optional second endpoint used only for bodies larger than the nginx limit.
_large_body_listen = phpvpn_config.optional("testServer.largeBodyListen")
DEFAULT_LARGE_BODY_URL = f"http://{_large_body_listen}/phpvpn.php" if _large_body_listen else None

#: Collected performance samples, printed in the terminal summary.
PERFORMANCE_SAMPLES: list[tuple[str, float, str]] = []


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--server-config", default=DEFAULT_SERVER_CONFIG,
                     help="Path to an isolated deployment fixture JSON.")
    parser.addoption(
        "--large-body-url",
        action="store",
        default=os.environ.get("PHPVPN_LARGE_BODY_URL", DEFAULT_LARGE_BODY_URL),
        help=(
            "Base URL of a second deployment of the same phpvpn.php whose web "
            "server accepts a body larger than MAX_REQUEST_BODY_BYTES."
        ),
    )


@pytest.fixture(scope="session")
def configuration(pytestconfig: pytest.Config) -> Configuration:
    """Load the deployment description given by --server-config."""
    option = pytestconfig.getoption("--server-config")
    assert option, "no deployment description: pass --server-config or set testServer.deploymentDescription"
    path = Path(option)
    assert path.is_file(), f"missing deployment description: {path}"
    return Configuration.load(path, large_body_url=pytestconfig.getoption("--large-body-url"))


@pytest.fixture(scope="session")
def session_client(configuration: Configuration) -> Iterator[PhpVpnClient]:
    """A session wide client whose clock is synchronized with the server."""
    with requests.Session() as session:
        client = PhpVpnClient(configuration, session=session)
        response = client.request("GET")
        assert response.status_code == 200, (
            "the deployment is not reachable, start it with "
            "tools/start-test-server.ps1"
        )
        client.synchronize_clock()
        yield client


@pytest.fixture(scope="session")
def server_clock(session_client: PhpVpnClient) -> ServerClock:
    """The measured offset between the local clock and the server clock."""
    return session_client.clock


@pytest.fixture
def client(configuration: Configuration, server_clock: ServerClock) -> Iterator[PhpVpnClient]:
    """A fresh client per test, sharing the measured server clock."""
    with requests.Session() as session:
        instance = PhpVpnClient(configuration, session=session)
        instance.clock = server_clock
        yield instance


@pytest.fixture(scope="session")
def large_body_client(
    configuration: Configuration, server_clock: ServerClock
) -> Iterator[PhpVpnClient | None]:
    """A client for the endpoint that allows bodies above the nginx limit.

    Yields `None` when that endpoint is not reachable, so the affected tests
    can skip instead of failing.
    """
    url = configuration.large_body_url
    if not url:
        yield None
        return
    with requests.Session() as session:
        instance = PhpVpnClient(configuration, session=session, base_url=url)
        instance.clock = server_clock
        try:
            probe = instance.request("GET", timeout=5)
        except requests.RequestException:
            yield None
            return
        if probe.status_code != 200:
            yield None
            return
        yield instance


@pytest.fixture(scope="session")
def skew_calibration(session_client: PhpVpnClient) -> dict[str, tuple[int, int]]:
    """Measure the accepted timestamp window edges of both authenticated actions.

    Returns `{action: (past_edge_ms, future_edge_ms)}`. For an intended
    deviation `s` the server sees `s - d`, where `d` is the residual delay
    between the estimated arrival of the request and the moment the server
    evaluates its timestamp. The past edge is therefore `W - d` and the future
    edge is `W + d`, so their half sum is the window `W` and their half
    difference is `d`. Neither value assumes the size of the window, which
    keeps the later assertion on `W` free of circular reasoning.
    """
    from phpvpn_client import format_timestamp

    def accepted(action: str, skew_ms: int) -> bool:
        timestamp = format_timestamp(session_client.clock.arrival_ms() + skew_ms)
        if action == "info":
            response, _ = session_client.info(timestamp=timestamp)
        else:
            address = session_client.configuration.addresses[-1]
            response, _ = session_client.send_data(address, timestamp=timestamp)
        assert response.status_code in (200, 401), response.status_code
        return response.status_code == 200

    def edge(action: str, direction: int) -> int:
        low, high = 0, 300_000
        while high - low > 1:
            middle = (low + high) // 2
            if accepted(action, direction * middle):
                low = middle
            else:
                high = middle
        return low

    calibration: dict[str, tuple[int, int]] = {}
    for action in ("info", "data"):
        calibration[action] = (edge(action, -1), edge(action, 1))
    residuals = [(future - past) / 2 for past, future in calibration.values()]
    session_client.clock.bias_ms = int(sum(residuals) / len(residuals))
    return calibration


@pytest.fixture
def limits(session_client: PhpVpnClient) -> dict[str, int]:
    """The active limits published by the `info` action, chapter 8.2."""
    plaintext, _ = session_client.info_plaintext()
    return plaintext["limits"]


@pytest.fixture
def clean_queues(client: PhpVpnClient) -> Iterator[None]:
    """Drain every configured client queue before and after a test.

    Draining uses ordinary authenticated polls rather than deleting files, so
    the suite never touches the buffer directory of the deployment.
    """
    for address in client.configuration.addresses:
        client.drain(address)
    yield
    for address in client.configuration.addresses:
        client.drain(address)


@pytest.fixture
def performance_recorder():
    """Record a labelled latency sample for the terminal summary."""

    def record(label: str, milliseconds: float, detail: str = "") -> None:
        PERFORMANCE_SAMPLES.append((label, milliseconds, detail))

    return record


def pytest_terminal_summary(terminalreporter, exitstatus, config) -> None:
    """Print the measured latencies at the end of the run."""
    if not PERFORMANCE_SAMPLES:
        return
    terminalreporter.write_sep("=", "phpvpn performance samples")
    for label, milliseconds, detail in PERFORMANCE_SAMPLES:
        suffix = f"  {detail}" if detail else ""
        terminalreporter.write_line(f"{label:<44} {milliseconds:9.1f} ms{suffix}")
