"""Real init/start/stop regression on isolated distribution copies, never live servers.

Run using a development venv with pytest/requests/psutil and the server lock.
--distribution accepts an unmodified, freshly extracted complete ZIP directory.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import importlib.metadata
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

import psutil

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / 'tests-integration'))
from phpvpn_client import Configuration, PhpVpnClient, make_record


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def command(root, action):
    if os.name == 'nt':
        # cmd.exe requires its own outer quote pair. Passing a Python argv list
        # would backslash-escape the inner quotes instead of using cmd syntax.
        return '"' + os.environ.get('COMSPEC', 'cmd.exe') + '" /d /v:off /s /c ""' + str(root / (action + '.bat')) + '""'
    return ['sh', str(root / (action + '.sh'))]


def invoke(root, action, expected=0, banner=None, env=None):
    process = subprocess.Popen(command(root, action), cwd=REPO.parent, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                               encoding='utf-8', errors='replace', env=env,
                               creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    # A normal keystroke must not bypass the success delay.
    process.stdin.write('\n')
    process.stdin.close()
    lines, banner_times = [], []
    def collect():
        for line in process.stdout:
            lines.append(line)
            if line.startswith('+'):
                banner_times.append(time.monotonic())
    reader = threading.Thread(target=collect, daemon=True)
    reader.start()
    process.wait(timeout=90)
    finished = time.monotonic()
    reader.join(timeout=5)
    elapsed = None if not banner_times else finished - banner_times[-1]
    output = ''.join(lines)
    assert (process.returncode == 0) == (expected == 0), (action, process.returncode, output)
    if banner:
        assert banner in output, output
        assert elapsed is not None and elapsed >= 3.0, (elapsed, output)
    else:
        assert 'successfully.' not in output, output
    if action == 'stop':
        assert 'Open in your browser:' not in output, output
    return output, elapsed


def configure(root, **settings):
    path = root / 'phpvpn.py'
    source = path.read_text(encoding='utf-8')
    for name, value in settings.items():
        source, count = re.subn(r'^' + name + r' = .*$', lambda _: f'{name} = {value!r}', source, flags=re.M)
        assert count == 1, name
    path.write_text(source, encoding='utf-8', newline='\n')


def read_state(root):
    return json.loads((root / '.runtime/state.json').read_text())


def check_dead(state):
    try:
        process = psutil.Process(state['pid'])
        assert abs(process.create_time() - state['created']) > .001 or process.status() == psutil.STATUS_ZOMBIE
    except psutil.NoSuchProcess:
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--distribution', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = {'platform': sys.platform, 'python': sys.version, 'results': []}
    def passed(name, **details):
        report['results'].append({'name': name, 'result': 'PASS', **details})
        print('PASS:', name, flush=True)
    # Retain artifacts on failure for inspection. A finally block always attempts
    # addressed stop, never terminate/kill of a server.
    root = Path(tempfile.mkdtemp(prefix='phpvpn scripts ')) / 'Žluťoučký & (server)!'
    root.mkdir()
    report['deployment'] = str(root)
    if args.distribution:
        shutil.copytree(args.distribution / 'server/Python', root, dirs_exist_ok=True)
    else:
        shutil.copytree(REPO / 'tools/server-python', root, dirs_exist_ok=True)
        for name in ('phpvpn.py', 'requirements-server.txt', 'server-python.md'):
            shutil.copy2(REPO / name, root / name)
    port = free_port()
    configure(root, LISTEN_PORT=port, BUFFER_DIRECTORY=str(root / 'buffer'))
    foreign_packages = sorted((d.metadata['Name'], d.version) for d in importlib.metadata.distributions())
    env = {**os.environ, 'WEB_CONCURRENCY': '4', 'VIRTUAL_ENV': sys.prefix}
    runtime = root / '.runtime'
    try:
        invoke(root, 'start', expected=1)
        passed('start without environment fails')
        # init has a different success message, no success banner/delay contract.
        for iteration in range(2):
            result = subprocess.run(command(root, 'init'), cwd=REPO.parent, env=env,
                                    capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=700)
            assert result.returncode == 0, result.stdout + result.stderr
            assert 'Environment initialized successfully.' in result.stdout
        passed('init and repeated init with foreign VIRTUAL_ENV')
        local_python = root / '.venv' / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python')
        report['runtime_python'] = subprocess.check_output([str(local_python), '-I', '-c', 'import sys;print(sys.version)'], text=True).strip()
        invoke(root, 'stop')
        passed('stop without server')
        for driver in ('disk', 'memory'):
            configure(root, STORAGE_DRIVER=driver)
            output, delay = invoke(root, 'start', banner='Server started successfully.',
                                   env={**env, 'HTTP_PROXY': 'http://127.0.0.1:1', 'NO_PROXY': ''})
            current = read_state(root)
            assert current['phase'] == 'ready'
            assert current['url'] == f'http://127.0.0.1:{port}/phpvpn.php'
            assert current['url'] in output
            children = psutil.Process(current['pid']).children(recursive=True)
            assert not [p for p in children if p.name().lower() != 'conhost.exe']
            config = Configuration(current['url'], b'CHANGE_THIS_SERVER_SECRET',
                                   {f'10.99.99.{n}': bytes([n]) * 32 for n in range(2, 16)}, root / 'buffer')
            client = PhpVpnClient(config)
            client.synchronize_clock()
            a, b = config.addresses[:2]
            item = make_record(a, b, b'isolated distributed server')
            assert client.poll(a, [item]).accepted_message_ids == [item.message_id]
            assert client.poll(b).records[0].message_id == item.message_id
            client.session.close()
            passed(driver + ' start, true application PID, health/info/data after launcher exit', banner_seconds=delay)
            invoke(root, 'start', expected=1)
            result = subprocess.run(command(root, 'init'), capture_output=True, timeout=30)
            assert result.returncode != 0
            assert read_state(root)['instance'] == current['instance']
            passed(driver + ' duplicate start and init while running rejected')
            _, delay = invoke(root, 'stop', banner='Server stopped successfully.')
            check_dead(current)
            final = read_state(root)
            assert final['phase'] == 'stopped' and final['completed_request']
            assert 'Graceful shutdown completed' in (runtime / 'server.log').read_text()
            passed(driver + ' graceful stop and lifespan confirmation', banner_seconds=delay)
            invoke(root, 'stop')
        # Wildcards, URL encoding, hostname and disabled public status.
        for host in ('0.0.0.0', 'localhost', '::1', '::'):
            if ':' in host:
                try:
                    with socket.socket(socket.AF_INET6) as sock:
                        sock.bind(('::1', 0))
                except OSError:
                    report['results'].append({'name': host, 'result': 'UNAVAILABLE: IPv6'})
                    continue
            configure(root, LISTEN_HOST=host, SERVER_PATH='/api/žluť %/relay.php', STATUS_PAGE_ENABLED=False)
            output, _ = invoke(root, 'start', banner='Server started successfully.')
            assert '%C5%BElu%C5%A5%20%25' in output
            invoke(root, 'stop', banner='Server stopped successfully.')
            passed('verified URL ' + host + ', encoded path, status disabled')
        configure(root, LISTEN_HOST='127.0.0.1', SERVER_PATH='/phpvpn.php', STATUS_PAGE_ENABLED=True)
        with socket.socket() as occupied:
            occupied.bind(('127.0.0.1', port))
            occupied.listen()
            invoke(root, 'start', expected=1)
        check_dead(read_state(root))
        passed('occupied port produces no successful banner or orphan server')
        secondary = root.parent / 'Other managed server'
        secondary.mkdir()
        for path in root.iterdir():
            if path.is_file():
                shutil.copy2(path, secondary / path.name)
        configure(secondary, BUFFER_DIRECTORY=str(secondary / 'buffer'))
        result = subprocess.run(command(secondary, 'init'), capture_output=True, timeout=700)
        assert result.returncode == 0, result.stdout + result.stderr
        try:
            invoke(secondary, 'start', banner='Server started successfully.')
            other_state = read_state(secondary)
            invoke(root, 'start', expected=1)
            assert psutil.Process(other_state['pid']).is_running()
            assert read_state(secondary)['phase'] == 'ready'
            passed('same-secret PHP VPN on occupied port cannot satisfy own startup')
        finally:
            invoke(secondary, 'stop', banner='Server stopped successfully.')
        configure(root, LISTEN_PORT=0)
        invoke(root, 'start', expected=1)
        configure(root, LISTEN_PORT=port)
        passed('invalid configuration rejected')
        controller_path = root / 'server-control.py'
        controller_source = controller_path.read_text(encoding='utf-8')
        # Exercise both a child delayed before publishing its PID and a child
        # delayed after publication but before the Uvicorn bind.
        for timing in ('before identity', 'before bind'):
            modified = controller_source.replace('START_TIMEOUT = 30', 'START_TIMEOUT = 0.5')
            if timing == 'before identity':
                modified = modified.replace('    uuid.UUID(instance)\n', '    time.sleep(2)\n    uuid.UUID(instance)\n')
            else:
                modified = modified.replace('async def serve_async(state):\n', 'async def serve_async(state):\n    await asyncio.sleep(2)\n')
            controller_path.write_text(modified, encoding='utf-8')
            try:
                invoke(root, 'start', expected=1)
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    live = []
                    for process in psutil.process_iter(['pid', 'cmdline']):
                        argv = process.info['cmdline'] or []
                        if str(controller_path) in argv and 'serve' in argv:
                            live.append(process.pid)
                    if not live:
                        break
                    time.sleep(.1)
                assert not live, live
                with socket.socket() as socket_check:
                    assert socket_check.connect_ex(('127.0.0.1', port)) != 0
                passed('readiness timeout ' + timing + ' revokes late child without orphan')
            finally:
                controller_path.write_text(controller_source, encoding='utf-8')
        cryptography = Path(subprocess.check_output([str(local_python), '-I', '-X', 'utf8', '-c',
                             'import cryptography;print(cryptography.__path__[0])'], text=True).strip())
        missing_dependency = cryptography.with_name('cryptography-saved')
        cryptography.rename(missing_dependency)
        try:
            invoke(root, 'start', expected=1)
            check_dead(read_state(root))
            passed('missing runtime dependency fails without orphan')
        finally:
            missing_dependency.rename(cryptography)
        with ThreadPoolExecutor(max_workers=2) as pool:
            futures = [pool.submit(subprocess.run, command(root, 'start'), capture_output=True, timeout=70) for _ in range(2)]
            results = [f.result() for f in futures]
        assert sorted(r.returncode for r in results) == [0, 1], results
        current = read_state(root)
        with ThreadPoolExecutor(max_workers=2) as pool:
            futures = [pool.submit(subprocess.run, command(root, 'stop'), capture_output=True, timeout=70) for _ in range(2)]
            results = [f.result() for f in futures]
        assert all(r.returncode == 0 for r in results), results
        assert sum(b'Server stopped successfully.' in r.stdout for r in results) == 1
        check_dead(current)
        passed('concurrent starts and concurrent stops serialize to one instance')
        previous_instance = read_state(root)['instance']
        starting = subprocess.Popen(command(root, 'start'), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            try:
                during_start = read_state(root)
                if during_start['instance'] != previous_instance:
                    break
            except (FileNotFoundError, PermissionError):
                pass
            time.sleep(.02)
        else:
            raise AssertionError('concurrent start did not publish its identity')
        stopping = subprocess.Popen(command(root, 'stop'), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        start_output = starting.communicate(timeout=70)
        stop_output = stopping.communicate(timeout=70)
        assert starting.returncode == stopping.returncode == 0, (start_output, stop_output)
        check_dead(during_start)
        passed('stop issued while start holds its operation lock')
        saved = (runtime / 'state.json').read_bytes()
        # A real unrelated process with accurate PID/creation time must survive.
        foreign = subprocess.Popen([sys.executable, '-I', '-c', 'import time;time.sleep(120)'])
        try:
            forged = read_state(root)
            forged.update(pid=foreign.pid, created=psutil.Process(foreign.pid).create_time(), phase='ready')
            (runtime / 'state.json').write_text(json.dumps(forged))
            invoke(root, 'stop', expected=1)
            assert foreign.poll() is None
            passed('real foreign PID survives stop with forged metadata')
        finally:
            # This is the test-owned sleeper, never a server or metadata PID.
            foreign.terminate()
            foreign.wait(timeout=10)
            (runtime / 'state.json').write_bytes(saved)
        for bad in ('{invalid json', json.dumps({**read_state(root), 'root': str(root.parent)})):
            (runtime / 'state.json').write_text(bad)
            invoke(root, 'stop', expected=1)
            assert (runtime / 'state.json').read_text() == bad
        (runtime / 'state.json').write_bytes(saved)
        passed('corrupt and copied state rejected without mutation')
        lock_path = root / 'requirements-server.txt'
        original_lock = lock_path.read_bytes()
        try:
            lock_path.write_bytes(original_lock + b'\nphpvpn-deliberately-unavailable-test-package==0.0.0\n')
            result = subprocess.run(command(root, 'init'), capture_output=True, timeout=90,
                                    env={**env, 'UV_OFFLINE': '1'})
            assert result.returncode != 0
            assert b'Environment initialized successfully.' not in result.stdout
            passed('offline synchronization failure propagates without success')
        finally:
            lock_path.write_bytes(original_lock)
        result = subprocess.run(command(root, 'init'), capture_output=True, timeout=90, env=env)
        assert result.returncode == 0, result.stdout + result.stderr
        passed('init recovers after failed synchronization')
        # Hold an incomplete HTTP request open while stopping the actual server.
        for driver in ('disk', 'memory'):
            configure(root, STORAGE_DRIVER=driver)
            invoke(root, 'start', banner='Server started successfully.')
            current = read_state(root)
            with socket.create_connection(('127.0.0.1', port), timeout=5) as active:
                active.sendall(b'POST /phpvpn.php?action=data HTTP/1.1\r\nHost: localhost\r\n'
                               b'Content-Type: application/x-ndjson\r\nContent-Length: 100000\r\n\r\n{')
                invoke(root, 'stop', banner='Server stopped successfully.')
            check_dead(current)
            passed(driver + ' stop with active incomplete data connection')

        # A real delayed lifespan must not turn a stop timeout into a hard kill.
        server_path = root / 'phpvpn.py'
        original_server = server_path.read_text(encoding='utf-8')
        server_path.write_text(original_server.replace('self.executor.shutdown(wait=True)',
                              "__import__('time').sleep(4)\n                    self.executor.shutdown(wait=True)"), encoding='utf-8')
        control_path = root / 'server-control.py'
        original_control = control_path.read_text(encoding='utf-8')
        control_path.write_text(original_control.replace('STOP_TIMEOUT = 45', 'STOP_TIMEOUT = 0.5'), encoding='utf-8')
        try:
            invoke(root, 'start', banner='Server started successfully.')
            current = read_state(root)
            output, _ = invoke(root, 'stop', expected=1)
            assert 'No hard kill' in output
            assert psutil.Process(current['pid']).is_running()
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline and read_state(root)['phase'] != 'stopped':
                time.sleep(.1)
            assert read_state(root)['phase'] == 'stopped'
            time.sleep(.2)
            check_dead(current)
            passed('real delayed lifespan, stop timeout preserves process until graceful completion')
        finally:
            server_path.write_text(original_server, encoding='utf-8')
            control_path.write_text(original_control, encoding='utf-8')

        # Init must neither erase an incompatible environment nor claim success.
        venv_saved = root / '.venv-saved'
        (root / '.venv').rename(venv_saved)
        (root / '.venv').mkdir()
        marker = root / '.venv/keep-me.txt'
        marker.write_text('keep')
        try:
            result = subprocess.run(command(root, 'init'), capture_output=True, timeout=60)
            assert result.returncode != 0
            assert marker.read_text() == 'keep'
            passed('incompatible venv rejected and preserved')
        finally:
            marker.unlink()
            (root / '.venv').rmdir()
            venv_saved.rename(root / '.venv')
        assert sorted((d.metadata['Name'], d.version) for d in importlib.metadata.distributions()) == foreign_packages
        passed('real active foreign venv dependency set unchanged')
        report['status'] = 'PASS'
    finally:
        result = subprocess.run(command(root, 'stop'), capture_output=True, timeout=70)
        report['cleanup_exit'] = result.returncode
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
        print('Isolated test deployment retained:', root, flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
