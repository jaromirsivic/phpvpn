"""Private, single-process controller for a deployed PHP VPN Python server.

Only init is runnable with the bootstrap interpreter. Start/stop/serve use the
deployment's venv. No process is ever signalled by PID, including on timeout.
The deployment user (and OS administrators) is the trust boundary.
"""
import argparse
import asyncio
import base64
import contextlib
import importlib.util
import json
import math
import os
from pathlib import Path
import stat
import subprocess
import sys
import time
import traceback
import urllib.parse
import urllib.request
import uuid

ROOT = Path(__file__).absolute().parent
RUNTIME = ROOT / '.runtime'
START_TIMEOUT = 30
STOP_TIMEOUT = 45
LOCK_TIMEOUT = 60


class ControlError(RuntimeError):
    pass


def retry_io(operation):
    """Bounded retry for Windows sharing violations, never a permissions bypass."""
    deadline = time.monotonic() + 2
    while True:
        try:
            return operation()
        except PermissionError:
            if os.name != 'nt' or time.monotonic() >= deadline:
                raise
            time.sleep(.01)


def no_links(path):
    """Check every existing component before resolving or opening it."""
    for part in (path, *path.parents):
        try:
            info = retry_io(part.lstat)
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ControlError(f'Symlink/reparse path is not allowed: {part}')


def prepare_runtime():
    no_links(RUNTIME)
    RUNTIME.mkdir(mode=0o700, exist_ok=True)
    # Runtime has only flat, private files. Never follow a copied link or tree.
    for path in RUNTIME.iterdir():
        no_links(path)
        if path.exists() and not path.is_file():
            raise ControlError('Unexpected directory in .runtime.')
    if os.name == 'nt':
        script = r"""
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$user = [Security.Principal.WindowsIdentity]::GetCurrent().User
$sids = @($user, [Security.Principal.SecurityIdentifier]'S-1-5-18', [Security.Principal.SecurityIdentifier]'S-1-5-32-544')
$path = $env:PHPVPN_PRIVATE_RUNTIME
$acl = New-Object Security.AccessControl.DirectorySecurity
$acl.SetAccessRuleProtection($true, $false)
foreach ($sid in $sids) {
    $acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($sid, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')))
}
[IO.Directory]::SetAccessControl($path, $acl)
foreach ($file in Get-ChildItem -LiteralPath $path -Force) {
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse point in runtime' }
    $fileAcl = New-Object Security.AccessControl.FileSecurity
    $fileAcl.SetAccessRuleProtection($true, $false)
    foreach ($sid in $sids) {
        $fileAcl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($sid, 'FullControl', 'Allow')))
    }
    try { [IO.File]::SetAccessControl($file.FullName, $fileAcl) }
    catch { if ([IO.File]::Exists($file.FullName)) { throw } }
}
"""
        subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-EncodedCommand',
                        base64.b64encode(script.encode('utf-16-le')).decode('ascii')],
                       env={**os.environ, 'PHPVPN_PRIVATE_RUNTIME': str(RUNTIME)},
                       check=True, timeout=30, stdout=subprocess.DEVNULL,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    else:
        RUNTIME.chmod(0o700)
        for path in RUNTIME.iterdir():
            with contextlib.suppress(FileNotFoundError):
                path.chmod(0o600)


class FileLock:
    def __init__(self, name, timeout=LOCK_TIMEOUT):
        self.path = RUNTIME / name
        self.timeout = timeout
        self.file = None

    def __enter__(self):
        no_links(self.path)
        fd = os.open(self.path, os.O_RDWR | os.O_CREAT, 0o600)
        self.file = os.fdopen(fd, 'r+b', buffering=0)
        if os.fstat(fd).st_size == 0:
            self.file.write(b'0')
        deadline = time.monotonic() + self.timeout
        while True:
            try:
                self.file.seek(0)
                if os.name == 'nt':
                    import msvcrt
                    msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return self
            except OSError:
                if time.monotonic() >= deadline:
                    self.file.close()
                    self.file = None
                    raise ControlError(f'Lock is busy or inaccessible: {self.path.name}') from None
                time.sleep(.05)

    def __exit__(self, *_):
        if self.file is not None:
            self.file.seek(0)
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(self.file.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.file.fileno(), fcntl.LOCK_UN)
            self.file.close()
            self.file = None


