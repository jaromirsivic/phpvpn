"""Exercise real PHP atomic info publication in isolated local storage.

No HTTP deployment, privileged filesystem changes or production paths.
Run with uv run --python 3.13 python tools/test-info-storage.py --php PATH.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--php", required=True)
    args = parser.parse_args()
    repository = Path(__file__).resolve().parents[1]
    workspace = repository / "work"
    workspace.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="info-storage-", dir=workspace) as temporary:
        root = Path(temporary).resolve()
        assert root.is_relative_to(workspace.resolve())
        script = root / "exercise.php"
        script.write_text("""<?php
namespace PhpVpn {
function fwrite($stream, string $data, ?int $length=null): int|false {
    static $partialWritten=false;
    $mode=getenv('PHPVPN_INFO_FAULT');
    if (str_contains(stream_get_meta_data($stream)['uri'],'.info-')) {
        if ($mode==='write') return 0;
        if ($mode==='partial') {
            if ($partialWritten) return 0;
            $partialWritten=true;
            return \\fwrite($stream,substr($data,0,7));
        }
    }
    return \\fwrite($stream,$data,$length??strlen($data));
}
function fflush($stream): bool {
    if (str_contains(stream_get_meta_data($stream)['uri'],'.info-')) {
        if (getenv('PHPVPN_INFO_FAULT')==='flush') return false;
        if (getenv('PHPVPN_INFO_FAULT')==='interrupt') {
            file_put_contents(getenv('PHPVPN_INFO_SIGNAL'),'ready');
            sleep(30);
        }
    }
    return \\fflush($stream);
}
}
namespace {
define('PHPVPN_NO_AUTORUN', true);
require $argv[1];
$storage = new \\PhpVpn\\DiskStorage($argv[2]);
$storage->isUsable();
if ($argv[3] === 'sweep') {
    $storage->sweep((int)$argv[4],1000);
} elseif ($argv[3] === 'read') {
    for ($i=0; $i<2500; ++$i) {
        $snapshot=$storage->readClientInfoSnapshot('10.99.99.2');
        if (count($snapshot['contracts'])!==1 || $snapshot['lastSeen']!==hexdec($snapshot['contracts'][0]['hash'])*1000) {
            fwrite(STDERR, "mixed or partial info snapshot\\n"); exit(2);
        }
    }
} else {
    $count=(int)$argv[3]; $base=(int)$argv[4]; $successes=0;
    for ($i=0; $i<$count; ++$i) {
        $time=$base+$i;
        $ok=$storage->publishMaskedContracts('10.99.99.2', [['peer'=>'10.99.99.3','hash'=>str_pad(dechex($time),64,'0',STR_PAD_LEFT)]],$time*1000);
        if ($ok) ++$successes;
    }
    if (!$successes) exit(3);
}
}
""", encoding="utf-8")
        command = [args.php, str(script), str(repository / "phpvpn.php"), str(root / "buffer")]
        subprocess.run(command + ["1", "1700000000"], check=True, capture_output=True)
        processes = [subprocess.Popen(command + ["250", str(base)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                     for base in (1700000100, 1700000500)]
        processes += [subprocess.Popen(command + ["read"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                      for _ in range(2)]
        results = []
        try:
            for process in processes:
                out, error = process.communicate(timeout=30)
                results.append((process.returncode, out, error))
        finally:
            for process in processes:
                if process.poll() is None:
                    process.kill()
                    process.communicate(timeout=5)
        assert all(code == 0 for code, _, _ in results), results
        marker = root / "buffer" / "10_99_99_2" / "info.json"
        previous = marker.read_bytes()
        for mode in ("write", "partial", "flush"):
            failed = subprocess.run(command + ["1", "1700000900"], capture_output=True, timeout=10,
                                    env={**os.environ, "PHPVPN_INFO_FAULT": mode})
            assert failed.returncode == 3, (mode, failed.stderr)
            assert marker.read_bytes() == previous
            assert not list(marker.parent.glob(".info-*.tmp"))
        signal = root / "writer-ready"
        writer = subprocess.Popen(command + ["1", "1700000900"], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  env={**os.environ, "PHPVPN_INFO_FAULT": "interrupt", "PHPVPN_INFO_SIGNAL": str(signal)})
        deadline = time.monotonic() + 5
        try:
            while not signal.exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            assert signal.exists(), "writer never reached pre-publication checkpoint"
        finally:
            writer.kill()
            writer.communicate(timeout=5)
        assert marker.read_bytes() == previous
        assert list(marker.parent.glob(".info-*.tmp")), "interrupted writer left no temp fixture"
        subprocess.run(command + ["sweep", str((int(time.time()) + 302) * 1000)], check=True, capture_output=True)
        assert not list(marker.parent.glob(".info-*.tmp"))
        rename_checked = hasattr(ctypes, "WinDLL")
        if rename_checked:
            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                          ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
            kernel.CreateFileW.restype = ctypes.c_void_p
            kernel.CloseHandle.argtypes = [ctypes.c_void_p]
            # Read sharing only: the owned handle denies rename/delete of this test file.
            handle = kernel.CreateFileW(str(marker), 0x80000000, 1, None, 3, 0, None)
            assert handle != ctypes.c_void_p(-1).value
            try:
                failed = subprocess.run(command + ["1", "1700000900"], capture_output=True, timeout=10)
                assert failed.returncode == 3, failed.stderr
                assert marker.read_bytes() == previous
                assert not list(marker.parent.glob(".info-*.tmp"))
            finally:
                kernel.CloseHandle(handle)
        assert json.loads(marker.read_bytes())["maskedCommunicationContracts"]
        print("PASS: two concurrent publishers, 5000 consistent reads, write/partial/flush failures preserve old snapshot, interrupted writer temp swept, Windows rename failure checked:", rename_checked)


if __name__ == "__main__":
    main()
