from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
import time

import pytest

import phpvpn as s
from conftest import call, verified_data
from phpvpn_client import make_record

A, B, C = '10.99.99.2', '10.99.99.3', '10.99.99.4'


def records(app, dst=B, payload=b'x'):
    raw = make_record(A, dst, payload)
    return (s.parse_record(raw.line[:-1], A, app.cfg),)


def test_capacity_atomic_groups_and_expiry(runtime, monkeypatch):
    app, _, clock = runtime
    storage = app.storage
    group = records(app)
    size = len(group[0].line) + 1
    app.cfg.MAX_QUEUE_BYTES_PER_CLIENT = size
    assert storage.enqueue(B, A, group)
    sweeps = []
    original = storage.sweep_queue
    monkeypatch.setattr(storage, 'sweep_queue', lambda *a: (sweeps.append(a), original(*a))[1])
    assert not storage.enqueue(B, A, group)
    assert len(sweeps) == 1
    assert storage.usage(B, clock.ms) == size
    clock.ms += app.cfg.QUEUE_TTL_SECONDS * 1000
    assert storage.usage(B, clock.ms) == 0
    assert storage.drain(B, clock.ms) == []
    assert storage.enqueue(B, A, group)
    claimed = storage.drain(B, clock.ms)
    assert len(claimed) == 1
    assert storage.drain(B, clock.ms) == []
    storage.finish(claimed[0])


def test_parallel_capacity_and_at_most_once(runtime):
    app, _, clock = runtime
    group = records(app)
    size = len(group[0].line) + 1
    app.cfg.MAX_QUEUE_BYTES_PER_CLIENT = size * 4
    with ThreadPoolExecutor(max_workers=8) as pool:
        accepted = list(pool.map(lambda _: app.storage.enqueue(B, A, group), range(16)))
        assert sum(accepted) == 4
        claims = list(pool.map(lambda _: app.storage.drain(B, clock.ms), range(16)))
    batches = [batch for claim in claims for batch in claim]
    assert len(batches) == 4  # Same message ID is intentionally not deduplicated.
    assert sum(batch.size for batch in batches) == size * 4
    for batch in batches:
        app.storage.finish(batch)


def test_lock_timeout_and_unrelated_queue(runtime):
    app, client, _ = runtime
    app.cfg.LOCK_TIMEOUT_MS = 20
    lock = app.storage.locks[B]
    lock.acquire()
    try:
        started = time.monotonic()
        with pytest.raises(s.ProtocolError) as error:
            app.storage.enqueue(B, A, records(app))
        assert .015 <= time.monotonic() - started < .5
        assert error.value.code == 'queue_busy'
        assert app.storage.enqueue(C, A, records(app, C))
        body, _ = client.build_data_body(A, [make_record(A, B, b'locked').line])
        response = call(app, 'data', body)
        assert response.status_code == 503
        assert response.headers['Retry-After'] == '1'
        assert response.json()['code'] == 'queue_busy'
    finally:
        lock.release()


def test_stats_throttle_contract_snapshot_and_future(runtime):
    app, _, clock = runtime
    storage = app.storage
    storage.touch(A, clock.ms)
    assert storage.snapshot(A) == (clock.ms, [])
    old = clock.ms
    storage.touch(A, old + 30_000)
    assert storage.snapshot(A)[0] == old
    storage.touch(A, old + 31_000)
    assert storage.snapshot(A)[0] == old + 31_000
    storage.touch(A, old)
    assert storage.snapshot(A)[0] == old + 31_000
    values = [{'peer': B, 'hash': 'a'*64}]
    storage.publish_contracts(A, values, old)
    values[0]['hash'] = 'b'*64
    assert storage.snapshot(A) == (old, [{'peer': B, 'hash': 'a'*64}])
    read = storage.snapshot(A)[1]
    read.clear()
    storage.touch(A, old + 32_000)
    assert storage.snapshot(A)[1] == [{'peer': B, 'hash': 'a'*64}]
    storage.publish_contracts(A, [], old + 33_000)
    assert storage.snapshot(A) == (old + 33_000, [])


def test_info_snapshot_concurrent_publication(runtime):
    app, _, clock = runtime
    values = [[{'peer': B, 'hash': h*64}] for h in ('a', 'b')]
    app.storage.publish_contracts(A, values[0], clock.ms)
    failures = []

    def writer():
        for i in range(40):
            try:
                app.storage.publish_contracts(A, values[i % 2], clock.ms + (i % 2)*1000)
            except OSError:
                # Windows may refuse replace while the old handle is open.
                failures.append(i)

    with ThreadPoolExecutor(max_workers=2) as pool:
        work = pool.submit(writer)
        while not work.done():
            seen, snapshot = app.storage.snapshot(A)
            assert (seen, snapshot) in [(clock.ms, values[0]), (clock.ms + 1000, values[1])]
        work.result()


