"""Real sockets, process restarts, single-worker launch and HTTPS proxy."""
from __future__ import annotations

from contextlib import contextmanager
from datetime import datetime, timedelta, timezone
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import ipaddress
from pathlib import Path
import ssl
import threading

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
import psutil
import pytest
import requests

from conftest import ROOT
from phpvpn_client import Configuration, PhpVpnClient, make_record

spec = importlib.util.spec_from_file_location('fixture_runner', ROOT / 'tools/test-python-server.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


@pytest.mark.parametrize('profile', ['python-disk', 'python-memory'])
def test_process_restart_and_single_worker(profile, tmp_path):
    with runner.fixture(profile, directory=tmp_path) as (config, url, process, root):
        children = psutil.Process(process.pid).children(recursive=True)
        # Windows may create its console host, which is not an application worker.
        assert [child.name() for child in children if child.name().lower() != 'conhost.exe'] == []
        cfg = Configuration.load(config)
        client = PhpVpnClient(cfg)
        client.synchronize_clock()
        a, b = cfg.addresses[:2]
        client.extended_info(a, {'maskedCommunicationContracts': [{'peer': b, 'hash': 'a'*64}]})
        queued = make_record(a, b, b'preserve only disk across process restart')
        assert client.poll(a, [queued]).accepted_message_ids == [queued.message_id]
        client.session.close()
        if profile == 'python-memory':
            assert not (root / 'buffer').exists()
    with runner.fixture(profile, directory=tmp_path) as (config, url, process, root):
        cfg = Configuration.load(config)
        client = PhpVpnClient(cfg)
        client.synchronize_clock()  # Fresh verified health, including recovery clock.
        document, _ = client.info_plaintext()
        assert bool(document['clients']) == (profile == 'python-disk')
        response = client.poll(b)
        assert [r.message_id for r in response.records] == ([queued.message_id] if profile == 'python-disk' else [])
        assert client.poll(b).records == []
        client.session.close()
        if profile == 'python-memory':
            assert not (root / 'buffer').exists()


@contextmanager
def https_proxy(upstream: str, root: Path):
    """Test-only TLS terminator with a private CA, no OS trust-store changes."""
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    subject = x509.Name([x509.NameAttribute(x509.oid.NameOID.COMMON_NAME, 'PHPVPN isolated test')])
    cert = (x509.CertificateBuilder().subject_name(subject).issuer_name(subject).public_key(key.public_key())
            .serial_number(x509.random_serial_number()).not_valid_before(datetime.now(timezone.utc)-timedelta(minutes=1))
            .not_valid_after(datetime.now(timezone.utc)+timedelta(hours=2))
            .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
            .add_extension(x509.SubjectAlternativeName([x509.IPAddress(ipaddress.ip_address('127.0.0.1'))]), critical=False)
            .sign(key, hashes.SHA256()))
    pem, keyfile = root / 'test-ca.pem', root / 'test-key.pem'
    pem.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    keyfile.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    port = int(upstream.split(':')[2].split('/')[0])

    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'

        def log_message(self, *args):
            pass

        def forward(self):
            body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
            conn = http.client.HTTPConnection('127.0.0.1', port, timeout=30)
            try:
                headers = {k: v for k, v in self.headers.items() if k.lower() not in ('host', 'connection', 'transfer-encoding')}
                # Hostile proxy metadata must never change wire authorization.
                headers.update({'X-Forwarded-For': '10.99.99.2', 'X-Forwarded-Proto': 'ftp',
                                'Forwarded': 'for=10.99.99.3;proto=ftp'})
                conn.request(self.command, self.path, body, headers)
                response = conn.getresponse()
                payload = response.read()
                self.send_response(response.status)
                for k, v in response.getheaders():
                    if k.lower() not in ('content-length', 'transfer-encoding', 'connection', 'date', 'server'):
                        self.send_header(k, v)
                self.send_header('Content-Length', str(len(payload)))
                self.end_headers()
                if self.command != 'HEAD':
                    self.wfile.write(payload)
            finally:
                conn.close()

        do_POST = do_GET = do_HEAD = forward

    httpd = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(pem, keyfile)
    httpd.socket = context.wrap_socket(httpd.socket, server_side=True)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    try:
        yield f'https://127.0.0.1:{httpd.server_port}/phpvpn.php', pem
    finally:
        httpd.shutdown()
        httpd.server_close()
        thread.join(timeout=5)


@pytest.mark.parametrize('profile', ['python-disk', 'python-memory'])
def test_https_proxy_whole_queue_and_exact_body(profile, tmp_path):
    with runner.fixture(profile) as (config, url, process, root):
        with https_proxy(url, tmp_path) as (tls_url, ca):
            cfg = Configuration.load(config)
            with requests.Session() as session:
                session.verify = str(ca)
                client = PhpVpnClient(cfg, session=session, base_url=tls_url)
                client.synchronize_clock()
                assert client.info_plaintext()[0]['limits']['maxDataRecordsPerRequest'] == 16
                a, b = cfg.addresses[:2]
                sent = []
                for _ in range(3):
                    records = [make_record(a, b, b'x'*32768) for _ in range(16)]
                    result = client.poll(a, records)
                    assert len(result.accepted_message_ids) == 16
                    sent.extend(r.line for r in records)
                drained = client.poll(b)
                assert len(drained.records) == 48
                assert sorted(line + b'\n' for line in drained.raw.split(b'\n')[:-3]) == sorted(sent)
                assert client.poll(b).records == []
            if profile == 'python-memory':
                assert not (root / 'buffer').exists()


def test_fixture_replacement_rejects_missing_or_duplicate_header():
    with pytest.raises(RuntimeError):
        runner.replace_once('x=1\nx=2', '^x=.*$', 'x=3')
    with pytest.raises(RuntimeError):
        runner.replace_once('y=1', '^x=.*$', 'x=3')
