#!/bin/bash
# Development verification of the Configuration Wizard windows of the GTK tray (WP10a).
#
#   bash run-wizard-checks.sh <build-dir> wizard|compact|detail
#
#   wizard  a REAL isolated phpvpn-service instance (--config-dir/--state-dir/
#           --runtime-dir under /run/phpvpn-wp10a) in the private network namespace
#           phpvpn-wp10a-svc, and the real Python relay phpvpn.py in
#           phpvpn-wp10a-relay (192.168.91.1:18096, memory storage). The tray talks
#           to the instance through PHPVPN_TRAY_TEST_SOCKET. Covers every Wizard
#           step with real checks, HTTP headers, the Wizard HTTP error detail,
#           Finish with the receipt, the disconnect gate, Reset, keepalive and close.
#           Needs sudo for the namespaces and the service only; the GUI runs as
#           the calling user. The main network namespace is never changed.
#   compact the same real service on a 700 x 560 virtual monitor: the window is
#           constrained below 760 DIP, so the sidebar becomes the compact strip of
#           numbers and Reset moves to its own footer row; a real URL check passes.
#   detail  the scripted mock service (phpvpn-tray-mock-service): a failed Connect
#           with an HTTP diagnostic opens the HTTP error detail from the progress
#           window.
#
# Both modes run the tray in a private D-Bus session with a headless mutter, like
# run-tray-checks.sh, and verify it through AT-SPI (wizard_atspi.py). Needs mutter,
# python3-gobject, python3-pyatspi; wizard mode also the relay requirements in
# PHPVPN_SERVER_PYTHON (default ~/phpvpn-wp06-venv/bin/python). Exit 0 when every
# check passed.
set -euo pipefail
build=${1:?usage: run-wizard-checks.sh <build-dir> wizard|compact|detail}
mode=${2:?usage: run-wizard-checks.sh <build-dir> wizard|compact|detail}
here=$(cd "$(dirname "$0")" && pwd)
build=$(cd "$build" && pwd)
repository=$(cd "$here/../../../.." && pwd)
work=${PHPVPN_WIZARD_WORK:-$repository/work/wizard-checks-$mode-$(date +%Y%m%d-%H%M%S)}
python_server=${PHPVPN_SERVER_PYTHON:-$HOME/phpvpn-wp06-venv/bin/python}
mkdir -p "$work"
echo "work directory: $work"

ns_relay=phpvpn-wp10a-relay
ns_svc=phpvpn-wp10a-svc
base=/run/phpvpn-wp10a
relay_ip=192.168.91.1
svc_ip=192.168.91.2
relay_port=18096
relay_url="http://$relay_ip:$relay_port/phpvpn.php"

root_down() {
    set +e
    for file in "$work"/service.pid "$work"/relay.pid; do
        [ -f "$file" ] || continue
        pid=$(cat "$file")
        sudo kill -TERM "$pid" 2>/dev/null
        for _ in $(seq 1 60); do sudo kill -0 "$pid" 2>/dev/null || break; sleep 0.5; done
        sudo kill -KILL "$pid" 2>/dev/null
        rm -f "$file"
    done
    sudo ip netns del "$ns_svc" 2>/dev/null
    sudo ip netns del "$ns_relay" 2>/dev/null
    sudo rm -rf "$base"
}

