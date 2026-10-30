#!/bin/sh
# Screenshots of the installed macOS tray, taken in the login session of the
# console user (not root) on the CI runner after tests/e2e/macos/run-install-test.sh
# installed the package:
#
#   sh tests/ui/macos/capture-tray.sh <output directory>
#
# The tray is started once per window with the TEST ONLY variables of
# apps/tray-macos/macos_options.h: PHPVPN_TRAY_TEST_OPEN_WINDOW opens the window
# through its menu path against the installed daemon and
# PHPVPN_TRAY_TEST_CAPTURE_DIR makes the tray write PNGs of its status item and of
# every visible window and exit. The PNGs come from the application's own views,
# so no Screen Recording permission is needed; a plain screencapture of the
# whole screen is attempted as well and may be empty without that permission.
# The tray that the launch agent started is stopped first and restarted at the
# end. PASS/FAIL lines, exit 1 on any failure.
set -u

out=${1:-}
[ -n "$out" ] || { echo "usage: sh capture-tray.sh <output directory>" >&2; exit 2; }
[ "$(id -u)" != 0 ] || { echo "capture-tray.sh: run as the console user, not root" >&2; exit 2; }
mkdir -p "$out"
out=$(cd "$out" && pwd -P)
AGENT_LABEL=com.pilandrome.phpvpn-tray
TRAY_BIN="/Applications/phpvpn Tray.app/Contents/MacOS/phpvpn-tray"
uid=$(id -u)

passes=0
failures=0
pass() { passes=$((passes + 1)); echo "PASS: $*"; }
fail() { failures=$((failures + 1)); echo "FAIL: $*"; }

[ -x "$TRAY_BIN" ] || { echo "capture-tray.sh: $TRAY_BIN is not installed" >&2; exit 2; }

# The agent's tray would receive the forwarded arguments of a second launch, so
# it is stopped for the duration of the captures.
if pgrep -x phpvpn-tray > /dev/null 2>&1; then pass "a tray process was running before the captures"; fi
launchctl bootout "gui/$uid/$AGENT_LABEL" > /dev/null 2>&1 || true
pkill -x phpvpn-tray > /dev/null 2>&1 || true
sleep 1
if pgrep -x phpvpn-tray > /dev/null 2>&1; then fail "the previous tray could not be stopped"; fi

capture() {
    name=$1
    window=$2
    log="$out/$name.log"
    if [ -n "$window" ]; then
        PHPVPN_TRAY_TEST_OPEN_WINDOW=$window PHPVPN_TRAY_TEST_CAPTURE_DIR=$out "$TRAY_BIN" > "$log" 2>&1 &
    else
        PHPVPN_TRAY_TEST_CAPTURE_DIR=$out "$TRAY_BIN" > "$log" 2>&1 &
    fi
    pid=$!
    sleep 4
    screencapture -x "$out/screen-$name.png" 2> /dev/null || true
    waited=0
    while kill -0 "$pid" 2> /dev/null && [ "$waited" -lt 25 ]; do waited=$((waited + 1)); sleep 1; done
    if kill -0 "$pid" 2> /dev/null; then
        kill "$pid" 2> /dev/null || true
        fail "$name: the tray did not finish its capture within 25 s"
    else
        wait "$pid" 2> /dev/null
        status=$?
        if [ "$status" -eq 0 ]; then pass "$name: the tray captured and exited 0"; else fail "$name: the tray exited $status (see $name.log)"; fi
    fi
    if [ -f "$out/$name-status-item.png" ]; then pass "$name: status item PNG written"; else fail "$name: status item PNG missing"; fi
    if [ -n "$window" ]; then
        if ls "$out/$name-0-"*.png > /dev/null 2>&1; then
            pass "$name: window PNG written ($(ls "$out/$name-"*.png | wc -l | tr -d ' ') files)"
        else
            fail "$name: no window PNG written"
            sed 's/^/  tray: /' "$log" | tail -n 10
        fi
    fi
}

capture menubar ""
for window in control wizard contracts edit info statistics; do
    capture "$window" "$window"
done

# Restore the production tray of this session.
launchctl bootstrap "gui/$uid" /Library/LaunchAgents/$AGENT_LABEL.plist > /dev/null 2>&1 || true
ls -la "$out"
echo "RESULT: $passes passed, $failures failed"
[ "$failures" -eq 0 ]
