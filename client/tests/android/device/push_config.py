"""Builds the phone's configuration from the relay and the Windows config without printing any secret.

Usage: python push_config.py [relay url] [--dry]
adb, the phone and the relay come from phpvpn.config.json (tools.adb, android.deviceSerial,
android.relayUrl, android.relayServerFile, android.virtualIp, android.peerIp); ANDROID_SERIAL wins.
The document goes to the app's cacheDir through `adb exec-in run-as` (no file on shared storage), the
debug receiver stores it with set-config and deletes it. Only key names and lengths are printed.
"""
import json, os, re, subprocess, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools"))
import phpvpn_config  # noqa: E402

ADB = phpvpn_config.tool("adb")
SERIAL = os.environ.get("ANDROID_SERIAL") or phpvpn_config.get("android.deviceSerial")
PKG = "com.pilandrome.phpvpn"
URL = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1].startswith("http") else phpvpn_config.get("android.relayUrl")
MY_IP = phpvpn_config.get("android.virtualIp", "10.99.99.5")
PEER_IP = phpvpn_config.get("android.peerIp", "10.99.99.2")
WINDOWS_CONFIG = Path(os.environ.get("ProgramData", r"C:\ProgramData")) / "phpvpn" / "config.json"
RELAY_FILE = phpvpn_config.path("android.relayServerFile")
if not ADB or not SERIAL or not URL or RELAY_FILE is None:
    sys.exit("set tools.adb, android.deviceSerial, android.relayUrl and android.relayServerFile in phpvpn.config.json")


def adb(*args, data=None):
    return subprocess.run([ADB, "-s", SERIAL, *args], input=data, capture_output=True, check=True).stdout


default = json.loads(adb("exec-out", "run-as", PKG, "cat", "no_backup/phpvpn/config/config.json"))
win = json.loads(WINDOWS_CONFIG.read_text(encoding="utf-8-sig"))
php = RELAY_FILE.read_text(encoding="utf-8")
secret = re.search(r"const SERVER_SECRET = '([^']*)'", php).group(1)
block = re.search(r"const ACCESS_KEYS = \[(.*?)\];", php, re.S).group(1)
keys = dict(re.findall(r"'([0-9.]+)'\s*=>\s*'([0-9a-fA-F]+)'", block))

doc = default
ms = doc["masterSetup"]
print("default masterSetup keys:", sorted(ms))
print("default top-level keys:", sorted(doc))
ms["serverUrl"] = URL
ms["serverSecret"] = secret
ms["virtualIp"] = MY_IP
ms["accessKey"] = keys[MY_IP]
peer_key = win["communicationContracts"][MY_IP]["payloadKey"]
doc["communicationContracts"] = {PEER_IP: {"payloadKey": peer_key}}
print("secret len", len(secret), "accessKey len", len(ms["accessKey"]), "payloadKey len", len(peer_key),
      "secret==windows", secret == win["masterSetup"]["serverSecret"])
if "--dry" in sys.argv:
    sys.exit(0)
body = (json.dumps(doc, indent=4) + "\n").encode("utf-8")
adb("exec-in", "run-as", PKG, "sh", "-c", "umask 077; cat > cache/device-test-config.json", data=body)
body = b"\0" * len(body)
adb("shell", "am", "broadcast", "-n", f"{PKG}/phpvpn.android.debug.DebugVpnCommandReceiver",
    "-a", "phpvpn.android.debug.CONFIGURE_FILE")
print("pushed and broadcast sent")