root_up() {
    for ns in $ns_relay $ns_svc; do
        ! ip netns list | grep -qw "$ns" || { echo "FATAL: network namespace $ns exists"; exit 1; }
    done
    [ ! -e "$base" ] || { echo "FATAL: $base exists"; exit 1; }
    "$python_server" -c 'import uvicorn, cryptography' 2>/dev/null || { echo "FATAL: $python_server lacks the relay requirements"; exit 1; }
    for ns in $ns_relay $ns_svc; do sudo ip netns add "$ns"; sudo ip -n "$ns" link set lo up; done
    sudo ip -n $ns_relay link add r0 type veth peer name eth0 netns $ns_svc
    sudo ip -n $ns_relay addr add $relay_ip/24 dev r0
    sudo ip -n $ns_relay link set r0 up
    sudo ip -n $ns_svc addr add $svc_ip/24 dev eth0
    sudo ip -n $ns_svc link set eth0 up
    mkdir -p "$work/relay"
    sed -e "s|^STORAGE_DRIVER = 'disk'|STORAGE_DRIVER = 'memory'|" \
        -e "s|^LISTEN_HOST = '127.0.0.1'|LISTEN_HOST = '$relay_ip'|" \
        -e "s|^LISTEN_PORT = 8000|LISTEN_PORT = $relay_port|" \
        "$repository/phpvpn.py" > "$work/relay/phpvpn.py"
    sudo ip netns exec $ns_relay setpriv --reuid="$(id -u)" --regid="$(id -g)" --init-groups \
        "$python_server" -B "$work/relay/phpvpn.py" > "$work/relay/relay.log" 2>&1 &
    echo $! > "$work/relay.pid"
    local code=""
    for _ in $(seq 1 60); do
        code=$(sudo ip netns exec $ns_svc curl -s -o /dev/null -w '%{http_code}' --max-time 2 "$relay_url" || true)
        [ "$code" = 200 ] && break
        sleep 0.5
    done
    [ "$code" = 200 ] || { echo "FATAL: the relay did not become available"; exit 1; }
    sudo mkdir -m 0755 "$base"
    sudo ip netns exec $ns_svc "$build/apps/phpvpn-service" --foreground --config-dir "$base/config" \
        --state-dir "$base/state" --runtime-dir "$base/run" > "$work/service.log" 2>&1 &
    echo $! > "$work/service.pid"
    for _ in $(seq 1 100); do [ -S "$base/run/control-v1.sock" ] && break; sleep 0.1; done
    [ -S "$base/run/control-v1.sock" ] || { echo "FATAL: the service did not start"; exit 1; }
}

monitor=1280x800
[ "$mode" = compact ] && monitor=700x560
if [ "$mode" = wizard ] || [ "$mode" = compact ]; then
    trap root_down EXIT
    root_up
    socket="$base/run/control-v1.sock"
    server_uid=0
fi

# A private runtime directory keeps the Wayland, accessibility bus and dconf
# sockets of this run apart from the desktop session of the same user.
XDG_RUNTIME_DIR=$(mktemp -d /tmp/phpvpn-wizard-check.XXXXXX)
chmod 700 "$XDG_RUNTIME_DIR"
export XDG_RUNTIME_DIR here build mode work relay_url base monitor
export socket=${socket:-} server_uid=${server_uid:-}
unset DISPLAY WAYLAND_DISPLAY DBUS_SESSION_BUS_ADDRESS
export GIO_USE_VFS=local

status=0
dbus-run-session -- bash -eu -c '
runtime="$XDG_RUNTIME_DIR"
display="phpvpn-wizard-$$"
pids=""
cleanup() { for pid in $pids; do kill "$pid" 2>/dev/null || true; done; }
trap cleanup EXIT INT TERM
passes=0
fail() { echo "FAIL: $*"; echo "RESULT: $passes passed, 1 failed"; exit 1; }
pass() { passes=$((passes + 1)); echo "PASS: $*"; }

mutter --headless --no-x11 --virtual-monitor "$monitor" --wayland-display "$display" >"$work/mutter.log" 2>&1 &
pids="$pids $!"
for _ in $(seq 50); do [ -S "$runtime/$display" ] && break; sleep 0.1; done
[ -S "$runtime/$display" ] || fail "headless mutter did not start"
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

if [ "$mode" = detail ]; then
    socket="$runtime/mock/control.sock"
    server_uid=$(id -u)
    mkfifo "$runtime/control"
    sleep 900 >"$runtime/control" &
    pids="$pids $!"
    "$build/apps/tray-linux/phpvpn-tray-mock-service" --socket "$socket" --step-ms 400 --result http \
        <"$runtime/control" >"$work/mock.log" 2>&1 &
    pids="$pids $!"
    for _ in $(seq 50); do [ -S "$socket" ] && break; sleep 0.1; done
    [ -S "$socket" ] || fail "mock service did not start"
fi

PHPVPN_TRAY_TEST_SOCKET="$socket" PHPVPN_TRAY_TEST_SERVER_UID="$server_uid" \
    "$build/apps/tray-linux/phpvpn-tray" >"$work/tray.log" 2>&1 &
