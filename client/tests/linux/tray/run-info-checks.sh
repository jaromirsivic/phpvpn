#!/bin/sh
# Development verification of the Server information and Statistics windows of the
# GTK tray against the mock service with mock_server_info.cpp (WP10c).
#
#   sh run-info-checks.sh <build-dir> info|statistics
#
# Same private environment as run-tray-checks.sh: own XDG_RUNTIME_DIR, private
# D-Bus session, headless mutter, own accessibility bus; the fallback control
# window (no StatusNotifierWatcher) opens the windows. Never touches the user's
# desktop session. Needs mutter, python3-gobject and python3-pyatspi.
set -eu
build=${1:?usage: run-info-checks.sh <build-dir> info|statistics}
mode=${2:?usage: run-info-checks.sh <build-dir> info|statistics}
here=$(cd "$(dirname "$0")" && pwd)
build=$(cd "$build" && pwd)
# PHPVPN_CHECK_LOGS=<directory> keeps the tray log of the run (sanitizer reports).
export PHPVPN_CHECK_LOGS="${PHPVPN_CHECK_LOGS:-}"
XDG_RUNTIME_DIR=$(mktemp -d /tmp/phpvpn-info-check.XXXXXX)
chmod 700 "$XDG_RUNTIME_DIR"
export XDG_RUNTIME_DIR here build mode
unset DISPLAY WAYLAND_DISPLAY DBUS_SESSION_BUS_ADDRESS
export GIO_USE_VFS=local

status=0
dbus-run-session -- sh -eu -c '
work="$XDG_RUNTIME_DIR"
socket="$work/mock/control.sock"
display="phpvpn-info-$$"
pids=""
cleanup() { for pid in $pids; do kill "$pid" 2>/dev/null || true; done; }
trap cleanup EXIT INT TERM
keep() { [ -n "${PHPVPN_CHECK_LOGS:-}" ] && cp "$work/tray.log" "$PHPVPN_CHECK_LOGS/tray-$mode.log" 2>/dev/null || true; }
fail() { echo "FAIL: $*"; echo "--- mock requests"; sort "$work/mock.log" | uniq -c; echo "--- tray log"; tail -20 "$work/tray.log"; keep; exit 1; }
pass() { echo "PASS: $*"; }
count() { grep -c "^request $1\$" "$work/mock.log" || true; }
send() { echo "$1" >"$work/control"; sleep 0.2; }

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

mkfifo "$work/control"
sleep 900 >"$work/control" &
pids="$pids $!"
if [ "$mode" = info ]; then args="--info-rows 45"; else args="--counters large"; fi
"$build/apps/tray-linux/phpvpn-tray-mock-service" --socket "$socket" --step-ms 300 $args <"$work/control" >"$work/mock.log" 2>&1 &
pids="$pids $!"
for _ in $(seq 50); do [ -S "$socket" ] && break; sleep 0.1; done
[ -S "$socket" ] || fail "mock service did not start"
[ "$mode" = statistics ] && send "status fail"

PHPVPN_TRAY_TEST_SOCKET="$socket" PHPVPN_TRAY_TEST_SERVER_UID=$(id -u) \
    "$build/apps/tray-linux/phpvpn-tray" >"$work/tray.log" 2>&1 &
tray=$!
pids="$pids $tray"
P="python3 $here/tray_atspi.py"
I="python3 $here/info_atspi.py"

