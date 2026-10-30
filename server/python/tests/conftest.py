"""Unit/fault fixtures using the existing independent wire client."""
from __future__ import annotations

import asyncio
from pathlib import Path
import sys

import pytest

SERVER = Path(__file__).resolve().parents[1]
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(SERVER))
sys.path.insert(0, str(ROOT / 'tests-integration'))
sys.path.insert(0, str(ROOT / 'tools'))

import phpvpn as server
from phpvpn_client import Configuration, PhpVpnClient, Response, parse_data_response, format_timestamp


class FakeClock(server.Clock):
    def __init__(self):
        self.ms = 1_790_330_000_000
        self.mono = 100.0

    def now_ms(self):
        return self.ms

    def monotonic(self):
        return self.mono


async def exchange(app, action=None, body=b'', method='POST', headers=None, path='/phpvpn.php'):
    sent = []
    messages = [{'type': 'http.request', 'body': body, 'more_body': False}]

    async def receive():
        return messages.pop(0) if messages else {'type': 'http.disconnect'}

    async def send(message):
        sent.append(message)

    content_type = 'application/x-ndjson' if action == 'data' else 'application/json'
    scope = {'type': 'http', 'method': method, 'path': path,
             'query_string': b'' if action is None else ('action=' + action).encode(),
             'headers': [(k.encode(), v.encode()) for k, v in (headers or {'content-type': content_type}).items()]}
    await app(scope, receive, send)
    first = sent[0]
    return Response(method, path, first['status'], {k.decode(): v.decode() for k, v in first['headers']},
                    b''.join(m.get('body', b'') for m in sent[1:]), 0)


@pytest.fixture
def clock():
    return FakeClock()


@pytest.fixture(params=['disk', 'memory'])
def runtime(request, tmp_path, clock):
    cfg = server.Config(STORAGE_DRIVER=request.param, BUFFER_DIRECTORY=str(tmp_path / 'buffer'))
    app = server.Application(cfg, clock=clock)
    client = PhpVpnClient(Configuration('http://unused/phpvpn.php', cfg.secret, cfg.access_keys, cfg.buffer))
    client.clock.timestamp = lambda: format_timestamp(clock.now_ms())
    yield app, client, clock
    app.executor.shutdown(wait=True)


def call(app, action=None, body=b'', **kwargs):
    return asyncio.run(exchange(app, action, body, **kwargs))


def verified_data(response, request_id, key):
    assert response.status_code == 200
    parsed = parse_data_response(response.content)
    assert parsed.verify_mac(key, request_id)
    return parsed
