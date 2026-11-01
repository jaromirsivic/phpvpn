"""Summarises a pinger.py log between optional HH:MM:SS bounds: count, timeouts, RTT min/median/p90/max, gaps."""
import re, statistics, sys

path = sys.argv[1]
lo = sys.argv[2] if len(sys.argv) > 2 else "00:00:00"
hi = sys.argv[3] if len(sys.argv) > 3 else "99:99:99"
rows = []
for line in open(path, encoding="utf-8"):
    m = re.match(r"(\d\d:\d\d:\d\d)\.\d+ (rtt=(\d+)|timeout)", line)
    if m and lo <= m.group(1) < hi:
        rows.append((m.group(1), int(m.group(3)) if m.group(3) else None))
rtts = [r for _, r in rows if r is not None]
to = [t for t, r in rows if r is None]
print(f"{lo}-{hi}: pings={len(rows)} replies={len(rtts)} timeouts={len(to)} {('at ' + ','.join(to)) if to else ''}")
if rtts:
    q = sorted(rtts)
    print(f"  rtt ms: min={q[0]} median={statistics.median(q):.0f} p90={q[int(0.9 * (len(q) - 1))]} max={q[-1]} mean={statistics.mean(q):.0f}")
    over = [(t, r) for t, r in rows if r and r > 3500]
    print(f"  replies > 3500 ms: {len(over)} {over[:10]}")
