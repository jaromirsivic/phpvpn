"""Isolated real-HTTP regression for the embedded status page and storage defaults."""
import json
import base64
import datetime
import hashlib
import hmac
import pathlib
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[1]
PHP = pathlib.Path('D:/nginx/php8.5.9/php.exe')


def request(url, method='GET', data=None):
    try:
        response = urllib.request.urlopen(urllib.request.Request(
            url, method=method, data=data,
            headers={'Content-Type': 'application/json'}), timeout=3)
    except urllib.error.HTTPError as error:
        response = error
    return response.status, response.read(), response.headers


source = (ROOT / 'phpvpn.php').read_text(encoding='utf-8')
mockup = (ROOT / 'assets/index.html').read_text(encoding='utf-8').strip()
for enabled, secret, custom in [(True, 'CHANGE_THIS_SERVER_SECRET', False),
                              (True, '', False), (True, 'secure-example', True),
                              (False, 'secure-example', False)]:
    with tempfile.TemporaryDirectory(prefix='phpvpn-status-') as folder:
        path = pathlib.Path(folder)
        (path / 'phpvpn').mkdir()
        sentinel = path / 'phpvpn/sentinel'
        sentinel.write_text('keep')
        variant = source.replace("const SERVER_SECRET = 'CHANGE_THIS_SERVER_SECRET';",
                                 f"const SERVER_SECRET = '{secret}';")
        if not enabled:
            variant = variant.replace('const STATUS_PAGE_ENABLED = true;',
                                      'const STATUS_PAGE_ENABLED = false;')
        if custom:
            variant = variant.replace("const BUFFER_DIRECTORY = './phpvpn_data';",
                                      "const BUFFER_DIRECTORY = './custom-data';")
        (path / 'phpvpn.php').write_text(variant, encoding='utf-8')
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        server = subprocess.Popen([str(PHP), '-S', f'127.0.0.1:{port}', '-t', folder],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                  creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            url = f'http://127.0.0.1:{port}/phpvpn.php'
            for attempt in range(50):
                try:
                    status, body, headers = request(url)
                    break
                except OSError:
                    time.sleep(.05)
            else:
                raise AssertionError('PHP fixture failed to start')
            assert status == (200 if enabled else 404)
            head_status, head_body, head_headers = request(url, 'HEAD')
            assert head_status == status and head_body == b''
            if enabled:
                expected = mockup
                if secret == 'secure-example':
                    import re
                    expected = re.sub(r'<aside>.*?</aside>', '', expected)
                assert body.decode() == expected
                assert head_headers['Content-Length'] == str(len(body))
            else:
                assert b'not_found' in body and b'<html' not in body
            for action in ['health', 'info', 'data']:
                status, body, _ = request(url + '?action=' + action, 'POST', b'{}')
                assert status in (400, 415), (action, status, body)
            # All three APIs still accept valid requests while the page is disabled.
            effective = (secret or 'CHANGE_THIS_SERVER_SECRET').encode()
            def mac(key, message):
                return hmac.new(key, message, hashlib.sha256).digest()
            def hkdf(ikm, salt, info):
                return mac(mac(salt, ikm), info + b'\x01')
            for action in ['health', 'info', 'data']:
                request_id = str(uuid.uuid4())
                timestamp = datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='milliseconds').replace('+00:00', 'Z')
                fields = {'type': 'auth-none', 'requestId': request_id}
                if action == 'info':
                    key = hkdf(effective, hashlib.sha256(b'phpvpn/auth-semi/salt').digest(), b'phpvpn/auth-semi/key')
                    canonical = f'PHPVPN-AUTH-SEMI\nPOST\ninfo\n{timestamp}\n{request_id}'
                    fields = {'type': 'auth-semi', 'timestamp': timestamp, 'requestId': request_id}
                elif action == 'data':
                    src = '10.99.99.2'
                    key = hkdf(bytes([2]) * 32, mac(effective, b'phpvpn/auth-full/salt\x00' + src.encode()), b'phpvpn/auth-full/key\x00' + src.encode())
                    canonical = f'PHPVPN-AUTH-FULL\nPOST\ndata\n{src}\n{timestamp}\n{request_id}\n' + hashlib.sha256(b'').hexdigest()
                    fields = {'type': 'auth-full', 'src': src, 'timestamp': timestamp, 'requestId': request_id}
                if action != 'health':
                    fields['tag'] = base64.urlsafe_b64encode(mac(key, canonical.encode())).decode().rstrip('=')
                payload = json.dumps(fields, separators=(',', ':')).encode() + (b'\n' if action == 'data' else b'')
                req = urllib.request.Request(url + '?action=' + action, method='POST', data=payload,
                    headers={'Content-Type': 'application/x-ndjson' if action == 'data' else 'application/json'})
                with urllib.request.urlopen(req, timeout=5) as response:
                    assert response.status == 200
            # Config resolution is independent of status routing and never migrates data.
            probe = "define('PHPVPN_NO_AUTORUN', true); require 'phpvpn.php'; echo \\PhpVpn\\Config::fromConstants()->bufferDirectory;"
            resolved = subprocess.check_output([str(PHP), '-r', probe], cwd=folder).decode()
            assert pathlib.Path(resolved) == path / ('custom-data' if custom else 'phpvpn_data')
            assert sentinel.read_text() == 'keep'
            assert not (path / 'phpvpn_data/sentinel').exists()
            if custom:
                assert not (path / 'phpvpn_data').exists()
        finally:
            server.terminate()
            server.wait(timeout=5)
print('PASS: four isolated HTTP variants, embedded HTML, HEAD, API routing, paths and no migration')
