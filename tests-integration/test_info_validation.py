"""Shared strict extended-info validation, independent of storage implementation."""
import json

import pytest

from phpvpn_client import PhpVpnClient


@pytest.mark.parametrize('field,value,status', [
    ('includeMaskedCommunicationContracts', None, 400),
    ('includeMaskedCommunicationContracts', 0, 400),
    ('includeMaskedCommunicationContracts', 1, 400),
    ('includeMaskedCommunicationContracts', 'true', 400),
    ('includeMaskedCommunicationContracts', [], 400),
    ('maskedCommunicationContracts', None, 400),
    ('maskedCommunicationContracts', {}, 400),
    ('maskedCommunicationContracts', False, 400),
    ('src', '10.99.99.02', 401),
    ('src', '127.0.0.1', 401),
    ('src', None, 400),
    ('requestId', None, 400),
    ('requestId', '00000000-0000-0000-0000-000000000000', 400),
    ('timestamp', '2026-09-25T00:00:00Z', 400),
    ('tag', '_x', 400),
])
def test_extended_info_rejects_invalid_types_and_fields(client: PhpVpnClient, field, value, status):
    body, rid = client.build_info_full(client.configuration.addresses[0], {'includeMaskedCommunicationContracts': False})
    document = json.loads(body)
    document[field] = value
    response, _ = client.info(body=json.dumps(document).encode(), request_id=rid)
    assert response.status_code == status


@pytest.mark.parametrize('duplicate', [b'"src":"10.99.99.2"', b'"\\u0073rc":"10.99.99.2"',
                                       b'"includeMaskedCommunicationContracts":false',
                                       b'"includeMaskedCommunication\\u0043ontracts":false'])
def test_extended_info_duplicate_decoded_keys(client: PhpVpnClient, duplicate):
    body, rid = client.build_info_full(client.configuration.addresses[0], {'includeMaskedCommunicationContracts': False})
    response, _ = client.info(body=body[:-1] + b',' + duplicate + b'}', request_id=rid)
    assert response.status_code == 400


@pytest.mark.parametrize('payload', [b'NaN', b'Infinity', b'-Infinity', b'[[[[[[null]]]]]]', b'"\\ud800"'])
def test_extended_info_non_json_and_depth(client: PhpVpnClient, payload):
    body, rid = client.build_info_full(client.configuration.addresses[0], {'includeMaskedCommunicationContracts': False})
    body = body.replace(b'"includeMaskedCommunicationContracts":false', b'"includeMaskedCommunicationContracts":' + payload)
    assert client.info(body=body, request_id=rid)[0].status_code == 400


def test_extended_info_member_order_and_escapes_do_not_change_signature(client: PhpVpnClient):
    a, b = client.configuration.addresses[:2]
    body, rid = client.build_info_full(a, {'maskedCommunicationContracts': [{'peer': b, 'hash': 'a'*64}],
                                         'includeMaskedCommunicationContracts': True})
    obj = json.loads(body)
    obj['maskedCommunicationContracts'][0] = dict(reversed(list(obj['maskedCommunicationContracts'][0].items())))
    obj = dict(reversed(list(obj.items())))
    body = json.dumps(obj, indent=2).encode().replace(b'"src"', b'"\\u0073rc"')
    response, _ = client.info(body=body, request_id=rid)
    assert response.status_code == 200
    assert client.decrypt_info(response.json(), rid)['serverVersion']


@pytest.mark.parametrize('value', [
    [{'peer': '10.99.99.2', 'hash': 'a'*64}],
    [{'peer': '10.99.99.3', 'hash': 'A'*64}],
    [{'peer': '10.99.99.3', 'hash': 'a'*63}],
    [{'peer': '10.99.99.03', 'hash': 'a'*64}],
    [{'peer': '10.99.99.3', 'hash': 'a'*64, 'extra': ''}],
    [{'peer': '10.99.99.3', 'hash': 'a'*64}, {'peer': '10.99.99.3', 'hash': 'b'*64}],
    [[]], [None], [''],
])
def test_extended_info_rejects_bad_contracts(client: PhpVpnClient, value):
    body, rid = client.build_info_full(client.configuration.addresses[0], {'includeMaskedCommunicationContracts': False})
    document = json.loads(body)
    document['maskedCommunicationContracts'] = value
    response, _ = client.info(body=json.dumps(document).encode(), request_id=rid)
    assert response.status_code == 400


@pytest.mark.parametrize('count,status', [(251, 200), (252, 200), (253, 413)])
def test_contract_count_boundary(client: PhpVpnClient, count, status):
    src = client.configuration.addresses[0]
    peers = [f'10.99.99.{n}' for n in range(2, 255) if f'10.99.99.{n}' != src]
    values = [{'peer': peers[n % len(peers)], 'hash': 'a'*64} for n in range(count)]
    body, rid = client.build_info_full(src, {'maskedCommunicationContracts': values})
    response, _ = client.info(body=body, request_id=rid)
    assert response.status_code == status
    if status == 200:
        client.decrypt_info(response.json(), rid)
