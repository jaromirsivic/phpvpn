"""Explicit wizard-default deployment profile, separate from integration keys."""
import pytest
from phpvpn_client import PhpVpnClient


def test_all_fourteen_default_identities_authenticate(client: PhpVpnClient) -> None:
    if client.configuration.server_secret != b"CHANGE_THIS_SERVER_SECRET":
        pytest.skip("requires the explicit wizard-default deployment profile")
    for octet in range(2, 16):
        response = client.poll(f"10.99.99.{octet}")
        assert response.result["acceptedRecords"] == 0
        assert response.result["acceptedBytes"] == 0
