#!/bin/sh
# Check of the installed phpvpn-tray package on a Fedora desktop system
# (plan-v001 5/WP11). Run as the desktop user (sudo without a password for the
# SELinux queries) after install.sh installed phpvpn and phpvpn-tray:
#
#   sh tests/e2e/linux/installed-tray-check.sh [<install-time for ausearch>]
#
# Checks the installed files, rpm -V, desktop-file-validate, the SELinux labels
# (restorecon -n finds nothing to relabel, no AVC since <install-time>), then
# starts /usr/bin/phpvpn-tray against the installed service in a private D-Bus
# session with a headless mutter (the desktop session of the user is never
# touched): without a StatusNotifierWatcher the control window must show
# "PHPVPN: disconnected" (AT-SPI), the installed icon theme must resolve the
# state icons, and a second start with --show-window must forward and exit.
# The tray never presses Connect; it is ended with SIGTERM and the service
# state and invocation are compared before and after. Needs mutter,
# python3-gobject, python3-pyatspi and desktop-file-utils.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
atspi=$here/../../linux/tray/tray_atspi.py
since=${1:-today}

passes=0
failures=0
pass() { passes=$((passes + 1)); echo "PASS: $*"; }
fail() { failures=$((failures + 1)); echo "FAIL: $*"; }
check() {
    message=$1
    shift
    if sh -c "$1"; then pass "$message"; else fail "$message"; fi
}

# -------------------------------------------------------------------- files
check "phpvpn-tray package installed with the phpvpn release" \
    'test "$(rpm -q --qf "%{VERSION}-%{RELEASE}" phpvpn-tray)" = "$(rpm -q --qf "%{VERSION}-%{RELEASE}" phpvpn)"'
check "rpm -V phpvpn-tray reports no change" 'test -z "$(rpm -V phpvpn-tray)"'
check "rpm -V phpvpn reports no change" 'test -z "$(rpm -V phpvpn)"'
check "/usr/bin/phpvpn-tray" 'test -x /usr/bin/phpvpn-tray'
check "/etc/xdg/autostart/phpvpn-tray.desktop starts the tray without arguments" \
    'grep -qx "Exec=phpvpn-tray" /etc/xdg/autostart/phpvpn-tray.desktop'
check "/usr/share/applications/phpvpn-tray.desktop runs --show-window" \
    'grep -qx "Exec=phpvpn-tray --show-window" /usr/share/applications/phpvpn-tray.desktop'
check "desktop-file-validate of both entries" \
    'desktop-file-validate /etc/xdg/autostart/phpvpn-tray.desktop && desktop-file-validate /usr/share/applications/phpvpn-tray.desktop'
check "28 hicolor icons" \
    'test "$(ls /usr/share/icons/hicolor/*/apps/phpvpn.png /usr/share/icons/hicolor/*/apps/phpvpn-*.png | wc -l)" = 28'
check "4 scalable hicolor icons" 'test "$(ls /usr/share/icons/hicolor/scalable/apps/phpvpn*.svg | wc -l)" = 4'
check "the hicolor icon cache lists the tray icons (hicolor-icon-theme file trigger)" \
    'grep -qa phpvpn-connected /usr/share/icons/hicolor/icon-theme.cache'
check "phpvpn-tray(1)" 'man -w phpvpn-tray > /dev/null'
check "phpvpn-tray is MIT only, no third-party icon licence" \
    'test "$(rpm -q --qf "%{LICENSE}" phpvpn-tray)" = MIT && test ! -e /usr/share/licenses/phpvpn-tray'
check "ldd resolves every library of phpvpn-tray" '! ldd /usr/bin/phpvpn-tray | grep -q "not found"'
check "the phpvpn package has no graphical dependency" '! rpm -q --requires phpvpn | grep -Eiq "gtk|gdk|ayatana|libX|wayland"'

# ------------------------------------------------------------------ SELinux
if command -v getenforce > /dev/null 2>&1; then
    check "SELinux enforcing" 'test "$(getenforce)" = Enforcing'
    check "restorecon -n finds nothing to relabel in the tray files" \
        'test -z "$(sudo restorecon -nvR /usr/bin/phpvpn-tray /etc/xdg/autostart/phpvpn-tray.desktop /usr/share/applications/phpvpn-tray.desktop $(ls /usr/share/icons/hicolor/*/apps/phpvpn*.png /usr/share/icons/hicolor/scalable/apps/phpvpn*.svg) 2>&1)"'
    ls -Z /usr/bin/phpvpn-tray /etc/xdg/autostart/phpvpn-tray.desktop /usr/share/applications/phpvpn-tray.desktop
fi