def instance_free():
    try:
        with FileLock('instance.lock', 0):
            return True
    except ControlError:
        return False


def write_json(name, value):
    target = RUNTIME / name
    no_links(target)
    temporary = RUNTIME / (uuid.uuid4().hex + '.tmp')
    try:
        fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'w', encoding='utf-8') as stream:
            json.dump(value, stream, ensure_ascii=True)
            stream.flush()
        replace_file(temporary, target)
    finally:
        temporary.unlink(missing_ok=True)


def replace_file(source, target):
    # Windows readers (and virus scanners) may temporarily omit FILE_SHARE_DELETE.
    # Keep the publication atomic, retry rather than deleting the old state.
    retry_io(lambda: os.replace(source, target))


def read_json(name):
    path = RUNTIME / name
    no_links(path)
    try:
        def read():
            with path.open('r', encoding='utf-8') as stream:
                return stream.read(16385)
        text = retry_io(read)
        if len(text) > 16384:
            raise ValueError('too large')
        value = json.loads(text)
        if not isinstance(value, dict):
            raise ValueError('not an object')
        return value
    except FileNotFoundError:
        return None
    except (ValueError, UnicodeError):
        raise ControlError(f'Invalid {name}. Preserve .runtime and inspect it manually.') from None


def read_state():
    state = read_json('state.json')
    if state is None:
        return None
    if (state.get('schema') != 1 or type(state.get('pid')) is not int or state['pid'] <= 0
            or type(state.get('created')) not in (int, float) or not math.isfinite(state['created'])
            or state['created'] <= 0 or state.get('root') != str(ROOT.resolve())
            or state.get('phase') not in ('starting', 'ready', 'stopping', 'stopped', 'failed')
            or not isinstance(state.get('url'), str)):
        raise ControlError('Invalid or foreign deployment state. No process was touched.')
    try:
        uuid.UUID(state['instance'])
    except (KeyError, ValueError, TypeError, AttributeError):
        raise ControlError('Invalid instance identity.') from None
    return state


def identity_alive(state):
    import psutil
    try:
        process = psutil.Process(state['pid'])
        if abs(process.create_time() - state['created']) > .001:
            raise ControlError('PID creation time mismatch. No process was touched.')
        return process.is_running() and process.status() != psutil.STATUS_ZOMBIE
    except psutil.NoSuchProcess:
        return False
    except psutil.AccessDenied:
        raise ControlError('Process identity is inaccessible. No process was touched.') from None


def same_identity(left, right):
    return all(left.get(k) == right.get(k) for k in ('instance', 'root', 'pid', 'created'))


def request(state, action):
    rid = str(uuid.uuid4())
    write_json('request.json', {'instance': state['instance'], 'request': rid, 'action': action})
    return rid


def launch_permitted(instance):
    ticket = read_json('launch.json')
    return bool(ticket and ticket.get('instance') == instance
                and ticket.get('root') == str(ROOT.resolve()) and ticket.get('active') is True)


def challenge(state):
    if not identity_alive(state) or instance_free():
        raise ControlError('Instance is not alive or does not own its lock.')
    rid = request(state, 'ping')
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        reply = read_json('reply.json')
        if reply and reply.get('request') == rid and same_identity(reply, state):
            return
        time.sleep(.05)
    raise ControlError('Instance did not confirm its identity. No stop request was sent.')