tray=$!
pids="$pids $tray"
P="python3 $here/wizard_atspi.py"
W="Configuration Wizard"
notrun=0
# Keyboard focus is only reported for the active window; a headless compositor does
# not activate windows opened without real input, so such a check is NOT RUN.
focus() {
    local rc=0
    $P focused "$@" || rc=$?
    [ "$rc" = 0 ] && return 0
    [ "$rc" = 3 ] && { notrun=$((notrun + 1)); echo "NOT RUN: focus $* (window not active)"; return 0; }
    return 1
}
cli() { "$build/apps/phpvpn" --socket "$socket" "$@"; }
vpn_state() { cli status 2>/dev/null | python3 -c "import json,sys; s=json.load(sys.stdin)[\"status\"]; print(s[\"state\"], s[\"phase\"])" 2>/dev/null || echo unreachable; }
wait_state() { for _ in $(seq 1 "$2"); do [ "$(vpn_state)" = "$1" ] && return 0; sleep 1; done; return 1; }

$P wait PHPVPN "PHPVPN: disconnected" 20 || fail "tray control window without disconnected status"

if [ "$mode" = detail ]; then
    $P click PHPVPN Connect
    $P wait "PHPVPN connection" "Cannot connect to the server" 20 || fail "no Connect error panel"
    $P button "PHPVPN connection" "Error detail" visible || fail "no Error detail button"
    $P click "PHPVPN connection" "Error detail"
    $P present "Error detail" 5 || fail "HTTP error detail did not open from the progress window"
    $P tabs "Error detail" "Request,Response" || fail "tabs are not exactly Request, Response"
    $P contains "Error detail" Request "POST /phpvpn.php HTTP/1.1" || fail "request text"
    $P wait-prefix "Error detail" "Request (Original size: " 3 || fail "request note"
    $P tab "Error detail" Response
    $P contains "Error detail" Response "HTTP/1.1 503 Service Unavailable" || fail "response text"
    $P wait-prefix "Error detail" "Response (Original size: " 3 || fail "response note"
    $P dump "Error detail" > "$work/detail.txt"
    grep -q "button: .OK." "$work/detail.txt" && fail "the detail window has a footer button"
    pass "Connect Error detail: owned window, tabs Request/Response, raw text and notes, no footer"
    $P close "Error detail"
    $P absent "Error detail" 5 || fail "X did not close the detail window"
    $P click "PHPVPN connection" "Error detail"
    $P present "Error detail" 5 || fail "detail did not reopen"
    $P close "PHPVPN connection"
    $P absent "Error detail" 5 || fail "closing the progress window did not close the detail"
    pass "detail closes with X and together with the progress window"
elif [ "$mode" = compact ]; then
    $P click PHPVPN "Configuration Wizard"
    $P wait "$W" "Server URL configuration" 15 || fail "Wizard step 1 not shown"
    sleep 1
    $P dump "$W" > "$work/compact1.txt"
    $P gone "$W" "Server secret
Upcoming" 5 || fail "sidebar texts shown in the compact layout"
    $P gone "$W" "Configure this computer" 2 || fail "sidebar heading shown in the compact layout"
    for marker in 1 2 3 4; do grep -q "label: .$marker.$" "$work/compact1.txt" || fail "compact marker $marker missing"; done
    for button in Reset "Verify & continue"; do $P button "$W" "$button" visible || fail "$button not reachable"; done
    pass "compact layout below 760 DIP: four numbered markers, no step texts, footer actions reachable"
    $P settext "$W" "Server URL" "'"$relay_url"'"
    $P click "$W" "Verify & continue"
    $P wait "$W" "Server secret configuration" 20 || fail "URL check in the compact layout"
    $P dump "$W" > "$work/compact2.txt"
    grep -q "label: .✓.$" "$work/compact2.txt" || fail "completed step 1 is not a check mark in the compact strip"
    for button in Reset Back "Verify & continue"; do $P button "$W" "$button" visible || fail "$button not reachable"; done
    pass "compact layout: check mark for the completed step, Reset, Back and primary reachable"
    $P close "$W"
    $P absent "$W" 10 || fail "X did not close the Wizard"
