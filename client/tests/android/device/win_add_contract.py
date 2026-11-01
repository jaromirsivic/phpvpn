"""Adds a communication contract for the phone (PEER) to the Windows client's configuration.

A fresh random payload key is generated; nothing secret is printed. The document goes through
`phpvpn.exe set-config <file>` from a temporary file that is overwritten and deleted afterwards.
"""
import json, os, secrets, subprocess, sys, tempfile

EXE = r"C:\Program Files\phpvpn\1.0.2\phpvpn.exe"
PEER = sys.argv[1] if len(sys.argv) > 1 else "10.99.99.5"

doc = json.loads(open(r"C:\ProgramData\phpvpn\config.json", encoding="utf-8-sig").read())
contracts = doc["communicationContracts"]
if PEER in contracts:
    print("contract already present for", PEER, "- unchanged")
    sys.exit(0)
contracts[PEER] = {"payloadKey": secrets.token_hex(32)}
body = (json.dumps(doc, indent=4) + "\n").encode("utf-8")
fd, path = tempfile.mkstemp(suffix=".json")
try:
    os.write(fd, body)
    os.close(fd)
    r = subprocess.run([EXE, "set-config", path], capture_output=True, text=True)
    out = r.stdout + r.stderr
    print("set-config exit", r.returncode, "result ok" if '"result": "ok"' in out or '"result":"ok"' in out else out[:300])
finally:
    with open(path, "r+b") as f:
        f.write(b"\0" * len(body))
    os.remove(path)
    print("temp file removed:", not os.path.exists(path))
print("contracts now:", sorted(contracts))