def local_url(cfg):
    host = {'0.0.0.0': '127.0.0.1', '::': '::1'}.get(cfg.LISTEN_HOST, cfg.LISTEN_HOST)
    host = host.strip('[]')
    host = '[' + host.replace('%', '%25') + ']' if ':' in host else host.encode('idna').decode('ascii')
    return f'http://{host}:{cfg.LISTEN_PORT}' + urllib.parse.quote(cfg.SERVER_PATH, safe='/')


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *_args, **_kwargs):
        return None


def probe(module, cfg, url):
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    rid = str(uuid.uuid4())
    body = json.dumps({'type': 'auth-none', 'requestId': rid}).encode('ascii')
    req = urllib.request.Request(url + '?action=health', data=body,
                                 headers={'Content-Type': 'application/json'})
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    with opener.open(req, timeout=2) as response:
        if response.status != 200:
            raise ControlError('Health HTTP status is not 200.')
        result = json.loads(response.read(16384))
    if (result['requestId'] != rid or result['status'] != 'running'
            or result['description'] != 'OK' or result['serverVersion'] != cfg.SERVER_VERSION):
        raise ControlError('Health response does not match this request.')
    aad = (f"PHPVPN-HEALTH-RESPONSE\nhealth\n{rid}\n{result['serverVersion']}\nrunning\nOK\n"
           f"{result['timestamp']}\n{result['iv']}").encode('utf-8')
    AESGCM(module.health_key(cfg.secret, rid)).decrypt(
        module.b64decode(result['iv'], 12), module.b64decode(result['tag'], 16), aad)


def load_server():
    spec = importlib.util.spec_from_file_location('phpvpn', ROOT / 'phpvpn.py')
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def banner(started, url=None):
    lines = ['PHP VPN', 'Server started successfully.' if started else 'Server stopped successfully.']
    if started:
        lines.append('Open in your browser: ' + url)
    width = max(map(len, lines)) + 2
    border = '+' + '-' * width + '+'
    print('\n' + border)
    for line in lines:
        print('| ' + line.ljust(width - 2) + ' |')
    print(border, flush=True)
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        time.sleep(max(0, deadline - time.monotonic()))


def stop_instance(state):
    challenge(state)
    rid = request(state, 'stop')
    deadline = time.monotonic() + STOP_TIMEOUT
    while time.monotonic() < deadline:
        if not identity_alive(state):
            final = read_state()
            if (final and same_identity(final, state) and final['phase'] == 'stopped'
                    and final.get('completed_request') == rid and instance_free()):
                return
            raise ControlError('Process exited without confirmed graceful shutdown.')
        time.sleep(.1)
    raise ControlError('Graceful shutdown timed out. State and log preserved in .runtime. No hard kill was used.')


def stop():
    state = read_state()
    if state is None:
        if not instance_free():
            raise ControlError('Instance lock is held but state is missing.')
        print('Server is not running.')
        return
    if not identity_alive(state):
        if not instance_free():
            raise ControlError('Dead state but instance lock is held.')
        print('Server is not running.')
        return
    stop_instance(state)
    banner(False)


