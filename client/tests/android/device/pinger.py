"""Host -> phone ping every INTERVAL s for DURATION s: logs UTC time, RTT or timeout (Windows ping, 20 s wait).

Usage: python pinger.py LOG DURATION_S [INTERVAL_S] [TARGET]
"""
import datetime, re, subprocess, sys, time

log, duration = sys.argv[1], float(sys.argv[2])
interval = float(sys.argv[3]) if len(sys.argv) > 3 else 10.0
target = sys.argv[4] if len(sys.argv) > 4 else "10.99.99.5"
end = time.monotonic() + duration
with open(log, "a", buffering=1, encoding="utf-8") as f:
    f.write(f"# start {datetime.datetime.now(datetime.timezone.utc):%Y-%m-%dT%H:%M:%S}Z target {target} interval {interval}\n")
    while time.monotonic() < end:
        t0 = time.monotonic()
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%H:%M:%S.%f")[:-3]
        out = subprocess.run(["ping", "-n", "1", "-w", "20000", target], capture_output=True, text=True, errors="replace").stdout
        wall = (time.monotonic() - t0) * 1000
        m = re.search(r"time[=<](\d+)ms", out)
        f.write(f"{stamp} {'rtt=' + m.group(1) if m else 'timeout'} wall={wall:.0f}\n")
        rest = interval - (time.monotonic() - t0)
        if rest > 0:
            time.sleep(rest)
    f.write(f"# end {datetime.datetime.now(datetime.timezone.utc):%Y-%m-%dT%H:%M:%S}Z\n")
