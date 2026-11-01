"""Checks that no access key or payload key of the relay or the Windows config occurs in the given trees."""
import glob, json, os, re, sys

win = json.loads(open(r"C:\ProgramData\phpvpn\config.json", encoding="utf-8-sig").read())
php = open(r"D:\nginx\html\phpvpn.php", encoding="utf-8").read()
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
