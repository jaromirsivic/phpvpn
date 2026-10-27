#!/bin/sh
# Development verification of the GTK tray against the mock service (WP09).
#
#   sh run-tray-checks.sh <build-dir> fallback|sni
#
# Runs in a private D-Bus session with a headless mutter (virtual monitor), so it
# needs no unlocked desktop and never touches the user's session bus:
#   fallback  no StatusNotifierWatcher: control window, Connect -> progress window
#             with four checks -> "Connected successfully" countdown -> auto close,
#             Disconnect -> "Disconnected", Exit keeps the (mock) VPN untouched.
#   sni       sni_stub.py owns org.kde.StatusNotifierWatcher: the tray registers an
#             item; IconName, Title and the dbusmenu labels follow the status.
# Needs mutter, python3-gobject and python3-pyatspi. Exit code 0 when every check passed.
set -eu
build=${1:?usage: run-tray-checks.sh <build-dir> fallback|sni}
mode=${2:?usage: run-tray-checks.sh <build-dir> fallback|sni}
here=$(cd "$(dirname "$0")" && pwd)
build=$(cd "$build" && pwd)
# A private runtime directory keeps the Wayland, accessibility bus and dconf
# sockets of this run apart from the desktop session of the same user.
XDG_RUNTIME_DIR=$(mktemp -d /tmp/phpvpn-tray-check.XXXXXX)
chmod 700 "$XDG_RUNTIME_DIR"
export XDG_RUNTIME_DIR here build mode
unset DISPLAY WAYLAND_DISPLAY DBUS_SESSION_BUS_ADDRESS
# No gvfs FUSE mount in the private runtime directory.
export GIO_USE_VFS=local

status=0
dbus-run-session -- sh -eu -c '
work="$XDG_RUNTIME_DIR"
socket="$work/mock/control.sock"
display="phpvpn-check-$$"
pids=""
cleanup() { for pid in $pids; do kill "$pid" 2>/dev/null || true; done; }
trap cleanup EXIT INT TERM
fail() { echo "FAIL: $*"; exit 1; }
pass() { echo "PASS: $*"; }

mutter --headless --no-x11 --virtual-monitor 1280x800 --wayland-display "$display" >"$work/mutter.log" 2>&1 &
pids="$pids $!"
for _ in $(seq 50); do [ -S "$XDG_RUNTIME_DIR/$display" ] && break; sleep 0.1; done
[ -S "$XDG_RUNTIME_DIR/$display" ] || fail "headless mutter did not start"
export WAYLAND_DISPLAY="$display" GDK_BACKEND=wayland
# D-Bus activation of the accessibility bus is not available from every login
# context (SSH), so the launcher is started explicitly inside the private session.
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

if [ "$mode" = sni ]; then
    python3 "$here/sni_stub.py" serve >"$work/sni.log" 2>&1 &
    pids="$pids $!"
    for _ in $(seq 50); do grep -q "owning" "$work/sni.log" 2>/dev/null && break; sleep 0.1; done
fi

mkfifo "$work/control"
sleep 600 >"$work/control" &
pids="$pids $!"
"$build/apps/tray-linux/phpvpn-tray-mock-service" --socket "$socket" --step-ms 600 <"$work/control" >"$work/mock.log" 2>&1 &
pids="$pids $!"
for _ in $(seq 50); do [ -S "$socket" ] && break; sleep 0.1; done
[ -S "$socket" ] || fail "mock service did not start"

PHPVPN_TRAY_TEST_SOCKET="$socket" PHPVPN_TRAY_TEST_SERVER_UID=$(id -u) \
    "$build/apps/tray-linux/phpvpn-tray" >"$work/tray.log" 2>&1 &
tray=$!
pids="$pids $tray"
P="python3 $here/tray_atspi.py"

