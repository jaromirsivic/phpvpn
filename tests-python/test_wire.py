from __future__ import annotations

import json
from pathlib import Path
import re
import subprocess

import pytest

import phpvpn as s
from conftest import call, verified_data
from phpvpn_client import (build_auth_none, derive_full_auth_key, derive_health_key,
                          derive_semi_key, make_record, new_uuid4)


@pytest.mark.parametrize('secret', ['', '0', '0000', '\0', '\0' * 32, 'Příliš žluťoučký 🐍', 'strong-test-value'])
def test_secret_health_info_and_data(secret, tmp_path, clock):
    cfg = s.Config(STORAGE_DRIVER='memory', SERVER_SECRET=secret, BUFFER_DIRECTORY=str(tmp_path))
    app = s.Application(cfg, clock=clock, random_bytes=lambda n: bytes(range(n)))
    from phpvpn_client import Configuration, PhpVpnClient
    client = PhpVpnClient(Configuration('http://unused', cfg.secret, cfg.access_keys, tmp_path))
    rid = new_uuid4()
    try:
        response = call(app, 'health', build_auth_none(rid))
        assert response.status_code == 200
        assert client.verify_health(response.json(), rid)
        body, _, rid = client.build_auth_semi(timestamp=s.timestamp(clock.ms))
        response = call(app, 'info', body)
        assert client.decrypt_info(response.json(), rid)['clients'] == []
        body, rid = client.build_data_body('10.99.99.2', timestamp=s.timestamp(clock.ms))
        response = call(app, 'data', body)
        assert verified_data(response, rid, client.full_auth_key('10.99.99.2')).result['acceptedRecords'] == 0
        page = call(app, method='GET').content
        assert (b'<aside>' in page) == s.insecure_secret(secret)
    finally:
        app.executor.shutdown()


def test_key_vectors():
    assert s.hkdf(bytes.fromhex('0b'*22), bytes.fromhex('000102030405060708090a0b0c'),
                  bytes.fromhex('f0f1f2f3f4f5f6f7f8f9')).hex() == '3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf'
    secret, access, src, rid = b'test secret', bytes(range(32)), '10.99.99.2', new_uuid4()
    assert s.semi_key(secret) == derive_semi_key(secret)
    assert s.full_key(secret, access, src) == derive_full_auth_key(secret, access, src)
    assert s.health_key(secret, rid) == derive_health_key(secret, rid)
    assert len({s.semi_key(secret), s.full_key(secret, access, src), s.health_key(secret, rid)}) == 3


@pytest.mark.parametrize('offset,status', [(-60001, 401), (-60000, 200), (0, 200), (60000, 200), (60001, 401)])
@pytest.mark.parametrize('action', ['data', 'info', 'info-full'])
def test_exact_skew(runtime, offset, status, action):
    app, client, clock = runtime
    stamp = s.timestamp(clock.ms + offset)
    if action == 'data':
        body, _ = client.build_data_body('10.99.99.2', timestamp=stamp)
    elif action == 'info':
        body, _, _ = client.build_auth_semi(timestamp=stamp)
    else:
        body, _ = client.build_info_full('10.99.99.2', {'includeMaskedCommunicationContracts': False}, timestamp=stamp)
    assert call(app, 'info' if action == 'info-full' else action, body).status_code == status


@pytest.mark.parametrize('body', [b'\xef\xbb\xbf{}', b'{"x":NaN}', b'{"x":Infinity}',
                                  b'{"x":1}', b'{"x":-1}', b'{"x":1.1}',
                                  b'{"x":1,"\\u0078":2}', b'{"x":"\\ud800"}',
                                  b'{"x":' + b'['*2000 + b']'*2000 + b'}'])
def test_bounded_info_json(body):
    assert s.info_object(body) is None


@pytest.mark.parametrize('text', ['_x', '__9', 'A', 'Zg=', 'Zg\n', 'é', '+w', '/w'])
def test_noncanonical_base64(text):
    with pytest.raises(s.ProtocolError) as error:
        s.b64decode(text)
    assert error.value.status == 400


@pytest.mark.parametrize('setting,value', [('STORAGE_DRIVER', 'other'), ('STORAGE_DRIVER', True),
                                        ('MAX_AUTH_RECORD_BYTES', True), ('MAX_QUEUE_BYTES_PER_CLIENT', 0),
                                        ('SERVER_SECRET', None), ('ACCESS_KEYS', []),
                                        ('STATUS_PAGE_ENABLED', 1), ('VPN_HOST_MAX', 255),
                                        ('VPN_ADDRESS_PREFIX', '010.99.99.'), ('LISTEN_PORT', 65536)])
def test_invalid_config(setting, value):
    with pytest.raises(s.ConfigError):
        s.Config(**{setting: value})