else
    # --- 1: new configuration, Server URL step ------------------------------------
    $P click PHPVPN "Configuration Wizard"
    $P wait "$W" "Server URL configuration" 15 || fail "Wizard step 1 not shown"
    # The service creates the default configuration at startup (config startup: default created).
    $P text "$W" "Server URL" "http://127.0.0.1/phpvpn.php" || fail "URL of the default configuration"
    $P dump "$W" > "$work/step1.txt"
    for text in "Configure this computer" "Set up this computer for your PHPVPN server. Changes are saved with Finish." \
        "Enter the complete address of the PHPVPN server script." "Example: https://vpn.example.com/phpvpn.php" \
        "Configure additional HTTP headers for this server." \
        "HTTP and HTTPS are supported. The URL cannot contain a username, query, or fragment." \
        "The endpoint will be checked before server authentication."; do
        grep -qF "label: '"'"'$text'"'"'" "$work/step1.txt" || fail "step 1 lacks: $text"
    done
    $P button "$W" Back hidden || fail "Back visible on step 1"
    $P button "$W" "Error detail" hidden || fail "Error detail visible without a diagnostic"
    $P button "$W" Reset sensitive || fail "Reset not available"
    $P button "$W" "Verify & continue" sensitive || fail "primary not available"
    focus "$W" "Server URL" || fail "focus is not on the first field"
    pass "step 1 texts, footer, focus on Server URL"

    $P settext "$W" "Server URL" "http://MODIFY-THIS-URL/phpvpn.php"
    $P click "$W" "Verify & continue"
    $P wait "$W" "Replace MODIFY-THIS-URL with your server address." 5 || fail "placeholder not refused"
    focus "$W" "Server URL" || fail "focus not on the refused field"
    $P wait "$W" "Could not complete this action" 2 || fail "error panel heading"
    pass "placeholder refused locally before any network check"

    # --- HTTP headers dialog -------------------------------------------------------
    $P click "$W" "HTTP headers"
    $P present "HTTP headers" 5 || fail "HTTP headers dialog did not open"
    $P contains "HTTP headers" "HTTP headers" "Cache-Control: no-cache" || fail "headers text"
    for button in Reset Cancel OK; do $P button "HTTP headers" "$button" visible || fail "headers lacks $button"; done
    $P settext "HTTP headers" "HTTP headers" "Host: example.com"
    $P click "HTTP headers" OK
    $P wait "alert:HTTP headers" "masterSetup.httpHeaders line 1: Host is managed by the application or transport" 5 || fail "headers validation error"
    $P click "alert:HTTP headers" OK
    $P absent "alert:HTTP headers" 5 || fail "error dialog stayed"
    $P present "HTTP headers" 1 || fail "the refused OK closed the dialog"
    $P click "HTTP headers" Reset
    $P contains "HTTP headers" "HTTP headers" "Pragma: no-cache" || fail "Reset did not load the defaults"
    $P click "HTTP headers" Cancel
    $P absent "HTTP headers" 5 || fail "Cancel did not close the dialog"
    focus "$W" "HTTP headers" || fail "focus did not return to HTTP headers"
    $P click "$W" "HTTP headers"
    $P settext "HTTP headers" "HTTP headers" "Cache-Control: no-cache
User-Agent: phpvpn-wizard-check"
    $P click "HTTP headers" OK
    $P absent "HTTP headers" 5 || fail "OK did not accept valid headers"
    pass "HTTP headers: validation error keeps the dialog, Reset, Cancel, OK"

    # --- URL error with HTTP diagnostic ----------------------------------------------
    $P settext "$W" "Server URL" "http://'"$relay_ip"':'"$relay_port"'/wrong.php"
    $P click "$W" "Verify & continue"
    $P wait-prefix "$W" "Could not complete this action" 20 || true
    $P button "$W" "Error detail" visible || fail "no Error detail after an HTTP error"
    $P dump "$W" > "$work/url-error.txt"
    grep -q "HTTP status: 404" "$work/url-error.txt" || fail "HTTP status not shown"
    $P click "$W" "Error detail"
    $P present "Error detail" 5 || fail "Wizard Error detail did not open"
    $P tabs "Error detail" "Request,Response" || fail "detail tabs"
    $P contains "Error detail" Request "/wrong.php" || fail "request line"
    $P contains "Error detail" Request "User-Agent: phpvpn-wizard-check" || fail "accepted headers not used by the check"
    $P tab "Error detail" Response
    $P contains "Error detail" Response " 404 " || fail "404 response"
    $P close "Error detail"
    $P absent "Error detail" 5 || fail "detail X"
    pass "URL check HTTP error: status, Error detail with raw request/response"

    # --- 2: server secret ----------------------------------------------------------------
    $P settext "$W" "Server URL" "'"$relay_url"'"
    $P button "$W" "Error detail" hidden || fail "diagnostic kept after editing the URL"
    $P click "$W" "Verify & continue"
    $P wait "$W" "Server secret configuration" 20 || fail "URL check did not pass"
    $P wait "$W" "Server URL
