#!/bin/sh
# Development verification of the Edit Config, Communication contracts and Password
# SHA windows of the GTK tray (WP10 part b) against the mock service with its real
# ConfigStore document store (mock_config.cpp).
#
#   sh run-config-checks.sh <build-dir> [editor] [deadline] [contracts]
#
# Default groups: editor deadline contracts (in this order: the editor group creates
# the configuration, contracts ends with Exit). Runs in a private D-Bus session with
# a headless mutter and a private accessibility bus, like run-tray-checks.sh, so it
# never touches the user's desktop session. Needs mutter, python3-gobject and
# python3-pyatspi. Exit code 0 when every check passed.
set -eu
build=${1:?usage: run-config-checks.sh <build-dir> [editor] [deadline] [contracts]}
shift
groups=${*:-editor deadline contracts}
here=$(cd "$(dirname "$0")" && pwd)
build=$(cd "$build" && pwd)
XDG_RUNTIME_DIR=$(mktemp -d /tmp/phpvpn-config-check.XXXXXX)
chmod 700 "$XDG_RUNTIME_DIR"
export XDG_RUNTIME_DIR here build groups
unset DISPLAY WAYLAND_DISPLAY DBUS_SESSION_BUS_ADDRESS
export GIO_USE_VFS=local

status=0
dbus-run-session -- sh -eu -c '
work="$XDG_RUNTIME_DIR"
socket="$work/mock/control.sock"
display="phpvpn-config-$$"
pids=""
cleanup() { for pid in $pids; do kill "$pid" 2>/dev/null || true; done; }
trap cleanup EXIT INT TERM
fail() { echo "FAIL: $*"; exit 1; }

mutter --headless --no-x11 --virtual-monitor 1280x800 --wayland-display "$display" >"$work/mutter.log" 2>&1 &
pids="$pids $!"
for _ in $(seq 50); do [ -S "$XDG_RUNTIME_DIR/$display" ] && break; sleep 0.1; done
[ -S "$XDG_RUNTIME_DIR/$display" ] || fail "headless mutter did not start"
export WAYLAND_DISPLAY="$display" GDK_BACKEND=wayland
/usr/libexec/at-spi-bus-launcher --launch-immediately >"$work/atspi.log" 2>&1 &
pids="$pids $!"
for _ in $(seq 50); do
    gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
        --method org.freedesktop.DBus.NameHasOwner org.a11y.Bus 2>/dev/null | grep -q true && break
    sleep 0.1
done
/usr/libexec/at-spi2-registryd >"$work/registry.log" 2>&1 &
pids="$pids $!"
sleep 0.5

# The document store directory: owned by this user, not writable by others.
mkdir -m 700 "$work/config"
mkfifo "$work/control"
sleep 3600 >"$work/control" &
pids="$pids $!"
"$build/apps/tray-linux/phpvpn-tray-mock-service" --socket "$socket" --step-ms 300 --config-dir "$work/config" \
    <"$work/control" >"$work/mock.log" 2>&1 &
pids="$pids $!"
for _ in $(seq 50); do [ -S "$socket" ] && break; sleep 0.1; done
[ -S "$socket" ] || fail "mock service did not start"

PHPVPN_TRAY_TEST_SOCKET="$socket" PHPVPN_TRAY_TEST_SERVER_UID=$(id -u) \
    "$build/apps/tray-linux/phpvpn-tray" >"$work/tray.log" 2>&1 &
tray=$!
pids="$pids $tray"
python3 "$here/tray_atspi.py" wait PHPVPN "PHPVPN: disconnected" 15 >/dev/null || fail "control window did not appear"

result=0
PHPVPN_CHECK_CONFIG="$work/config/config.json" PHPVPN_CHECK_CONTROL="$work/control" \
PHPVPN_CHECK_MOCKLOG="$work/mock.log" PHPVPN_CHECK_TRAYPID="$tray" \
    python3 "$here/config_checks.py" $groups || result=$?
if [ "$result" -ne 0 ]; then
    python3 "$here/tray_atspi.py" dump 2>&1 | head -120 || true
fi
kill "$tray" 2>/dev/null || true
sleep 1
if grep -qE "ERROR: AddressSanitizer|runtime error:" "$work/tray.log" "$work/mock.log"; then
    grep -hE -A60 "ERROR: AddressSanitizer|runtime error:" "$work/tray.log" "$work/mock.log" | head -150
    echo "FAIL: sanitizer report (see above)"
    result=1
fi
# LeakSanitizer: a leak whose allocation stack passes through the tray or mock
# binaries fails; leaks allocated purely inside system libraries (fontconfig and
# pango caches of GTK) are only reported.
for log in "$work/tray.log" "$work/mock.log"; do
    grep -q "ERROR: LeakSanitizer" "$log" || continue
    python3 - "$log" "$build" <<"PYEOF" || result=1
import re, sys
text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
blocks = re.split(r"\n(?=(?:Direct|Indirect) leak of )", text.split("ERROR: LeakSanitizer", 1)[1])[1:]
own, foreign = [], 0
for block in blocks:
    frames = [line for line in block.splitlines() if re.match(r"\s+#\d+ ", line)]
    # main() and TrayApp::run only enter the GLib main loop; they do not make an
    # allocation made by GTK itself (portal, fontconfig) a PHPVPN allocation.
    frames = [line for line in frames[1:] if not re.search(r" in (main|_start|phpvpn::tray::TrayApp::run\(int, char\*\*\)) ", line)]
    if any(sys.argv[2] in line or "phpvpn" in line for line in frames):
        own.append(block[:3000])
    else:
        foreign += 1
for block in own:
    print(block)
name = sys.argv[1].rsplit("/", 1)[-1]
print(f"LeakSanitizer {name}: {len(own)} leak(s) from PHPVPN code, {foreign} inside system libraries only")
sys.exit(1 if own else 0)
PYEOF
done
if grep -qE "CRITICAL|WARNING" "$work/tray.log"; then
    grep -E "CRITICAL|WARNING" "$work/tray.log" | sort | uniq -c
    echo "NOTE: GLib criticals/warnings in the tray log (see above)"
fi
echo "--- mock requests"; grep "^request" "$work/mock.log" | sort | uniq -c
exit "$result"
' || status=$?
for _ in 1 2 3 4 5; do rm -rf "$XDG_RUNTIME_DIR" 2>/dev/null && break; sleep 1; done
[ "$status" -eq 0 ] && echo "RESULT: all config window checks passed" || echo "RESULT: FAILED ($status)"
exit "$status"
