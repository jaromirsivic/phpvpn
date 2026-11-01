"""Checks that no access key or payload key of the relay or the Windows config occurs in the given trees.

The relay file is android.relayServerFile of phpvpn.config.json, the Windows client configuration
%ProgramData%\\phpvpn\\config.json.
"""
import glob, json, os, re, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools"))
import phpvpn_config  # noqa: E402

RELAY_FILE = phpvpn_config.path("android.relayServerFile")
if RELAY_FILE is None:
    sys.exit("set android.relayServerFile in phpvpn.config.json")
win = json.loads((Path(os.environ.get("ProgramData", r"C:\ProgramData")) / "phpvpn" / "config.json").read_text(encoding="utf-8-sig"))
php = RELAY_FILE.read_text(encoding="utf-8")
block = re.search(r"const ACCESS_KEYS = \[(.*?)\];", php, re.S).group(1)
keys = set(re.findall(r"'([0-9a-fA-F]{64})'", block))
keys |= {c["payloadKey"] for c in win["communicationContracts"].values()}
keys.add(win["masterSetup"]["accessKey"])
hits = checked = 0
for root in sys.argv[1:]:
    for p in glob.glob(os.path.join(root, "**", "*"), recursive=True):
        if os.path.isfile(p):
            checked += 1
            data = open(p, "rb").read().decode("latin-1").lower()
            for k in keys:
                if k.lower() in data:
                    hits += 1
                    print("KEY FOUND in", p)
print(f"files checked={checked} keys={len(keys)} hits={hits}")
