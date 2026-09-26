#!/usr/bin/env python3
"""LeakSanitizer gate of the GTK tray checks (development harness, ASan builds).

    lsan_check.py <log>...

Used by every run-*-checks.sh script after the tray (and the mock) ended. Each leak
report block in the logs is attributed to the innermost frame (nearest to the
allocator) that is either PHPVPN code or matches a leak: pattern of lsan.supp:

  PHPVPN leak   a PHPVPN frame (a phpvpn:: symbol or a source file of this client
                tree) comes first: PHPVPN code allocated it directly or through a
                library without its own cache -> FAIL. The entry frames main, _start
                and TrayApp::run only enter the GLib main loop and do not count.
  system cache  a frame matching lsan.supp comes first: the system library allocated
                it for its own exit-time cache, even when PHPVPN code triggered the
                first use (for example the first label measured by pango) -> ignored,
                only counted.
  unknown       neither -> FAIL. Extend lsan.supp only after the stack shows a cache
                owned by a system library.

lsan.supp uses the LeakSanitizer suppression syntax (leak:<pattern>, * wildcard) but is
applied here instead of through LSAN_OPTIONS: a runtime suppression matches any frame
and would also hide a leak that PHPVPN code allocated itself. Exit code 0 when nothing
failed.
"""

from __future__ import annotations

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT = os.path.realpath(os.path.join(HERE, "..", "..", ".."))
FRAME = re.compile(r"^\s+#(\d+) 0x[0-9a-f]+ in (.*)$")
ENTRY = re.compile(r"^(main|_start|__libc_start_\w+|phpvpn::tray::TrayApp::run\(int, char\*\*\)) ")


def patterns(path: str) -> list[tuple[str, re.Pattern]]:
    result = []
    with open(path, encoding="utf-8") as supp:
        for line in supp:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            kind, _, pattern = line.partition(":")
            if kind != "leak" or not pattern:
                raise SystemExit(f"lsan.supp: unsupported line '{line}'")
            result.append((pattern, re.compile(".*".join(re.escape(part) for part in pattern.split("*")))))
    return result


def blocks(text: str) -> list[str]:
    result = []
    for report in text.split("ERROR: LeakSanitizer")[1:]:
        report = report.split("\nSUMMARY: ", 1)[0]
        result += re.split(r"\n(?=(?:Direct|Indirect) leak of )", report)[1:]
    return result


def classify(block: str, supp) -> tuple[str, str | None]:
    frames = []
    for line in block.splitlines():
        match = FRAME.match(line)
        if match:
            frames.append((int(match.group(1)), match.group(2)))
    # Frame #0 is the ASan allocator interceptor.
    for text in (text for number, text in frames if number > 0):
        if ("phpvpn::" in text or CLIENT + "/" in text) and not ENTRY.match(text):
            return "phpvpn", None
        for pattern, expression in supp:
            if expression.search(text):
                return "system", pattern
    return "unknown", None


def main(logs: list[str]) -> int:
    supp = patterns(os.path.join(HERE, "lsan.supp"))
    failed = False
    for log in logs:
        try:
            with open(log, encoding="utf-8", errors="replace") as handle:
                text = handle.read()
        except FileNotFoundError:
            continue
        counts = {"phpvpn": 0, "system": 0, "unknown": 0}
        used: dict[str, int] = {}
        for block in blocks(text):
            kind, pattern = classify(block, supp)
            counts[kind] += 1
            if pattern:
                used[pattern] = used.get(pattern, 0) + 1
            if kind != "system":
                failed = True
                print(f"--- {kind} leak in {os.path.basename(log)}:")
                print("\n".join(block.splitlines()[:40]))
        if sum(counts.values()):
            detail = ", ".join(f"{name} x{count}" for name, count in sorted(used.items()))
            print(f"LeakSanitizer {os.path.basename(log)}: {counts['phpvpn']} PHPVPN, {counts['unknown']} unknown, "
                  f"{counts['system']} system cache leak(s) ignored{': ' + detail if detail else ''}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