def test_php_configuration_parity_and_path(tmp_path, monkeypatch):
    php = (Path(s.__file__).parent / 'phpvpn.php').read_text(encoding='utf-8')
    expected = set(re.findall(r'^const ([A-Z_]+) = ', php, re.M))
    assert expected == set(s._CONFIG_NAMES) - {'LISTEN_HOST', 'LISTEN_PORT', 'SERVER_PATH'}
    assert len(s.ACCESS_KEYS) == 14
    for address, value in s.ACCESS_KEYS.items():
        assert f"'{address}' => '{value}'" in php
    monkeypatch.chdir(tmp_path)
    cfg = s.Config()
    assert cfg.buffer == Path(s.__file__).parent / 'phpvpn_data'
    assert not list(tmp_path.iterdir())


@pytest.mark.parametrize('enabled', [True, False])
def test_routing_precedes_bad_configuration(enabled, monkeypatch):
    monkeypatch.setattr(s, 'STORAGE_DRIVER', 'bad')
    monkeypatch.setattr(s, 'STATUS_PAGE_ENABLED', enabled)
    app = s.Application()
    try:
        assert call(app, method='GET').status_code == (200 if enabled else 404)
        assert call(app, method='HEAD').content == b''
        assert call(app, 'missing', method='HEAD').status_code == 404
        assert call(app, 'health', method='GET').status_code == 405
        assert call(app, 'health', b'{}').json() == {'type': 'error', 'code': 'server_misconfigured'}
        assert call(app, 'health', b'{}').headers['Retry-After'] == '1'
    finally:
        app.executor.shutdown()


def test_pretty_exact_boundaries_and_php_bytes(runtime):
    app, _, clock = runtime
    for address in app.cfg.addresses:
        app.storage.publish_contracts(address, [{'peer': '10.99.99.2', 'hash': 'a'*64}] if address != '10.99.99.2' else [], clock.ms)
    for requester in (None, '10.99.99.2'):
        app.cfg.MAX_INFO_PLAINTEXT_BYTES = 262144
        full = app.build_info(requester)
        document = json.loads(full)
        assert b'"maskedCommunicationContracts": []' in full if requester else b'\n    "clients": [' in full
        app.cfg.MAX_INFO_PLAINTEXT_BYTES = len(full)
        assert app.build_info(requester) == full
        app.cfg.MAX_INFO_PLAINTEXT_BYTES = len(full) - 1
        trimmed = json.loads(app.build_info(requester))
        assert [c['address'] for c in trimmed['clients']] == app.cfg.addresses[:-1]
        # Independent PHP encoder is the authoritative pretty representation.
        php = Path('D:/nginx/php8.5.9/php.exe')
        if php.exists():
            result = subprocess.run([str(php), '-r', 'echo json_encode(json_decode(stream_get_contents(STDIN)), JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);'],
                                    input=json.dumps(document).encode(), capture_output=True, check=True)
            assert full == result.stdout
        app.cfg.MAX_INFO_PLAINTEXT_BYTES = 10
        with pytest.raises(s.ProtocolError) as error:
            app.build_info(requester)
        assert error.value.code == 'server_misconfigured'


def test_data_bytes_and_auth_precedence(runtime):
    app, client, _ = runtime
    record = make_record('10.99.99.2', '10.99.99.3', b'original bytes')
    body, rid = client.build_data_body('10.99.99.2', [record.line])
    response = call(app, 'data', body)
    parsed = verified_data(response, rid, client.full_auth_key('10.99.99.2'))
    assert parsed.result['acceptedBytes'] == len(record.line)
    bad = body.replace(b'"dst":"', b'"dst": "')
    assert call(app, 'data', bad).status_code == 401  # MAC before record syntax.
    poll, rid = client.build_data_body('10.99.99.3')
    response = call(app, 'data', poll)
    assert response.content.startswith(record.line)
    verified_data(response, rid, client.full_auth_key('10.99.99.3'))


@pytest.mark.parametrize('action', ['health', 'info', 'data'])
@pytest.mark.parametrize('delta', [-1, 0, 1])
def test_body_limits(runtime, action, delta):
    app, client, _ = runtime
    if action == 'health':
        body = build_auth_none(new_uuid4())
        limit = app.cfg.MAX_AUTH_RECORD_BYTES
    elif action == 'info':
        body, _ = client.build_info_full('10.99.99.2', {'includeMaskedCommunicationContracts': False})
        limit = app.cfg.MAX_INFO_REQUEST_BODY_BYTES
    else:
        body, _ = client.build_data_body('10.99.99.2')
        # Auth line includes LF and retains the exact MAC over the empty rest.
        limit = app.cfg.MAX_AUTH_RECORD_BYTES
        body = body[:-1]
    target = limit + delta - (1 if action == 'data' else 0)
    body = body[:-1] + b' ' * (target - len(body)) + body[-1:] + (b'\n' if action == 'data' else b'')
    response = call(app, action, body)
    assert response.status_code == (413 if delta > 0 else 200)
