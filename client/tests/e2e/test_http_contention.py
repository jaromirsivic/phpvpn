"""Real PHP workers sharing an isolated disk queue, including held flock timeout.

Run with uv run --python 3.13 --with requests --with cryptography -- python
client/tests/e2e/test_http_contention.py --php D:/nginx/php8.5.9/php.exe
Only newly created fixture paths and child processes are changed.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

import requests

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests-integration"))
from phpvpn_client import (  # noqa: E402
    Configuration, PhpVpnClient, JSON_CONTENT_TYPE, assert_protocol_headers,
    make_record,
)


def free_port() -> int:
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--php", required=True)
    parser.add_argument("--full-suite", action="store_true", help="Run full Python integration against the same isolated workers")
    args = parser.parse_args()
    fixture = Path(tempfile.mkdtemp(prefix="http-contention-", dir=ROOT / "work/build"))
    source = (ROOT / "phpvpn.php").read_bytes()
    (fixture / "phpvpn.php").write_bytes(source)
    (fixture / "router.php").write_text('''<?php
$path = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH);
if ($path === '/phpvpn.php') { require __DIR__ . '/phpvpn.php'; }
elseif ($path === '/hold.php') { require __DIR__ . '/hold.php'; }
else { http_response_code(404); }
''', encoding="utf-8")
    (fixture / "hold.php").write_text('''<?php
$h = fopen(__DIR__ . '/phpvpn/10_99_99_3/.lock', 'cb');
if (!$h || !flock($h, LOCK_EX | LOCK_NB)) { http_response_code(500); exit; }
file_put_contents(__DIR__ . '/lock-ready', 'ready');
$end = microtime(true) + 12;
while (!is_file(__DIR__ . '/lock-release') && microtime(true) < $end) { usleep(10000); }
flock($h, LOCK_UN);
fclose($h);
echo 'released';
''', encoding="utf-8")
    report = {"fixture": str(fixture), "source_sha256": hashlib.sha256(source).hexdigest(), "pass": False}
    workers: list[subprocess.Popen] = []
    logs = []
    session = requests.Session()
    session.trust_env = False
    try:
        ports = [free_port(), free_port()]
        while ports[1] == ports[0]:
            ports[1] = free_port()
        for index, port in enumerate(ports):
            log = (fixture / f"worker-{index}.log").open("wb")
            logs.append(log)
            workers.append(subprocess.Popen(
                [args.php, "-d", "expose_php=0", "-S", f"127.0.0.1:{port}", "-t", str(fixture), str(fixture / "router.php")],
                cwd=fixture, stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW,
            ))
        report["worker_pids"] = [worker.pid for worker in workers]
        urls = [f"http://127.0.0.1:{port}/phpvpn.php" for port in ports]
        for url in urls:
            deadline = time.monotonic() + 5
            while True:
                try:
                    assert session.get(url, timeout=1).status_code == 200
                    break
                except requests.ConnectionError:
                    assert time.monotonic() < deadline, "private PHP worker did not start"
                    time.sleep(0.05)
        config = Configuration(urls[0], b"CHANGE_THIS_SERVER_SECRET",
                               {f"10.99.99.{i}": bytes([i]) * 32 for i in range(2, 16)}, fixture / "phpvpn")
        client = PhpVpnClient(config, session=session)
        client.synchronize_clock()
        (fixture / "phpvpn/10_99_99_3").mkdir(parents=True, exist_ok=True)
        with ThreadPoolExecutor(max_workers=1) as pool:
            holder = pool.submit(requests.get, f"http://127.0.0.1:{ports[1]}/hold.php", timeout=15)
            try:
                deadline = time.monotonic() + 3
                while not (fixture / "lock-ready").exists():
                    assert time.monotonic() < deadline, "second HTTP worker did not acquire actual flock"
                    time.sleep(0.01)
                record = make_record("10.99.99.2", "10.99.99.3", b"isolated-lock-contention")
                began = time.monotonic()
                response, _ = client.send_data("10.99.99.2", [record])
                elapsed = (time.monotonic() - began) * 1000
                report["contention"] = {"status": response.status_code, "body": response.json() if response.status_code != 200 else response.content.decode(),
                                        "headers": dict(response.headers), "elapsed_ms": elapsed,
                                        "holder_still_running": not holder.done()}
                print(json.dumps(report["contention"], ensure_ascii=False), flush=True)
                assert not holder.done(), "lock holder must overlap the entire HTTP response"
                assert response.status_code == 503, "storage lock expiry must be HTTP 503 per protocol chapter 13"
                assert response.json() == {"type": "error", "code": "queue_busy"}
                assert_protocol_headers(response, JSON_CONTENT_TYPE)
                assert response.headers.get("Retry-After") == "1"
                assert 150 <= elapsed < 1500, "real 200ms lock budget plus bounded HTTP/scheduling overhead"
                assert not list((fixture / "phpvpn/10_99_99_3").glob("*.data"))
            finally:
                (fixture / "lock-release").write_text("release", encoding="ascii")
            assert holder.result().text == "released"
        # A fresh explicit retry after lock release must publish and deliver.
        result = client.poll("10.99.99.2", [record])
        assert record.message_id in result.result["acceptedMessageIds"]
        incoming = client.poll("10.99.99.3")
        assert any(item.message_id == record.message_id for item in incoming.records)
        report["recovery"] = "HTTP 200, authenticated acceptedMessageIds and destination delivery"
        if args.full_suite:
            configuration_file = fixture / "server.json"
            configuration_file.write_text(json.dumps({
                "baseUrl": urls[0], "largeBodyBaseUrl": urls[0],
                "serverSecret": "CHANGE_THIS_SERVER_SECRET",
                "accessKeys": {address: key.hex() for address, key in config.access_keys.items()},
                "bufferDirectory": str(config.buffer_directory),
            }), encoding="utf-8")
            with (fixture / "pytest.log").open("wb") as log:
                suite = subprocess.run([sys.executable, "-m", "pytest", "tests-integration", "-q",
                    "--server-config", str(configuration_file), "--large-body-url", urls[0],
                    "--junitxml", str(fixture / "pytest.xml")], cwd=ROOT, stdout=log, stderr=log, timeout=360)
            report["full_suite_exit"] = suite.returncode
            assert suite.returncode == 0, f"integration failed, see {fixture / 'pytest.log'}"
        report["pass"] = True
    except BaseException as error:
        report["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        session.close()
        for worker in workers:
            if worker.poll() is None:
                worker.terminate()  # Only the Popen child handles created above.
            worker.wait(timeout=5)
        for log in logs:
            log.close()
        report["children_stopped"] = all(worker.poll() is not None for worker in workers)
        (fixture / "result.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"Evidence: {fixture / 'result.json'}", flush=True)


if __name__ == "__main__":
    main()