if [ "$mode" = fallback ]; then
    $P wait PHPVPN "PHPVPN: disconnected" 15 || fail "control window without disconnected status"
    $P dump | tee "$work/control.txt"
    for label in Connect "Configuration Wizard" "Communication contracts" "Edit Config" "Show Server Info" Statistics About Exit; do
        grep -q "button: .$label." "$work/control.txt" || fail "control window lacks $label"
    done
    pass "fallback control window with status and menu buttons"
    $P click PHPVPN About
    for _ in $(seq 50); do
        $P dump >"$work/about.txt" 2>&1
        grep -q "window .About PHPVPN." "$work/about.txt" && break
        sleep 0.2
    done
    grep -q "window .About PHPVPN." "$work/about.txt" || fail "About did not open"
    for text in "PHPVPN version [0-9]" "freeware and open source software" "License: MIT" "www.pilandrome.com"; do
        grep -q "$text" "$work/about.txt" || { cat "$work/about.txt"; fail "About lacks: $text"; }
    done
    $P click "About PHPVPN" OK
    $P absent "About PHPVPN" 5 || fail "OK did not close About"
    pass "About shows the version, licence and copyright and closes with OK"
    $P click PHPVPN Connect
    $P wait "PHPVPN connection" "Connecting client to the server." 10 || fail "no Connect progress window"
    $P wait "PHPVPN connection" "http://127.0.0.1/phpvpn.php" 10 || fail "Server URL detail missing"
    $P wait "PHPVPN connection" "Connected successfully" 20 || fail "no success"
    $P dump | tee "$work/progress.txt"
    [ "$(grep -c "label: .Success." "$work/progress.txt")" -ge 4 ] || fail "four checks not successful"
    $P wait "PHPVPN connection" "Closing in 1 second" 5 || fail "countdown did not advance"
    $P absent "PHPVPN connection" 5 || fail "progress window did not close"
    $P wait PHPVPN "PHPVPN: connected" 10 || fail "status not connected"
    pass "Connect progress, four checks, 3 s countdown and automatic close"
    $P click PHPVPN Disconnect
    $P wait "PHPVPN disconnection" "Disconnected" 15 || fail "no Disconnected"
    $P absent "PHPVPN disconnection" 8 || fail "disconnect window did not close"
    pass "Disconnect progress"
    echo "result http" >"$work/control"
    $P click PHPVPN Connect
    $P wait "PHPVPN connection" "Cannot connect to the server" 15 || fail "no error panel"
    $P wait "PHPVPN connection" "Check the Server URL and network connection, then try again." 3 || fail "no error action"
    $P dump >"$work/error.txt"
    grep -q "button: .Error detail.$" "$work/error.txt" || fail "Error detail button not shown for the HTTP diagnostic"
    grep -q "label: .Failed." "$work/error.txt" || fail "Server URL check not failed"
    sleep 5
    $P wait "PHPVPN connection" "Cannot connect to the server" 1 || fail "error window closed automatically"
    pass "error catalogue text, Error detail button, error window never closes automatically"
    $P close "PHPVPN connection"
    $P absent "PHPVPN connection" 5 || fail "the title bar X did not close the progress window"
    sleep 1
    grep -q "request dismiss-connect-diagnostic" "$work/mock.log" || fail "closing did not dismiss the diagnostic"
    pass "closing the error window sends dismiss-connect-diagnostic"
    echo "result success" >"$work/control"
    echo connect >"$work/control"
    $P wait PHPVPN "PHPVPN: connected" 15 || fail "external connect not shown"
    $P click PHPVPN Exit
    for _ in $(seq 50); do kill -0 "$tray" 2>/dev/null || break; sleep 0.1; done
    kill -0 "$tray" 2>/dev/null && fail "Exit did not end the tray"
    grep -c "request disconnect" "$work/mock.log" | grep -qx 1 || fail "Exit sent a disconnect"
    pass "Exit ends only the tray, the VPN stays connected"
else
    for _ in $(seq 100); do grep -q "registered" "$work/sni.log" && break; sleep 0.1; done
    grep -q "registered" "$work/sni.log" || fail "no StatusNotifierItem registered"
    sleep 1
    python3 "$here/sni_stub.py" inspect | tee "$work/sni1.txt"
    grep -q "IconName: .phpvpn-disconnected." "$work/sni1.txt" || fail "IconName not disconnected"
    grep -q "Title: .PHPVPN: disconnected." "$work/sni1.txt" || fail "Title not the status text"
    grep -q "menu: PHPVPN: disconnected \[insensitive\] | - | Connect | - | Configuration Wizard | Communication contracts | Edit Config | - | Show Server Info | Statistics | About | - | Exit" "$work/sni1.txt" || fail "menu differs"
    pass "SNI item, icon, title and dbusmenu layout"
    echo connect >"$work/control"
    sleep 9
    python3 "$here/sni_stub.py" inspect | tee "$work/sni2.txt"
    grep -q "IconName: .phpvpn-connected." "$work/sni2.txt" || fail "IconName not connected"
    grep -q "menu: PHPVPN: connected \[insensitive\] | - | Disconnect" "$work/sni2.txt" || fail "menu not updated"
    $P dump >"$work/windows.txt" 2>&1 || true
    grep -q "window .PHPVPN." "$work/windows.txt" && fail "control window opened although a watcher exists"
    pass "status change updates icon, title and toggle; no fallback window"
    python3 "$here/sni_stub.py" click Disconnect
    $P wait "PHPVPN disconnection" "Disconnected" 15 || fail "menu Disconnect did not open the progress window"
    grep -q "request disconnect" "$work/mock.log" || fail "menu Disconnect sent no disconnect"
    sleep 1
    python3 "$here/sni_stub.py" inspect | grep -q "IconName: .phpvpn-disconnected." || fail "IconName not back to disconnected"
    pass "dbusmenu activation runs the tray action"
fi
kill "$tray" 2>/dev/null || true
sleep 1
if grep -E "ERROR: AddressSanitizer|runtime error:" "$work/tray.log" "$work/mock.log"; then
    fail "sanitizer report (see above)"
fi
# LeakSanitizer (ASan builds): lsan_check.py ignores only the system library caches
# of lsan.supp; any leak through PHPVPN code fails.
python3 "$here/lsan_check.py" "$work/tray.log" "$work/mock.log" || fail "LeakSanitizer report (see above)"
if grep -q "CRITICAL" "$work/tray.log"; then
    grep "CRITICAL" "$work/tray.log" | sort | uniq -c
    echo "NOTE: GLib criticals in the tray log (see above)"
fi
echo "--- mock requests"; sort "$work/mock.log" | uniq -c
' || status=$?
# Portal and gvfs helpers of the private bus unmount once the bus is gone.
for _ in 1 2 3 4 5; do rm -rf "$XDG_RUNTIME_DIR" 2>/dev/null && break; sleep 1; done
exit "$status"