if [ "$mode" = info ]; then
    $P wait PHPVPN "PHPVPN: disconnected" 15 || fail "control window missing"
    $P click PHPVPN "Show Server Info"
    W=$($I title "Server information (" 15) || fail "Server information did not open"
    [ "$W" = "Server information (30 online clients, 15 valid contracts)" ] || fail "title: $W"
    $I dump >"$work/info.txt"
    for line in "page tab: .Who is online. \[selected\]" "page tab: .Raw server information." \
                "label: .Server information." "label: .(30 online clients, 15 valid contracts)." \
                "button: .Refresh." "check box: .Auto refresh.$" "button: .OK." \
                "button: .First page. \[insensitive\]" "button: .Previous page. \[insensitive\]" \
                "button: .Page 1 of 3, current page." "button: .Page 3 of 3." \
                "button: .Next page.$" "button: .Last page.$"; do
        grep -q "$line" "$work/info.txt" || { cat "$work/info.txt"; fail "Server information lacks: $line"; }
    done
    r=$(grep -n "button: .Refresh.$" "$work/info.txt" | head -1 | cut -d: -f1)
    a=$(grep -n "check box: .Auto refresh.$" "$work/info.txt" | head -1 | cut -d: -f1)
    o=$(grep -n "button: .OK.$" "$work/info.txt" | head -1 | cut -d: -f1)
    [ "$r" -lt "$a" ] && [ "$a" -lt "$o" ] || fail "accessible footer order must be Refresh, Auto refresh, OK"
    $I table "$W" >"$work/table.txt"
    grep -q "^header: Contract | Last seen | Status$" "$work/table.txt" || fail "table header"
    grep -q "^rows: 20$" "$work/table.txt" || fail "first page must hold 20 rows"
    grep -q "^row: 10.99.99.2_10.99.99.3 | .* | Online - valid communication contract$" "$work/table.txt" || fail "first row"
    grep -q "^row: .* | Online - invalid communication contract$" "$work/table.txt" || fail "invalid rows"
    [ "$(count get-server-info)" = 1 ] || fail "open must send exactly one get-server-info"
    pass "Show Info: one get-server-info, verified title, count, tabs, table, pager and footer"
    $I tab "$W" "Raw server information"
    $I text "$W" "Raw server information" >"$work/raw.txt"
    grep -q "^    \"serverVersion\": \"1.0.0-synthetic\",$" "$work/raw.txt" || fail "raw plaintext not the pretty server JSON"
    grep -q "\"maskedCommunicationContracts\"" "$work/raw.txt" || fail "raw plaintext lacks masked contracts"
    [ "$(wc -l <"$work/raw.txt")" -gt 100 ] || fail "raw plaintext is not multi-line"
    $I tab "$W" "Who is online"
    $P click "$W" "Last page"
    $I rows "$W" 5 5 || fail "last page must hold 5 rows"
    $P click PHPVPN "Show Server Info"
    sleep 1
    [ "$(count get-server-info)" = 1 ] || fail "tabs, paging or reactivation sent IPC"
    pass "raw exact multi-line plaintext; tabs, pager and reactivation are local only"
    send "info error"
    $P click "$W" Refresh
    $I anytext "Refresh failed. Previous information is retained." 10 || fail "failed Refresh notice"
    $I rows "$W" 5 1 || fail "failed Refresh replaced the snapshot"
    [ "$($I title "Server information (" 1)" = "$W" ] || fail "failed Refresh changed the title"
    send "info ok"
    send "info rows 1"
    $P click "$W" Refresh
    W=$($I title "Server information (1 " 10) || fail "successful Refresh not installed"
    [ "$W" = "Server information (1 online client, 1 valid contract)" ] || fail "singular title: $W"
    $I rows "$W" 1 5 || fail "refreshed table"
    pass "Refresh failure keeps the snapshot with the notice; success replaces title, table and raw"
    send "info delay 2500"
    $P click "$W" Refresh
    $I sensitive "$W" Refresh no 2 || fail "Refresh stays enabled while pending"
    $I anytext "Loading..." 2 || fail "no Loading... notice"
    $I sensitive "$W" Refresh yes 6 || fail "Refresh not re-enabled"
    pass "Refresh disabled with Loading... during the request"
    send "info delay 0"
    $I checked "$W" "Auto refresh" && fail "Auto refresh must start off"
    n=$(count get-server-info)
    $I toggle "$W" "Auto refresh"
    sleep 4.3
    [ "$(count get-server-info)" = "$n" ] || fail "Auto refresh fired before 5 s"
    sleep 1.5
    [ "$(count get-server-info)" = $((n + 1)) ] || fail "Auto refresh did not fire after 5 s"
    $I tab "$W" "Raw server information"
    sleep 5.2
    [ "$(count get-server-info)" = $((n + 2)) ] || fail "Auto refresh did not repeat on the raw tab"
    $I toggle "$W" "Auto refresh"
    sleep 6
    [ "$(count get-server-info)" = $((n + 2)) ] || fail "switching Auto refresh off did not stop it"
    $I toggle "$W" "Auto refresh"
    $P click PHPVPN "Show Server Info"
    sleep 0.5
    $I checked "$W" "Auto refresh" && fail "re-choosing Show Info must switch Auto refresh off"
    sleep 5.5
    [ "$(count get-server-info)" = $((n + 2)) ] || fail "Auto refresh continued after Show Info"
    pass "Auto refresh: off at open, 5 s after completion on both tabs, off by toggle and by Show Info"
    $I tab "$W" "Who is online"
    $P click "$W" OK
    $P absent "$W" 5 || fail "OK did not close"
    send "info error"
    n=$(count get-server-info)
    $P click PHPVPN "Show Server Info"
    $I anytext "Server information could not be verified. Check the service, configuration and server compatibility." 10 \
        || fail "first-load failure message"
    $I title "Server information" 1 >/dev/null && fail "an unverified snapshot was shown"
    [ "$(count get-server-info)" = $((n + 1)) ] || fail "failure path request count"
    $I dismiss "Server information could not be verified. Check the service, configuration and server compatibility."
    pass "first-load failure shows the Windows error and no window"
    send "info ok"
    $P click PHPVPN "Show Server Info"
    W=$($I title "Server information (" 10) || fail "reopen"
    $P close "$W"
    $P absent "$W" 5 || fail "title bar X did not close"
    pass "reopen after an error, title bar close"
else
    $P wait PHPVPN "PHPVPN: service status unavailable" 15 || fail "control window missing"
    $P click PHPVPN Statistics
    $I title "PHPVPN Statistics" 10 >/dev/null || fail "Statistics did not open"
    [ "$(count get-config-state)" = 1 ] || fail "Statistics must perform one get-config-state"
    $P wait "PHPVPN Statistics" "Waiting for service status" 3 || fail "no waiting text"
    $I field "PHPVPN Statistics" "Empty polls" "-" 2 || fail "no placeholder dash"
    $I fields "PHPVPN Statistics" >"$work/fields0.txt"
    [ "$(grep -c " = -$" "$work/fields0.txt")" = 10 ] || fail "ten placeholders expected"
    pass "one get-config-state, dashes and Waiting for service status before the first snapshot"
    send "status ok"
    $I field "PHPVPN Statistics" "Empty polls" "9,007,199,254,740,993" 8 || fail "first snapshot not shown"
    $I fields "PHPVPN Statistics" >"$work/fields1.txt"
    cat >"$work/expected.txt" <<EOF
Empty polls = 9,007,199,254,740,993
Polls with payload = 18,437,736,874,454,810,622
Total polls = 18,446,744,073,709,551,615
Request body = 18446.74 PB
Response body = 18446.74 PB
Total size = 36893.49 PB
Packets sent = 9,223,372,036,854,775,807
Bytes sent = 999 B
Packets received = 9,223,372,036,854,775,808
Bytes received = 1.00 MB
EOF
    diff "$work/expected.txt" "$work/fields1.txt" || fail "values or tab order differ"
    $I dump >"$work/statistics.txt"
    for line in "label: .Statistics." "label: .(Disconnected)." \
                "label: .Read-only global service counters for the current VPN connection.." \
                "label: .Data polling." "label: .Serialized data." "label: .IPv4 traffic."; do
        grep -q "$line" "$work/statistics.txt" || { cat "$work/statistics.txt"; fail "Statistics lacks: $line"; }
    done
    grep -q "Waiting for service status" "$work/statistics.txt" && ! grep -q "Waiting for service status.*hidden" "$work/statistics.txt" \
        && fail "waiting text still shown"
    pass "ten exact uint64 values (2^53+1 .. UINT64_MAX, carry sum, B/MB) in the UX07 order through get-status JSON"
    n=$(count get-status)
    sleep 11
    d=$(( $(count get-status) - n ))
    [ "$d" -ge 2 ] && [ "$d" -le 3 ] || fail "Statistics changed the status polling ($d reads in 11 s)"
    pass "no own polling: $d get-status in 11 s (five-second tray timer)"
    send "status fail"
    $P wait "PHPVPN Statistics" "Service status unavailable. Showing the last received values." 8 || fail "no stale text"
    $I field "PHPVPN Statistics" "Total size" "36893.49 PB" 1 || fail "stale read dropped the values"
    pass "failed read keeps the whole last snapshot with the stale text"
    send "status ok"
    send "connect"
    $P wait PHPVPN "PHPVPN: connected" 15 || fail "not connected"
    sleep 1.5
    $I dump >"$work/connected.txt"
    grep -q "label: .(Connected for 0d 0h 0m [0-9]*s)." "$work/connected.txt" || fail "connection duration missing"
    grep -q "Service status unavailable" "$work/connected.txt" && ! grep -q "Service status unavailable.*hidden" "$work/connected.txt" \
        && fail "stale text still shown"
    pass "connected duration and fresh status after recovery"
    $P close "PHPVPN Statistics"
    $P absent "PHPVPN Statistics" 5 || fail "title bar X did not close"
    [ "$(count disconnect)" = 0 ] || fail "closing Statistics sent disconnect"
    $P click PHPVPN Statistics
    $I field "PHPVPN Statistics" "Bytes received" "1.00 MB" 3 || fail "reopen without the last snapshot"
    [ "$(count get-config-state)" = 2 ] || fail "reopen must check the configuration again"
    pass "close only closes the window; reopen checks the configuration and shows the last snapshot at once"
fi
kill "$tray" 2>/dev/null || true
sleep 1
keep
if grep -E "ERROR: AddressSanitizer|runtime error:" "$work/tray.log" "$work/mock.log"; then
    fail "sanitizer report (see above)"
fi
# LeakSanitizer (ASan builds): lsan_check.py ignores only the system library caches
# of lsan.supp; any leak through PHPVPN code fails.
python3 "$here/lsan_check.py" "$work/tray.log" "$work/mock.log" || fail "LeakSanitizer report (see above)"
if grep -q -E "CRITICAL|WARNING" "$work/tray.log"; then
    grep -E "CRITICAL|WARNING" "$work/tray.log" | sort | uniq -c
    echo "NOTE: GLib criticals/warnings in the tray log (see above)"
fi
echo "--- mock requests"; grep "^request" "$work/mock.log" | sort | uniq -c
' || status=$?
for _ in 1 2 3 4 5; do rm -rf "$XDG_RUNTIME_DIR" 2>/dev/null && break; sleep 1; done
exit "$status"