def test_restart_state_and_claimed_batches(runtime):
    app, _, clock = runtime
    app.storage.enqueue(B, A, records(app))
    app.storage.publish_contracts(A, [{'peer': B, 'hash': 'a'*64}], clock.ms)
    new = type(app.storage)(app.cfg, clock)
    if isinstance(new, s.MemoryStorage):
        assert new.drain(B, clock.ms) == []
        assert new.snapshot(A) == (None, [])
        assert new.last_sweep == float('-inf')
    else:
        assert new.snapshot(A)[1] == [{'peer': B, 'hash': 'a'*64}]
        claimed = new.drain(B, clock.ms)
        assert len(claimed) == 1
        after_claim = s.DiskStorage(app.cfg, clock)
        assert after_claim.drain(B, clock.ms) == []
        assert claimed[0].path.exists()
        clock.ms += 60_000
        after_claim.sweep_queue(B, clock.ms)
        assert not claimed[0].path.exists()


def test_memory_never_uses_filesystem(tmp_path, clock, monkeypatch):
    def forbidden(*args, **kwargs):
        raise AssertionError('memory used filesystem')
    monkeypatch.setattr(s, 'FileSystem', forbidden)
    cfg = s.Config(STORAGE_DRIVER='memory', BUFFER_DIRECTORY=str(tmp_path / 'forbidden'))
    storage = s.MemoryStorage(cfg, clock)
    storage.is_usable()
    record = s.parse_record(make_record(A, B, b'test').line[:-1], A, cfg)
    storage.enqueue(B, A, (record,))
    storage.touch(A, clock.ms)
    storage.publish_contracts(A, [], clock.ms)
    assert len(storage.drain(B, clock.ms)) == 1
    assert storage.snapshot(A) == (clock.ms, [])
    storage.sweep()
    assert not list(tmp_path.iterdir())


def test_sweep_keeps_stats_and_obeys_file_budget(runtime):
    app, _, clock = runtime
    storage = app.storage
    for _ in range(6):
        storage.enqueue(A, A, records(app, A))
    storage.touch(A, clock.ms)
    clock.ms += 60_000
    with storage.locked(A):
        assert storage.sweep_queue(A, clock.ms, 2, clock.mono + 1) == 2
    assert storage.snapshot(A)[0] is not None
    if isinstance(storage, s.MemoryStorage):
        assert len(storage.queues[A]) == 4
    else:
        assert len(list(storage.directory(A).glob('*.data'))) >= 4
    for _ in range(6):
        storage.sweep_queue(A, clock.ms, 2, clock.mono + 1)
    assert storage.drain(A, clock.ms) == []


def test_memory_bounded_sweep_progress_preserves_publication_order(tmp_path, clock):
    cfg = s.Config(STORAGE_DRIVER='memory', BUFFER_DIRECTORY=str(tmp_path))
    app = s.Application(cfg, clock=clock)
    try:
        group = records(app, A)
        for _ in range(3):
            app.storage.enqueue(A, A, group)
        # A backward wall-clock step can put an earlier expiry behind live data.
        clock.ms -= 120_000
        app.storage.enqueue(A, A, group)
        clock.ms += 120_000
        for _ in range(4):
            assert app.storage.sweep_queue(A, clock.ms, 1, clock.mono + 1) == 1
        claimed = app.storage.drain(A, clock.ms)
        assert [batch.order for batch in claimed] == [0, 1, 2]
        assert A not in app.storage.queues
        assert A not in app.storage.sequences
    finally:
        app.executor.shutdown()


def test_partial_targets_keep_request_order_and_drain_when_full(runtime):
    app, client, _ = runtime
    source = make_record(A, B, b'x')
    app.cfg.MAX_QUEUE_BYTES_PER_CLIENT = len(source.line)
    body, _ = client.build_data_body(A, [source.line])
    assert call(app, 'data', body).status_code == 200
    incoming = make_record(B, A, b'x')
    body, _ = client.build_data_body(B, [incoming.line])
    call(app, 'data', body)  # drains B, so fill B again
    body, _ = client.build_data_body(C, [make_record(C, B, b'x').line])
    call(app, 'data', body)
    to_c, to_b = make_record(A, C, b'x'), make_record(A, B, b'x')
    body, rid = client.build_data_body(A, [to_c.line, to_b.line])
    response = call(app, 'data', body)
    result = verified_data(response, rid, client.full_auth_key(A))
    assert result.result['acceptedMessageIds'] == [to_c.message_id]
    assert result.result['deferredMessageIds'] == [to_b.message_id]
    assert response.content.startswith(incoming.line)