def start():
    if (ROOT / '.bootstrap.lock').exists():
        raise ControlError('Initialization is in progress (.bootstrap.lock).')
    state = read_state()
    if state and identity_alive(state):
        challenge(state)
        raise ControlError('Server is already running.')
    if not instance_free():
        raise ControlError('Instance lock is held. Preserve .runtime and inspect the server.')
    if state:
        replace_file(RUNTIME / 'state.json', RUNTIME / 'previous-state.json')
    instance = str(uuid.uuid4())
    ticket = {'instance': instance, 'root': str(ROOT.resolve()), 'active': True}
    write_json('launch.json', ticket)
    command = [str(venv_python()), '-I', '-B', str(ROOT / 'server-control.py'), 'serve', instance]
    options = {'creationflags': subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == 'nt' else {'start_new_session': True}
    no_links(RUNTIME / 'server.log')
    fd = os.open(RUNTIME / 'server.log', os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
    with os.fdopen(fd, 'ab', buffering=0) as log:
        process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.DEVNULL,
                                   stdout=log, stderr=log, close_fds=True, **options)
    deadline = time.monotonic() + START_TIMEOUT
    try:
        while time.monotonic() < deadline:
            state = read_state()
            if state and state['instance'] == instance:
                if state['phase'] == 'ready' and identity_alive(state):
                    challenge(state)
                    banner(True, state['url'])
                    return
                if state['phase'] in ('failed', 'stopped'):
                    raise ControlError('Server startup failed. See .runtime/server.log.')
            if process.poll() is not None:
                raise ControlError('Server process exited during startup. See .runtime/server.log.')
            time.sleep(.05)
        raise ControlError('Readiness timed out. See .runtime/server.log.')
    except BaseException:
        # Revoke even a child delayed before it can publish PID metadata. A late
        # child must not bind after its initiating start command has failed.
        write_json('launch.json', {**ticket, 'active': False})
        state = read_state()
        if state and state['instance'] == instance and identity_alive(state):
            try:
                stop_instance(state)
            except ControlError as exc:
                print(f'Cleanup incomplete: {exc} Run stop again. Instance: {instance}', file=sys.stderr)
        elif process.poll() is None:
            print(f'Launcher still exists (PID {process.pid}), without confirmed server identity. '
                  f'Inspect .runtime/server.log. Start ID: {instance}', file=sys.stderr)
        raise


async def serve_async(state):
    module = load_server()
    cfg = module.Config()
    if not launch_permitted(state['instance']):
        raise ControlError('Startup authorization expired before bind.')
    state['url'] = local_url(cfg)
    server = module.create_server(cfg)
    application = server.config.app
    shutdown_complete = False
    stop_request = None

    async def tracked_app(scope, receive, send):
        async def tracked_send(message):
            nonlocal shutdown_complete
            await send(message)
            if message['type'] == 'lifespan.shutdown.complete':
                shutdown_complete = True
        await application(scope, receive, tracked_send)

    server.config.app = tracked_app

    async def monitor():
        nonlocal stop_request
        last_request = None
        while True:
            message = read_json('request.json')
            if (message and message.get('instance') == state['instance']
                    and message.get('request') != last_request):
                last_request = message.get('request')
                if message.get('action') in ('ping', 'stop'):
                    write_json('reply.json', {**state, 'request': last_request})
                if message.get('action') == 'stop':
                    stop_request = last_request
                    state['phase'] = 'stopping'
                    write_json('state.json', state)
                    server.should_exit = True
            if not launch_permitted(state['instance']):
                server.should_exit = True
            if server.started and state['phase'] == 'starting':
                # Runs outside the event loop so this same server can answer it.
                await asyncio.to_thread(probe, module, cfg, state['url'])
                if launch_permitted(state['instance']) and not server.should_exit:
                    state['phase'] = 'ready'
                    write_json('state.json', state)
            await asyncio.sleep(.05)

    watcher = asyncio.create_task(monitor())
    async def checked_monitor():
        try:
            await watcher
        except Exception as exc:
            # Frame names and OS codes identify I/O failures without logging
            # configuration, request bodies, exception text or secret values.
            frames = ' > '.join(f'{frame.name}:{frame.lineno}' for frame in traceback.extract_tb(exc.__traceback__))
            print(f'Controller monitor failed: {type(exc).__name__}, errno={getattr(exc, "errno", None)}, '
                  f'winerror={getattr(exc, "winerror", None)}, at {frames}', flush=True)
            server.should_exit = True

    guard = asyncio.create_task(checked_monitor())
    try:
        await server.serve()
    finally:
        watcher.cancel()
        guard.cancel()
        await asyncio.gather(watcher, guard, return_exceptions=True)
    if shutdown_complete and stop_request:
        state.update(phase='stopped', completed_request=stop_request)
        write_json('state.json', state)
        print('Graceful shutdown completed, including application lifespan.', flush=True)
    else:
        raise ControlError('Server exited without an addressed graceful shutdown.')


def serve(instance):
    uuid.UUID(instance)
    # Parent established permissions. Do not reset ACLs concurrently with writers.
    no_links(RUNTIME)
    if not launch_permitted(instance):
        raise ControlError('Startup authorization expired.')
    import psutil
    with FileLock('instance.lock', 0):
        state = {'schema': 1, 'instance': instance, 'pid': os.getpid(),
                 'created': psutil.Process().create_time(), 'root': str(ROOT.resolve()),
                 'phase': 'starting', 'url': ''}
        write_json('state.json', state)
        try:
            asyncio.run(serve_async(state))
        except BaseException as exc:
            state['phase'] = 'failed'
            write_json('state.json', state)
            # Do not log arbitrary exception messages from configuration or crypto.
            print(f'Server failed: {type(exc).__name__}', flush=True)
            return 1
    return 0


def venv_python():
    return ROOT / '.venv' / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python')


def initialize(uv):
    # Bootstrap Python needs no third-party modules. Hold both locks for the
    # entire venv operation, excluding concurrent start, stop and initialization.
    with FileLock('instance.lock', 0):
        python = venv_python()
        venv = ROOT / '.venv'
        no_links(venv)
        env = {k: v for k, v in os.environ.items() if k not in ('VIRTUAL_ENV', 'PYTHONHOME', 'PYTHONPATH')}
        env.update(UV_HTTP_TIMEOUT='30', UV_HTTP_RETRIES='2')
        def run(*args):
            subprocess.run([str(uv), '--no-config', *map(str, args)], cwd=ROOT,
                           env=env, check=True, timeout=600)
        if venv.exists():
            try:
                subprocess.run([str(python), '-I', '-c',
                                'import sys; assert sys.version_info[:2] == (3,13); '
                                'assert sys.prefix != sys.base_prefix'], check=True, timeout=10,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except (OSError, subprocess.SubprocessError):
                raise ControlError('Incompatible/broken .venv. Stop the server, move .venv aside, then run init again.') from None
        else:
            run('venv', '--managed-python', '--python', '3.13', venv)
        run('pip', 'sync', '--python', python, ROOT / 'requirements-server.txt')
        run('pip', 'check', '--python', python)
        subprocess.run([str(python), '-I', '-B', '-c',
                        'import cryptography, uvicorn, psutil; import sys; assert sys.version_info[:2] == (3,13)'],
                       check=True, timeout=15, env=env)
        # uv's dry-run must find no additions/removals after synchronization.
        run('pip', 'sync', '--dry-run', '--python', python, ROOT / 'requirements-server.txt')
        print('Environment initialized successfully.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('start', 'stop', 'serve', 'init'))
    parser.add_argument('argument', nargs='?')
    args = parser.parse_args()
    try:
        if args.action == 'serve':
            return serve(args.argument)
        prepare_runtime()
        with FileLock('operation.lock'):
            if args.action == 'init':
                if not args.argument:
                    raise ControlError('init requires the absolute uv path.')
                initialize(Path(args.argument).resolve())
            else:
                import psutil  # noqa: F401 -- fail before starting anything
                if sys.version_info[:2] != (3, 13) or Path(sys.prefix).resolve() != (ROOT / '.venv').resolve():
                    raise ControlError('Use this deployment venv. Run init first.')
                (start if args.action == 'start' else stop)()
        return 0
    except (Exception, KeyboardInterrupt) as exc:
        message = str(exc) if isinstance(exc, ControlError) else type(exc).__name__ + ': operation failed; run init or inspect .runtime/server.log.'
        print('ERROR: ' + message, file=sys.stderr, flush=True)
        return 1


if __name__ == '__main__':
    sys.exit(main())
