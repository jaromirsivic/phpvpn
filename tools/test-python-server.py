"""Run the same HTTP suite against isolated PHP/disk, Python/disk, memory and Node/disk.

Use the prepared uv environment (see server-python.md). Never edits live sources
or server.json. Temporary fixture directories and only owned processes are cleaned.
"""
from __future__ import annotations

import argparse
import contextlib
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
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def replace_once(source: str, pattern: str, replacement: str) -> str:
    result, count = re.subn(pattern, lambda _: replacement, source, flags=re.M)
    if count != 1:
        raise RuntimeError(f"Expected one configuration match: {pattern}, got {count}")
    return result


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


@contextlib.contextmanager
def fixture(profile: str, php: str | None = None, directory: Path | None = None,
            python_executable: Path | None = None, server_file: Path | None = None,
            node: str | None = None):
    with contextlib.ExitStack() as stack:
        root = directory or Path(stack.enter_context(tempfile.TemporaryDirectory(prefix="phpvpn-test-")))
        root.mkdir(parents=True, exist_ok=True)
        web = root / "web"
        web.mkdir(exist_ok=True)
        port = free_port()
        implementation, driver = profile.split("-")
        extension = {"php": "php", "python": "py", "node": "js"}[implementation]
        source = (server_file or ROOT / f"phpvpn.{extension}").read_text(encoding="utf-8")
        buffer = (root / "buffer").as_posix()
        runtime_paths = [p for p in sys.path if p]
        if implementation == "php":
            source = replace_once(source, r"^const BUFFER_DIRECTORY = .*;$", f"const BUFFER_DIRECTORY = '{buffer}';")
            php = php or shutil.which("php")
            if not php:
                raise RuntimeError("PHP executable required: --php PATH")
            # The built-in server's stock 404 reflects the URL. Use the same
            # generic denial as a production proxy, without changing phpvpn.php.
            router = root / 'router.php'
            router.write_text("<?php\nif (parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH) !== '/phpvpn.php') {\n"
                              "http_response_code(404); header('Content-Type: application/json');\n"
                              "echo '{\"type\":\"error\",\"code\":\"not_found\"}'; return true; }\nreturn false;\n", encoding='utf-8')
            command = [php, "-d", "post_max_size=8M", "-S", f"127.0.0.1:{port}", "-t", str(web), str(router)]
        elif implementation == "node":
            node = node or shutil.which("node")
            if not node:
                raise RuntimeError("Node.js executable required: --node PATH")
            # The deployed script stays byte identical, fixture values come from
            # an explicit JSON file. The server reads no environment overrides.
            node_config = root / "node-config.json"
            node_config.write_text(json.dumps({
                "BUFFER_DIRECTORY": buffer, "LISTEN_HOST": "127.0.0.1", "LISTEN_PORT": port,
            }), encoding="utf-8")
            command = [node, str(web / "phpvpn.js"), "--config", str(node_config)]
        else:
            source = replace_once(source, r"^BUFFER_DIRECTORY = .*$", f"BUFFER_DIRECTORY = {buffer!r}")
            source = replace_once(source, r"^STORAGE_DRIVER = .*$", f"STORAGE_DRIVER = {driver!r}")
            source = replace_once(source, r"^LISTEN_PORT = .*$", f"LISTEN_PORT = {port}")
            # Bypass Windows' venv redirector so the owned PID is the server,
            # with no short-lived launcher holding the fixture log open.
            executable = sys._base_executable
            if python_executable is not None:
                executable, runtime_paths = json.loads(subprocess.check_output([
                    str(python_executable), '-B', '-c',
                    'import json,sys; print(json.dumps([sys._base_executable, [p for p in sys.path if p]]))'
                ], text=True))
            command = [executable, "-B", str(web / "phpvpn.py")]
        deployed = web / f"phpvpn.{extension}"
        deployed.write_text(source, encoding="utf-8", newline="\n")
        url = f"http://127.0.0.1:{port}/phpvpn.php"
        config = root / "fixture.json"
        config.write_text(json.dumps({
            "baseUrl": url, "largeBodyBaseUrl": url,
            "serverSecret": "CHANGE_THIS_SERVER_SECRET",
            "accessKeys": {f"10.99.99.{n}": f"{n:02x}" * 32 for n in range(2, 16)},
            "bufferDirectory": buffer, "deployedFile": str(deployed),
            "implementation": implementation, "storageDriver": driver,
        }), encoding="utf-8")
        log = stack.enter_context((root / "server.log").open("w", encoding="utf-8"))
        env = {**os.environ, "PYTHONDONTWRITEBYTECODE": "1", "WEB_CONCURRENCY": "4"}
        env['PYTHONPATH'] = os.pathsep.join(runtime_paths)
        process = subprocess.Popen(command, cwd=root, stdout=log, stderr=log, env=env,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError((root / "server.log").read_text(encoding="utf-8"))
                try:
                    with urllib.request.urlopen(url, timeout=1) as response:
                        if response.status == 200:
                            break
                except (OSError, urllib.error.URLError):
                    time.sleep(.05)
            else:
                raise RuntimeError("Fixture did not become ready")
            yield config, url, process, root
        finally:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=10)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profiles", nargs="+", default=["python-disk", "python-memory"],
                        choices=["php-disk", "python-disk", "python-memory", "node-disk"])
    parser.add_argument("--php")
    parser.add_argument("--node", help="Node.js executable for the node-disk profile")
    parser.add_argument("--report-dir", type=Path)
    parser.add_argument("--contract-executable", type=Path, help="Built real C++/WinHTTP contract executable")
    parser.add_argument("--contract-only", action="store_true")
    parser.add_argument("--measure-memory", action="store_true", help="Sample owned server RSS via test dependency psutil")
    args, pytest_args = parser.parse_known_args()
    if pytest_args[:1] == ["--"]:
        pytest_args = pytest_args[1:]
    result = 0
    for profile in args.profiles:
        print(f"\n=== {profile} ===", flush=True)
        with fixture(profile, args.php, node=args.node) as (config, url, process, root):
            if args.contract_executable:
                code = subprocess.call([str(args.contract_executable.resolve())], cwd=ROOT,
                                       env={**os.environ, 'PHPVPN_CONTRACT_URL': url})
                result = max(result, code)
            if args.contract_only:
                if not args.contract_executable:
                    parser.error('--contract-only requires --contract-executable')
                continue
            command = [sys.executable, "-B", "-m", "pytest", "tests-integration", "-ra",
                       "--server-config", str(config), "--large-body-url", url, *pytest_args]
            if args.report_dir:
                args.report_dir.mkdir(parents=True, exist_ok=True)
                command += [f"--junitxml={args.report_dir.resolve() / (profile + '.xml')}"]
            memory_samples, stop = [], threading.Event()
            monitor = None
            if args.measure_memory:
                import psutil
                server_process = psutil.Process(process.pid)

                def sample_memory():
                    while not stop.is_set():
                        try:
                            memory_samples.append(server_process.memory_info().rss)
                        except psutil.NoSuchProcess:
                            return
                        stop.wait(.01)

                monitor = threading.Thread(target=sample_memory, daemon=True)
                monitor.start()
            try:
                code = subprocess.call(command, cwd=ROOT)
            finally:
                stop.set()
                if monitor is not None:
                    monitor.join(timeout=2)
            if memory_samples:
                memory = {'profile': profile, 'rssStartBytes': memory_samples[0],
                          'rssPeakBytes': max(memory_samples), 'rssEndBytes': memory_samples[-1],
                          'samples': len(memory_samples), 'sampleIntervalMs': 10}
                print(json.dumps(memory), flush=True)
                if args.report_dir:
                    (args.report_dir / f'{profile}-memory.json').write_text(json.dumps(memory, indent=2), encoding='utf-8')
            if args.report_dir:
                shutil.copyfile(root / "server.log", args.report_dir / f"{profile}-server.log")
            if profile == "python-memory" and (root / "buffer").exists():
                raise AssertionError("Memory server created operational storage")
            result = max(result, code)
    return result


if __name__ == "__main__":
    raise SystemExit(main())