Completed" 3 || fail "sidebar step 1 not Completed"
    $P checked "$W" "Use default server secret" 1 || fail "default secret switch"
    $P wait "$W" "Default server secret in use" 3 || fail "default secret warning"
    $P button "$W" Back sensitive || fail "Back missing on step 2"
    pass "step 1 verified against the relay; step 2 with default secret warning"
    $P toggle "$W" "Use default server secret"
    $P checked "$W" "Use default server secret" 0 || fail "switch did not turn off"
    $P settext "$W" "Server secret" "wrong-secret"
    $P click "$W" "Verify & continue"
    $P wait-prefix "$W" "You are trying to use the server secret “wrong-secret”" 20 || fail "secret mismatch text"
    $P wait "$W" "Server secret
Failed" 3 || fail "sidebar step 2 not Failed"
    $P button "$W" "Error detail" visible || fail "no Error detail after the secret mismatch"
    pass "wrong secret: exact mismatch text, Failed, Error detail"
    $P click "$W" Back
    $P wait "$W" "Server URL configuration" 5 || fail "Back to step 1"
    $P text "$W" "Server URL" "'"$relay_url"'" || fail "Back lost the URL"
    $P click "$W" "Verify & continue"
    $P wait "$W" "Server secret configuration" 20 || fail "URL re-check"
    $P text "$W" "Server secret" "wrong-secret" || fail "secret draft kept"
    $P toggle "$W" "Use default server secret"
    $P text "$W" "Server secret" "CHANGE_THIS_SERVER_SECRET" || fail "default secret not filled"
    $P click "$W" "Verify & continue"
    $P wait "$W" "Virtual IP and access key" 20 || fail "secret check did not pass"
    pass "Back keeps the draft; default secret verified"

    # --- 3: identity -----------------------------------------------------------------------
    $P checked "$W" "Automatically detect IP address" 1 || fail "auto IP"
    $P text "$W" "Virtual IP address" "10.99.99.2" || fail "suggested IP"
    $P text "$W" "Access key" "0202020202020202020202020202020202020202020202020202020202020202" || fail "default key"
    $P wait "$W" "Default access key in use" 3 || fail "default key warning"
    $P click "$W" "Verify & continue"
    $P wait "$W" "Communication contracts" 20 || fail "access check did not pass"
    pass "step 3: verified IP suggestion and default key accepted by the relay"

    # --- 4: contracts and Finish ----------------------------------------------------------------
    $P dump "$W" > "$work/step4.txt"
    for text in "Existing contracts:" "Missing defaults:" "To add on Finish:" \
        "Existing contracts and keys are kept. Default keys are publicly predictable."; do
        grep -qF "$text" "$work/step4.txt" || fail "step 4 lacks: $text"
    done
    grep -q "label: .13.$" "$work/step4.txt" || fail "13 missing default contracts not shown"
    $P checked "$W" "Add missing default communication contracts" 1 || fail "contracts switch"
    $P click "$W" Finish
    $P wait "alert:Configuration saved" "The configuration was saved. To connect to the VPN server, click Connect." 20 || fail "no saved notification"
    $P click "alert:Configuration saved" OK
    $P absent "$W" 10 || fail "Wizard did not close after the confirmed save"
    sudo test -s "$base/config/config.json" || fail "no configuration written"
    sudo python3 -c "import json,sys; d=json.load(open(sys.argv[1])); m=d[\"masterSetup\"]; assert m[\"serverUrl\"]==sys.argv[2], m; assert m[\"virtualIp\"]==\"10.99.99.2\"; assert \"User-Agent: phpvpn-wizard-check\" in m[\"httpHeaders\"]; assert len(d[\"communicationContracts\"])==13, len(d[\"communicationContracts\"])" \
        "$base/config/config.json" "$relay_url" || fail "saved configuration differs"
    pass "Finish: receipt, Configuration saved notice, close; the file has URL, IP, headers and 13 contracts"

    # --- disconnect gate ----------------------------------------------------------------------------
    cli connect >"$work/connect.txt" 2>&1 || fail "phpvpn connect"
    wait_state "connected running" 45 || fail "VPN did not connect"
    $P absent "PHPVPN connection" 1 >/dev/null 2>&1 || true
    $P click PHPVPN "Configuration Wizard"
    $P wait "$W" "Disconnect the VPN before continuing" 15 || fail "no disconnect gate"
    $P wait "$W" "The VPN will be disconnected for everyone." 2 || fail "gate warning"
    $P button "$W" Back hidden || fail "Back on the gate"
    focus "$W" Reset || fail "gate focus is not on a non-destructive action"
    $P click "$W" "Disconnect & continue"
    $P wait "$W" "Server URL configuration" 45 || fail "gate did not resume at step 1"
    [ "$(vpn_state)" = "disconnected idle" ] || fail "VPN not disconnected"
    $P text "$W" "Server URL" "'"$relay_url"'" || fail "existing configuration not loaded"
    pass "gate: Disconnect & continue, real disconnected/idle, resume at Server URL with the saved values"

    # --- keepalive keeps the session beyond the 60 s lease -----------------------------------------
    sleep 70
    $P settext "$W" "Server URL" "'"$relay_url"'"
    $P click "$W" "Verify & continue"
    $P wait "$W" "Server secret configuration" 20 || fail "session lost after 70 s (keepalive)"
    pass "keepalive: the session survives 70 s without user input"

    # --- Reset -------------------------------------------------------------------------------------------
    $P click "$W" Reset
    $P wait "alert:$W" "Reset this entire wizard to the default settings? Your file will change only after Finish." 5 || fail "reset question"
    focus "alert:$W" No || fail "reset question default is not No"
    $P click "alert:$W" No
    $P absent "alert:$W" 5 || fail "reset dialog"
    $P wait "$W" "Server secret configuration" 2 || fail "No changed the step"
    $P click "$W" Reset
    $P click "alert:$W" Yes
    $P wait "$W" "Server URL configuration" 15 || fail "Reset did not return to step 1"
    $P text "$W" "Server URL" "http://MODIFY-THIS-URL/phpvpn.php" || fail "Reset draft is not the default"
    sudo python3 -c "import json,sys; assert json.load(open(sys.argv[1]))[\"masterSetup\"][\"serverUrl\"]==sys.argv[2]" \
        "$base/config/config.json" "$relay_url" || fail "Reset changed the file"
    pass "Reset asks (default No), resets only the draft; the file is unchanged"

    # --- close: dirty draft is discarded by X, file unchanged -------------------------------------------------
    $P close "$W"
    $P absent "$W" 10 || fail "X did not close the Wizard"
    $P click PHPVPN "Configuration Wizard"
    $P wait "$W" "Server URL configuration" 15 || fail "reopen"
    $P text "$W" "Server URL" "'"$relay_url"'" || fail "reopen did not load the saved file"
    $P close "$W"
    $P absent "$W" 10 || fail "X did not close the reopened Wizard"
    pass "X closes the session; reopening loads the saved configuration"

    # --- Exit with an open, clean Wizard ends only the tray -------------------------------------------------
    $P click PHPVPN "Configuration Wizard"
    $P wait "$W" "Server URL configuration" 15 || fail "reopen for Exit"
    $P settext "$W" "Server URL" "http://192.0.2.10/phpvpn.php"
    $P click PHPVPN Exit
    $P wait "alert:Exit PHPVPN" "Discard unsaved changes?" 5 || fail "Exit did not ask for the dirty Wizard"
    $P click "alert:Exit PHPVPN" Yes
    for _ in $(seq 50); do kill -0 "$tray" 2>/dev/null || break; sleep 0.1; done
    kill -0 "$tray" 2>/dev/null && fail "Exit did not end the tray"
    pass "Exit asks Discard unsaved changes? for the dirty Wizard and ends the tray"
fi

kill "$tray" 2>/dev/null || true
sleep 1
if grep -E "ERROR: AddressSanitizer|runtime error:" "$work/tray.log"; then
    fail "sanitizer report (see above)"
fi
# LeakSanitizer (ASan builds): lsan_check.py ignores only the system library caches
# of lsan.supp; any leak through PHPVPN code fails.
python3 "$here/lsan_check.py" "$work/tray.log" "$work/mock.log" || fail "LeakSanitizer report (see above)"
if grep -q "CRITICAL\|WARNING" "$work/tray.log"; then
    grep "CRITICAL\|WARNING" "$work/tray.log" | sort | uniq -c
    echo "NOTE: GLib criticals/warnings in the tray log (see above)"
fi
echo "RESULT: $passes passed, 0 failed, $notrun focus checks not run"
' || status=$?
for _ in 1 2 3 4 5; do rm -rf "$XDG_RUNTIME_DIR" 2>/dev/null && break; sleep 1; done
exit "$status"
