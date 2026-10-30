"""Safety properties that cannot reliably be induced with real PID recycling."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import time
import types
import uuid
from concurrent.futures import ThreadPoolExecutor

import psutil
import pytest

spec = importlib.util.spec_from_file_location('server_control', Path(__file__).resolve().parents[1] / 'tools/server-python/server-control.py')
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


@pytest.fixture
def control(tmp_path, monkeypatch):
    monkeypatch.setattr(c, 'ROOT', tmp_path)
    monkeypatch.setattr(c, 'RUNTIME', tmp_path / '.runtime')
    c.RUNTIME.mkdir()
    return c


def state(control, **changes):
    return {'schema': 1, 'root': str(control.ROOT.resolve()), 'pid': os.getpid(),
            'created': psutil.Process().create_time(), 'instance': str(uuid.uuid4()),
            'phase': 'ready', 'url': 'http://127.0.0.1:8000/phpvpn.php', **changes}


@pytest.mark.parametrize('host,expected', [
    ('0.0.0.0', '127.0.0.1'), ('::', '[::1]'), ('::1', '[::1]'),
    ('fe80::1%3', '[fe80::1%253]'), ('localhost', 'localhost')])
def test_usable_encoded_url(host, expected):
    cfg = types.SimpleNamespace(LISTEN_HOST=host, LISTEN_PORT=8123, SERVER_PATH='/api/žluť %/relay.php')
    assert c.local_url(cfg) == f'http://{expected}:8123/api/%C5%BElu%C5%A5%20%25/relay.php'


def test_reused_pid_is_error_and_never_signalled(control, monkeypatch):
    current = state(control, created=psutil.Process().create_time() - 10)
    monkeypatch.setattr(c, 'request', lambda *_: pytest.fail('must not send a request'))
    with pytest.raises(c.ControlError, match='creation time mismatch'):
        c.stop_instance(current)
    assert psutil.Process(os.getpid()).is_running()


def test_access_denied_is_not_evidence_of_exit(control, monkeypatch):
    current = state(control)
    def denied(*_):
        raise psutil.AccessDenied(current['pid'])
    monkeypatch.setattr(psutil, 'Process', denied)
    with pytest.raises(c.ControlError, match='inaccessible'):
        c.identity_alive(current)


@pytest.mark.parametrize('bad', ['{', '[]', 'null', 'x' * 17000])
def test_corrupt_state_is_preserved(control, bad):
    path = c.RUNTIME / 'state.json'
    path.write_text(bad)
    with pytest.raises(c.ControlError):
        c.stop()
    assert path.read_text() == bad


@pytest.mark.parametrize('changes', [{'root': '/other'}, {'pid': 0}, {'pid': True},
                                   {'created': float('nan')}, {'instance': 'bad'}, {'phase': 'unknown'}])
def test_invalid_and_copied_state_rejected(control, changes):
    c.write_json('state.json', state(control, **changes))
    with pytest.raises(c.ControlError):
        c.read_state()


def test_missing_state_cannot_hide_owned_lock(control):
    with c.FileLock('instance.lock', 0):
        with pytest.raises(c.ControlError, match='held but state is missing'):
            c.stop()
    assert c.instance_free()


def test_os_lock_excludes_other_handle_and_is_reusable(control):
    with c.FileLock('operation.lock', 0):
        with pytest.raises(c.ControlError, match='busy'):
            with c.FileLock('operation.lock', 0):
                pytest.fail('second lock acquired')
    with c.FileLock('operation.lock', 0):
        pass


def test_stale_dead_process_is_noop(control, monkeypatch, capsys):
    c.write_json('state.json', state(control))
    monkeypatch.setattr(c, 'identity_alive', lambda _: False)
    c.stop()
    assert capsys.readouterr().out == 'Server is not running.\n'


def test_foreign_live_pid_without_lock_never_gets_stop(control, monkeypatch):
    monkeypatch.setattr(c, 'request', lambda *_: pytest.fail('foreign process must not be addressed'))
    with pytest.raises(c.ControlError, match='does not own'):
        c.stop_instance(state(control))


def test_timeout_keeps_state_and_does_not_kill(control, monkeypatch):
    current = state(control)
    c.write_json('state.json', current)
    actions = []
    monkeypatch.setattr(c, 'challenge', lambda _: None)
    monkeypatch.setattr(c, 'request', lambda _, action: actions.append(action) or 'request')
    monkeypatch.setattr(c, 'STOP_TIMEOUT', .01)
    with pytest.raises(c.ControlError, match='No hard kill'):
        c.stop_instance(current)
    assert actions == ['stop']
    assert c.read_state() == current
    assert psutil.Process().is_running()


def test_exit_without_shutdown_confirmation_is_not_success(control, monkeypatch):
    current = state(control)
    c.write_json('state.json', current)
    monkeypatch.setattr(c, 'challenge', lambda _: None)
    monkeypatch.setattr(c, 'identity_alive', lambda _: False)
    with pytest.raises(c.ControlError, match='without confirmed graceful'):
        c.stop_instance(current)


def test_stop_banner_waits_full_three_seconds_without_url(control, capsys):
    started = time.monotonic()
    c.banner(False)
    assert time.monotonic() - started >= 3
    output = capsys.readouterr().out
    assert 'Server stopped successfully.' in output
    assert 'http' not in output and 'browser' not in output


def test_runtime_symlink_is_rejected(control):
    target = control.ROOT / 'target'
    target.write_text('keep')
    link = control.RUNTIME / 'state.json'
    try:
        link.symlink_to(target)
    except OSError:
        pytest.skip('symlink creation unavailable for this user')
    with pytest.raises(c.ControlError, match='Symlink/reparse'):
        c.read_state()
    assert target.read_text() == 'keep'


def test_atomic_channel_with_concurrent_reader(control):
    c.write_json('reply.json', {'sequence': 0, 'body': 'x' * 4000})
    def writer():
        for index in range(250):
            c.write_json('reply.json', {'sequence': index, 'body': 'x' * 4000})
    def reader():
        for _ in range(500):
            value = c.read_json('reply.json')
            assert type(value['sequence']) is int
            assert value['body'] == 'x' * 4000
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures = [pool.submit(writer), pool.submit(reader)]
        for future in futures:
            future.result(timeout=30)


def test_cancelled_or_foreign_start_ticket_cannot_launch(control):
    instance = str(uuid.uuid4())
    assert not c.launch_permitted(instance)
    c.write_json('launch.json', {'instance': instance, 'root': str(c.ROOT.resolve()), 'active': True})
    assert c.launch_permitted(instance)
    assert not c.launch_permitted(str(uuid.uuid4()))
    c.write_json('launch.json', {'instance': instance, 'root': str(c.ROOT.resolve()), 'active': False})
    with pytest.raises(c.ControlError, match='authorization expired'):
        c.serve(instance)
    assert not (c.RUNTIME / 'state.json').exists()
