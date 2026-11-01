"""Builds the phone's configuration from the relay and the Windows config without printing any secret.

Usage: python push_config.py [--dry]
The document goes to the app's cacheDir through `adb exec-in run-as` (no file on shared storage), the
debug receiver stores it with set-config and deletes it. Only key names and lengths are printed.
"""
import json, re, subprocess, sys

ADB = r"C:\Users\User\AppData\Local\Android\Sdk\platform-tools\adb.exe"
SERIAL = "64090DLCR000R5"
PKG = "io.github.jaromirsivic.phpvpn"
URL = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1].startswith("http") else "http://192.168.68.53/phpvpn.php"
MY_IP = "10.99.99.5"
PEER_IP = "10.99.99.2"


def adb(*args, data=None):
    return subprocess.run([ADB, "-s", SERIAL, *args], input=data, capture_output=True, check=True).stdout


default = json.loads(adb("exec-out", "run-as", PKG, "cat", "no_backup/phpvpn/config/config.json"))
win = json.loads(open(r"C:\ProgramData\phpvpn\config.json", encoding="utf-8-sig").read())
php = open(r"D:\nginx\html\phpvpn.php", encoding="utf-8").read()
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
