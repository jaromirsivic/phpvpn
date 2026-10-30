from __future__ import annotations

import asyncio
import threading

import pytest

import phpvpn as s
from conftest import call, exchange
from phpvpn_client import make_record
from test_storage import A, B, C, records


class FaultFS(s.FileSystem):
    def __init__(self):
        self.fail = None

    def check(self, name):
        if self.fail == name:
            raise OSError('injected private path and secret must never be returned')

    def mkdir(self, path):
        self.check('mkdir')
        return super().mkdir(path)

    def open(self, path, mode):
        self.check('read' if 'r' in mode else 'open')
        return super().open(path, mode)

    def write(self, handle, data):
        self.check('write')
        if self.fail == 'zero-write':
            return 0
        if self.fail == 'short-write':
            return super().write(handle, data[:max(1, len(data)//2)])
        return super().write(handle, data)

    def flush(self, handle):
        self.check('flush')
        return super().flush(handle)

    def replace(self, source, target):
        self.check('rename')
        return super().replace(source, target)

    def utime(self, path, seconds):
        self.check('utime')
        return super().utime(path, seconds)


@pytest.fixture
def faulty(tmp_path, clock):
    cfg = s.Config(BUFFER_DIRECTORY=str(tmp_path / 'buffer'))
    fs = FaultFS()
    storage = s.DiskStorage(cfg, clock, fs)
    app = s.Application(cfg, clock=clock, storage=storage)
    from phpvpn_client import Configuration, PhpVpnClient
    client = PhpVpnClient(Configuration('http://unused', cfg.secret, cfg.access_keys, cfg.buffer))
    client.clock.timestamp = lambda: s.timestamp(clock.ms)
    yield app, client, clock, fs
    app.executor.shutdown()


@pytest.mark.parametrize('operation', ['mkdir', 'open', 'write', 'zero-write', 'flush', 'rename'])
def test_failed_batch_never_publishes(faulty, operation):
    app, client, clock, fs = faulty
    fs.fail = operation
    body, _ = client.build_data_body(A, [make_record(A, B, b'payload').line])
    response = call(app, 'data', body)
    assert response.status_code == 503
    assert response.json() == {'type': 'error', 'code': 'queue_unavailable'}
    assert response.headers['Retry-After'] == '1'
    fs.fail = None
    assert app.storage.drain(B, clock.ms) == []
    assert not list(app.cfg.buffer.rglob('*.receiving'))


def test_short_writes_complete(faulty):
    app, _, clock, fs = faulty
    fs.fail = 'short-write'
    group = records(app)
    assert app.storage.enqueue(B, A, group)
    claimed = app.storage.drain(B, clock.ms)
    assert app.storage.read_batch(claimed[0]) == group[0].line + b'\n'
    app.storage.finish(claimed[0])


@pytest.mark.parametrize('operation', ['open', 'write', 'zero-write', 'flush', 'rename', 'utime'])
def test_failed_contract_publication_keeps_old_snapshot(faulty, operation):
    app, client, clock, fs = faulty
    original = [{'peer': B, 'hash': 'a'*64}]
    app.storage.publish_contracts(A, original, clock.ms)
    fs.fail = operation
    body, _ = client.build_info_full(A, {'maskedCommunicationContracts': []})
    response = call(app, 'info', body)
    assert response.status_code == 503
    assert response.json()['code'] == 'queue_unavailable'
    fs.fail = None
    assert app.storage.snapshot(A) == (clock.ms, original)
    assert not list(app.cfg.buffer.rglob('.info-*.tmp'))


def test_marker_failure_is_best_effort(faulty):
    app, client, clock, fs = faulty
    app.storage.is_usable()
    fs.fail = 'utime'
    body, _ = client.build_data_body(A, [make_record(A, B, b'payload').line])
    assert call(app, 'data', body).status_code == 200
    fs.fail = None
    assert len(app.storage.drain(B, clock.ms)) == 1


def test_standard_info_does_not_read_marker_content(faulty):
    app, client, clock, fs = faulty
    app.storage.publish_contracts(A, [{'peer': B, 'hash': 'a'*64}], clock.ms)
    fs.fail = 'read'
    body, _, rid = client.build_auth_semi()
    response = call(app, 'info', body)
    assert client.decrypt_info(response.json(), rid)['clients'] == [{'address': A, 'lastSeen': s.timestamp(clock.ms)}]


def test_unrepresentable_last_seen_is_omitted(runtime, monkeypatch):
    app, _, _ = runtime
    monkeypatch.setattr(app.storage, 'last_seen', lambda _: 10**100)
    import json
    assert json.loads(app.build_info())['clients'] == []


def test_windows_external_open_preserves_old_snapshot(faulty):
    import os
    if os.name != 'nt':
        pytest.skip('Windows deny-delete file sharing semantics only')
    app, _, clock, _ = faulty
    original = [{'peer': B, 'hash': 'a'*64}]
    app.storage.publish_contracts(A, original, clock.ms)
    with (app.storage.directory(A) / 'info.json').open('rb'):
        with pytest.raises(OSError):
            app.storage.publish_contracts(A, [], clock.ms + 1000)
    assert app.storage.snapshot(A) == (clock.ms, original)


def test_temp_ttl_and_incomplete_batches(faulty):
    app, _, clock, fs = faulty
    directory = app.storage.directory(A)
    fs.mkdir(directory)
    temp = directory / ('.info-' + str(s.uuid.uuid4()) + '.tmp')
    temp.write_bytes(b'partial')
    fs.utime(temp, clock.ms // 1000 - app.cfg.INFO_TEMP_TTL_SECONDS)
    app.storage.sweep_queue(A, clock.ms)
    assert temp.exists()  # TTL equality is not older than TTL for info temps.
    app.storage.sweep_queue(A, clock.ms + 1000)
    assert not temp.exists()
    name = s.timestamp(clock.ms + 60000).replace(':', '-') + f'_100_{A}_{s.uuid.uuid4()}.receiving'
    incomplete = directory / name
    incomplete.write_bytes(b'partial')
    assert app.storage.drain(A, clock.ms) == []
    app.storage.sweep_queue(A, clock.ms + 60000)
    assert not incomplete.exists()


def test_later_destination_failure_does_not_rollback(runtime, monkeypatch):
    app, client, clock = runtime
    original = app.storage.enqueue

    def enqueue(dst, *args):
        if dst == C:
            raise OSError('second destination unavailable')
        return original(dst, *args)

    monkeypatch.setattr(app.storage, 'enqueue', enqueue)
    body, _ = client.build_data_body(A, [make_record(A, C, b'c').line, make_record(A, B, b'b').line])
    assert call(app, 'data', body).status_code == 503
    claimed = app.storage.drain(B, clock.ms)
    assert len(claimed) == 1
    app.storage.finish(claimed[0])


def test_send_disconnect_cleans_all_claimed_batches(runtime):
    app, client, clock = runtime
    for _ in range(3):
        app.storage.enqueue(B, A, records(app))
    body, _ = client.build_data_body(B)

    async def run():
        async def receive():
            return {'type': 'http.request', 'body': body, 'more_body': False}

        async def send(message):
            if message['type'] == 'http.response.body':
                raise OSError('client disconnected')

        scope = {'type': 'http', 'method': 'POST', 'path': '/phpvpn.php', 'query_string': b'action=data',
                 'headers': [(b'content-type', b'application/x-ndjson')]}
        with pytest.raises(OSError):
            await app(scope, receive, send)

    asyncio.run(run())
    assert app.storage.drain(B, clock.ms) == []
    if isinstance(app.storage, s.DiskStorage):
        assert not list(app.cfg.buffer.rglob('*.sending'))
    else:
        assert B not in app.storage.queues


def test_disconnect_before_body_never_mutates(runtime):
    app, _, _ = runtime

    async def run():
        async def receive():
            return {'type': 'http.disconnect'}

        async def send(_):
            raise AssertionError('response after disconnect')

        await app({'type': 'http', 'method': 'POST', 'path': '/phpvpn.php', 'query_string': b'action=data',
                   'headers': [(b'content-type', b'application/x-ndjson')]}, receive, send)
    asyncio.run(run())
    assert app.storage.snapshot(A) == (None, [])
    assert not app.cfg.buffer.exists()


def test_cancellation_waits_for_thread_then_cleans_claims(runtime, monkeypatch):
    app, client, clock = runtime
    app.storage.enqueue(B, A, records(app))
    body, _ = client.build_data_body(B)
    entered, release = threading.Event(), threading.Event()
    original = app.handle

    def blocked(*args):
        response = original(*args)
        entered.set()
        assert release.wait(3)
        return response

    monkeypatch.setattr(app, 'handle', blocked)

    async def run():
        task = asyncio.create_task(exchange(app, 'data', body))
        while not entered.is_set():
            await asyncio.sleep(.001)
        task.cancel()
        await asyncio.sleep(.01)
        assert not task.done()
        release.set()
        with pytest.raises(asyncio.CancelledError):
            await task
    asyncio.run(run())
    assert app.storage.drain(B, clock.ms) == []
    if isinstance(app.storage, s.DiskStorage):
        assert not list(app.cfg.buffer.rglob('*.sending'))


def test_read_failure_omits_final_mac(runtime, monkeypatch):
    app, client, clock = runtime
    app.storage.enqueue(B, A, records(app))

    def failed(_):
        raise OSError('storage read failed')

    monkeypatch.setattr(app.storage, 'read_batch', failed)
    body, _ = client.build_data_body(B)
    response = call(app, 'data', body)
    assert response.status_code == 200
    assert b'data-mac' not in response.content
    assert app.storage.drain(B, clock.ms) == []


def test_slow_send_holds_no_queue_lock_and_later_publish_survives(runtime):
    app, client, clock = runtime
    app.storage.enqueue(B, A, records(app))
    later = records(app, payload=b'later')
    body, _ = client.build_data_body(B)

    async def run():
        async def receive():
            return {'type': 'http.request', 'body': body, 'more_body': False}

        async def send(message):
            if message['type'] == 'http.response.start':
                assert app.storage.locks[B].acquire(blocking=False)
                app.storage.locks[B].release()
                assert await app.in_thread(app.storage.enqueue, B, A, later)

        await app({'type': 'http', 'method': 'POST', 'path': '/phpvpn.php', 'query_string': b'action=data',
                   'headers': [(b'content-type', b'application/x-ndjson')]}, receive, send)
    asyncio.run(run())
    next_claim = app.storage.drain(B, clock.ms)
    assert len(next_claim) == 1
    assert next_claim[0].pairs[0][0] == later[0].message_id
    app.storage.finish(next_claim[0])


@pytest.mark.parametrize('corruption', ['truncated', 'wrong-version', 'wrong-bytes', 'bad-pair', 'changed-record'])
def test_disk_index_and_data_corruption_fail_closed(faulty, corruption):
    import json
    app, client, _, _ = faulty
    app.storage.enqueue(B, A, records(app))
    path = next(app.storage.directory(B).glob('*.data'))
    header, body = path.read_bytes().split(b'\n', 1)
    obj = json.loads(header)
    if corruption == 'truncated':
        body = body[:-1]
    elif corruption == 'wrong-version':
        obj['format'] = 'unknown'
    elif corruption == 'wrong-bytes':
        obj['bytes'] += 1
    elif corruption == 'bad-pair':
        obj['records'][0][0] = 'not-a-uuid'
    else:
        body = body.replace(b'"tag":"', b'"tag":"!', 1)
        body = body[:-2] + body[-1:]  # Keep length, damage wire content.
    path.write_bytes(s.compact(obj) + b'\n' + body)
    poll, _ = client.build_data_body(B)
    response = call(app, 'data', poll)
    assert response.status_code in (200, 503)
    assert b'data-mac' not in response.content
    assert not list(app.storage.directory(B).glob('*.sending'))
    assert not list(app.storage.directory(B).glob('*.data'))


def test_memory_response_releases_batches_after_send(tmp_path, clock):
    import gc
    import weakref
    from phpvpn_client import Configuration, PhpVpnClient
    cfg = s.Config(STORAGE_DRIVER='memory', BUFFER_DIRECTORY=str(tmp_path))
    app = s.Application(cfg, clock=clock)
    client = PhpVpnClient(Configuration('http://unused', cfg.secret, cfg.access_keys, cfg.buffer))
    try:
        app.storage.enqueue(B, A, records(app))
        batch_ref = weakref.ref(app.storage.queues[B][0])
        body, _ = client.build_data_body(B, timestamp=s.timestamp(clock.ms))
        response = app.data(body)
        assert batch_ref() is not None

        async def send(_):
            pass

        asyncio.run(app.emit(response, False, send))
        assert not response.batches
        gc.collect()
        assert batch_ref() is None
    finally:
        app.executor.shutdown()