# ------------------------------------------------- start in a private session
trays_before=$(pgrep -u "$(id -u)" -x phpvpn-tray | sort || true)
state_before=$(phpvpn status | grep '"state"' || true)
invocation_before=$(systemctl show -p InvocationID --value phpvpn.service)
XDG_RUNTIME_DIR=$(mktemp -d /tmp/phpvpn-installed-tray.XXXXXX)
chmod 700 "$XDG_RUNTIME_DIR"
export XDG_RUNTIME_DIR atspi
unset DISPLAY WAYLAND_DISPLAY DBUS_SESSION_BUS_ADDRESS
# No test endpoint: the tray must use /run/phpvpn/control-v1.sock with a root peer.
unset PHPVPN_TRAY_TEST_SOCKET PHPVPN_TRAY_TEST_SERVER_UID PHPVPN_TRAY_ICON_THEME_PATH
export GIO_USE_VFS=local GTK_USE_PORTAL=0

session=0
dbus-run-session -- sh -eu -c '
work="$XDG_RUNTIME_DIR"
display="phpvpn-installed-$$"
pids=""
cleanup() { for pid in $pids; do kill "$pid" 2>/dev/null || true; done; }
trap cleanup EXIT INT TERM
fail() { echo "FAIL: $*"; exit 1; }
pass() { echo "PASS: $*"; }
mutter --headless --no-x11 --virtual-monitor 1024x768 --wayland-display "$display" >"$work/mutter.log" 2>&1 &
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

python3 -c "
import gi
gi.require_version(\"Gtk\", \"3.0\")
from gi.repository import Gtk
theme = Gtk.IconTheme.get_default()
missing = [n for n in (\"phpvpn\", \"phpvpn-disconnected\", \"phpvpn-connecting\", \"phpvpn-connected\")
           if not all(theme.lookup_icon(n, s, 0) for s in (16, 22, 24, 32, 48))]
raise SystemExit(\"missing icons: %s\" % missing if missing else 0)
" || fail "the installed icon theme does not resolve the tray icons"
pass "the installed hicolor theme resolves phpvpn, phpvpn-disconnected, -connecting, -connected"

/usr/bin/phpvpn-tray >"$work/tray.log" 2>&1 &
tray=$!
pids="$pids $tray"
python3 "$atspi" wait PHPVPN "PHPVPN: disconnected" 20 || { cat "$work/tray.log"; fail "control window without the disconnected status of the installed service"; }
python3 "$atspi" dump >"$work/control.txt"
grep -q "button: .Connect." "$work/control.txt" || fail "control window lacks Connect"
pass "installed tray reads the installed service: control window shows PHPVPN: disconnected"
timeout 10 /usr/bin/phpvpn-tray --show-window >"$work/second.log" 2>&1 || { cat "$work/second.log"; fail "second start with --show-window failed"; }
kill -0 "$tray" || fail "primary tray ended after the second start"
pass "second start (--show-window) forwarded to the running instance and exited"
kill -TERM "$tray"
for _ in $(seq 50); do kill -0 "$tray" 2>/dev/null || break; sleep 0.1; done
kill -0 "$tray" 2>/dev/null && fail "the tray did not end on SIGTERM"
pass "the tray ends on SIGTERM"
if grep -q "CRITICAL" "$work/tray.log"; then
    grep "CRITICAL" "$work/tray.log" | sort | uniq -c
    echo "NOTE: GLib criticals in the tray log (see above)"
fi
' || session=$?
# Portal and gvfs helpers of the private bus unmount once the bus is gone.
for _ in 1 2 3 4 5; do rm -rf "$XDG_RUNTIME_DIR" 2>/dev/null && break; sleep 1; done
if [ "$session" = 0 ]; then pass "private session checks"; else fail "private session checks ($session)"; fi

trays_after=$(pgrep -u "$(id -u)" -x phpvpn-tray | sort || true)
if [ "$trays_before" = "$trays_after" ]; then pass "no phpvpn-tray process is left behind"; else fail "phpvpn-tray processes: '$trays_before' -> '$trays_after'"; fi
state_after=$(phpvpn status | grep '"state"' || true)
invocation_after=$(systemctl show -p InvocationID --value phpvpn.service)
if [ "$state_before" = "$state_after" ] && [ "$invocation_before" = "$invocation_after" ]; then
    pass "the service state ($(echo $state_after)) and invocation are unchanged"
else
    fail "the service changed: '$state_before' -> '$state_after', $invocation_before -> $invocation_after"
fi
if command -v ausearch > /dev/null 2>&1; then
    avc=$(sudo ausearch -m AVC,USER_AVC,SELINUX_ERR -ts $since 2>&1 | grep -E "phpvpn" || true)
    if [ -z "$avc" ]; then pass "no phpvpn AVC since $since"; else fail "AVC: $avc"; fi
fi

echo "passes=$passes failures=$failures"
[ "$failures" = 0 ]
