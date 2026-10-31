#!/bin/sh
# macOS service end-to-end test (plan-macos-v001 step 7). Root only.
#
#   sudo env "PATH=$PATH" PHPVPN_BUILD_DIR=work/build/macos-release \
#       sh client/tests/e2e/macos/run-service-e2e.sh
#
# macOS has no network namespaces, so everything runs on this one host: a
# private relay (fresh copy of server/php/phpvpn.php, wizard-default profile)
# on 127.0.0.1:${PHPVPN_E2E_PORT:-18094} and one isolated phpvpn-service
# instance in the foreground whose config, state, runtime and log directories
# come from the hidden test options. The instance is driven with the real CLI
# through its test-only --socket option and with the Wizard IPC probe.
#
# The service directories live in a fresh root owned directory under
# /private/tmp: the trusted directory walk of the service refuses ancestors
# owned by another user, which every path inside a checkout under /Users is.
# Logs and artifacts stay in PHPVPN_E2E_WORK (default
# work/e2e-macos/<timestamp>-<pid>); the service file log is copied there.
#
# Steps: --verify-tunnel, start, status, Wizard IPC (POSIX config path, finish
# with commitId), set-config and get-config byte for byte, connect and status
# connected/running/tunnelReady within 30 s, a utun interface with inet
# 10.99.99.2 and the route 10.99.99/24 through it, info and diagnostics,
# disconnect within 30 s with address, route and interface gone, a refused
# second instance, SIGTERM with exit code 0 and the socket removed, the default
# route unchanged. Nothing outside the private directories is created or
# removed; the utun interface vanishes with the service.
#
# Environment: PHPVPN_BUILD_DIR (required, the CMake build directory with
# apps/phpvpn-service, apps/phpvpn and tests/phpvpn-wizard-ipc-probe),
# PHPVPN_E2E_PORT, PHPVPN_E2E_WORK, PHPVPN_PHP (default: php from PATH).
# POSIX sh with the BSD userland of macOS.
set -eu

client=$(cd "$(dirname "$0")/../../.." && pwd)
repository=$(cd "$client/.." && pwd)
port=${PHPVPN_E2E_PORT:-18094}
relay_url="http://127.0.0.1:$port/phpvpn.php"
php=${PHPVPN_PHP:-php}
work=${PHPVPN_E2E_WORK:-$repository/work/e2e-macos/$(date +%Y%m%d-%H%M%S)-$$}
virtual_ip=10.99.99.2
peer_ip=10.99.99.3
access_key=0202020202020202020202020202020202020202020202020202020202020202
payload_key=918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d
ifconfig=/sbin/ifconfig
netstat=/usr/sbin/netstat
route=/sbin/route

failures=0
passes=0
pass() { passes=$((passes + 1)); echo "PASS: $*"; }
fail() { failures=$((failures + 1)); echo "FAIL: $*"; }
die() { echo "FATAL: $*"; exit 1; }
# check <description> <command...>
check() {
    check_message=$1
    shift
    if "$@"; then pass "$check_message"; else fail "$check_message"; fi
}

[ "$(id -u)" = 0 ] || die "run as root"
[ "$(uname -s)" = Darwin ] || die "this test runs on macOS only"
[ -n "${PHPVPN_BUILD_DIR:-}" ] || die "set PHPVPN_BUILD_DIR to the CMake build directory"
build=$(cd "$PHPVPN_BUILD_DIR" && pwd) || die "PHPVPN_BUILD_DIR $PHPVPN_BUILD_DIR does not exist"
service="$build/apps/phpvpn-service"
cli="$build/apps/phpvpn"
probe="$build/tests/phpvpn-wizard-ipc-probe"
for binary in "$service" "$cli" "$probe"; do [ -x "$binary" ] || die "missing $binary"; done
command -v "$php" > /dev/null 2>&1 || die "php is not installed (set PHPVPN_PHP)"
for tool in curl cmp awk stat mktemp "$ifconfig" "$netstat" "$route"; do
    command -v "$tool" > /dev/null 2>&1 || die "missing $tool"
done
if curl -s -o /dev/null --max-time 2 "http://127.0.0.1:$port/"; then
    die "127.0.0.1:$port is already in use (set PHPVPN_E2E_PORT)"
fi
if "$ifconfig" | grep -q "inet $virtual_ip "; then
    die "$virtual_ip is already assigned to an interface of this host"
fi
mkdir -p "$work"
echo "work directory: $work"

default_route() {
    "$route" -n get default 2> /dev/null | awk '$1 == "gateway:" || $1 == "interface:"' || true
}
default_route > "$work/default-route-before.txt"

service_pid=
relay_pid=
version_relay_pid=
base=
cleanup() {
    set +e
    if [ -n "$service_pid" ]; then
        kill -TERM "$service_pid" 2> /dev/null
        cleanup_wait=0
        while kill -0 "$service_pid" 2> /dev/null && [ "$cleanup_wait" -lt 450 ]; do
            sleep 0.1
            cleanup_wait=$((cleanup_wait + 1))
        done
        kill -KILL "$service_pid" 2> /dev/null
        wait "$service_pid" 2> /dev/null
    fi
    if [ -n "$relay_pid" ]; then
        kill "$relay_pid" 2> /dev/null
        wait "$relay_pid" 2> /dev/null
    fi
    if [ -n "$version_relay_pid" ]; then
        kill "$version_relay_pid" 2> /dev/null
        wait "$version_relay_pid" 2> /dev/null
    fi
    case $base in
        /private/tmp/phpvpn-e2e.*)
            if [ -d "$base/logs" ]; then
                mkdir -p "$work/service-logs"
                cp -p "$base"/logs/* "$work/service-logs/" 2> /dev/null
            fi
            rm -rf "$base"
            ;;
    esac
    rm -rf "$work/relay/buffer" "$work/relay-version/buffer"
    echo "passes=$passes failures=$failures (logs in $work)"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

show_service_log() {
    echo "---- service log (last 80 lines) ----"
    tail -n 80 "$work/service.log" 2> /dev/null || true
    echo "-------------------------------------"
}

# --- tunnel check -----------------------------------------------------------
check "phpvpn-service --verify-tunnel (utun kernel control, no interface)" \
    sh -c '"$1" --verify-tunnel > "$2" 2>&1' sh "$service" "$work/verify-tunnel.txt"

# --- relay -------------------------------------------------------------------
mkdir -p "$work/relay/www" "$work/relay/buffer"
sed "s|^const BUFFER_DIRECTORY = './phpvpn_data';|const BUFFER_DIRECTORY = '$work/relay/buffer';|" \
    "$repository/server/php/phpvpn.php" > "$work/relay/www/phpvpn.php"
grep -q "^const BUFFER_DIRECTORY = '$work/relay/buffer';" "$work/relay/www/phpvpn.php" || die "relay buffer not patched"
grep -q "^const SERVER_SECRET = 'CHANGE_THIS_SERVER_SECRET';" "$work/relay/www/phpvpn.php" ||
    die "the shipped relay is not the wizard-default profile"
PHP_CLI_SERVER_WORKERS=8 "$php" -S "127.0.0.1:$port" -t "$work/relay/www" > "$work/relay/relay.log" 2>&1 &
relay_pid=$!
attempt=0
until [ "$(curl -s -o /dev/null -w '%{http_code}' --max-time 2 "$relay_url" || true)" = 200 ]; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 60 ] || ! kill -0 "$relay_pid" 2> /dev/null; then
        cat "$work/relay/relay.log" || true
        die "the php relay did not become available at $relay_url"
    fi
    sleep 0.5
done
pass "php relay reachable at $relay_url"

# --- service instance ----------------------------------------------------------
base=$(mktemp -d /private/tmp/phpvpn-e2e.XXXXXX)
chmod 0755 "$base"
sock="$base/runtime/control-v1.sock"
service_args="--config-dir $base/config --state-dir $base/state --runtime-dir $base/runtime --log-dir $base/logs"
echo "service directories: $base"

cli_on() { "$cli" --socket "$sock" "$@"; }
# Commands whose answer only matters through the exit code go to cli.log.
cli_do() {
    echo "+ phpvpn $*" >> "$work/cli.log"
    cli_on "$@" >> "$work/cli.log" 2>&1
}
# "state phase tunnelReady" of the status envelope, or "unreachable".
vpn_state() {
    cli_on status 2> /dev/null | "$php" -d display_errors=stderr -r '
        $document = json_decode(stream_get_contents(STDIN), true);
        if (!is_array($document) || !isset($document["status"]["state"], $document["status"]["phase"],
                                            $document["status"]["tunnelReady"])) {
            echo "unreachable";
            exit(0);
        }
        $status = $document["status"];
        echo $status["state"], " ", $status["phase"], " ", $status["tunnelReady"];' 2> /dev/null || echo unreachable
}
# wait_state "<state phase [tunnelReady]>" <seconds>, prefix match
wait_state() {
    wait_deadline=$(( $(date +%s) + $2 ))
    while :; do
        wait_current=$(vpn_state)
        case $wait_current in "$1"*) return 0 ;; esac
        if [ "$(date +%s)" -ge "$wait_deadline" ]; then
            echo "  state: $wait_current (expected $1)"
            return 1
        fi
        sleep 0.2
    done
}
# Name of the utun interface carrying the IPv4 address $1, empty when none.
utun_with_address() {
    "$ifconfig" 2> /dev/null | awk -v address="$1" '
        /^[^ \t]/ { name = $1; sub(/:$/, "", name) }
        $1 == "inet" && $2 == address && name ~ /^utun[0-9]+$/ { print name; exit }'
}
# True when netstat lists 10.99.99/24 through interface $1 (any interface when empty).
has_vpn_route() {
    "$netstat" -rn -f inet 2> /dev/null | awk -v interface="$1" '
        $1 == "10.99.99/24" || $1 == "10.99.99.0/24" {
            if (interface == "") found = 1
            for (field = 2; field <= NF; field++) if ($field == interface) found = 1
        }
        END { exit found ? 0 : 1 }'
}
route_interface() {
    "$route" -n get "$1" 2> /dev/null | awk '$1 == "interface:" { print $2 }'
}
no_vpn_address() { [ -z "$(utun_with_address "$virtual_ip")" ]; }
no_vpn_route() { ! has_vpn_route ""; }
interface_gone() { ! "$ifconfig" "$1" > /dev/null 2>&1; }

start_service() {
    # Word splitting of service_args is intended; the paths contain no blanks.
    # shellcheck disable=SC2086
    "$service" --foreground $service_args >> "$work/service.log" 2>&1 &
    service_pid=$!
    start_wait=0
    while [ "$start_wait" -lt 300 ]; do
        if [ -S "$sock" ] && cli_on status > /dev/null 2>&1; then return 0; fi
        kill -0 "$service_pid" 2> /dev/null || return 1
        sleep 0.1
        start_wait=$((start_wait + 1))
    done
    return 1
}

if start_service; then
    pass "phpvpn-service --foreground answers on $sock"
else
    show_service_log
    die "the service did not open its control socket within 30 s"
fi
check "the control socket exists" test -S "$sock"
cli_on status > "$work/status-initial.json" 2>&1 || true
check "phpvpn status prints the envelope" grep -q '"state": "disconnected"' "$work/status-initial.json"
check "status is disconnected/idle" wait_state "disconnected idle" 10

# --- Wizard IPC ----------------------------------------------------------------
if "$probe" --socket "$sock" --relay "$relay_url" --config-path "$base/config/config.json" \
        --virtual-ip "$virtual_ip" --document-out "$work/wizard-document.json" > "$work/wizard-probe.log" 2>&1; then
    pass "Wizard IPC open/check url/secret/access/finish with commitId (see wizard-probe.log)"
else
    fail "Wizard IPC probe"
    cat "$work/wizard-probe.log"
fi
check "the committed Wizard document is the file at the POSIX config path" \
    cmp -s "$work/wizard-document.json" "$base/config/config.json"
check "config file is 0600 root in a 0700 directory" \
    test "$(stat -f '%Lp %u' "$base/config/config.json" 2> /dev/null) $(stat -f '%Lp' "$base/config" 2> /dev/null)" = "600 0 700"

# --- configure through the CLI ---------------------------------------------------
cat > "$work/config.json" <<EOF
{
  "masterSetup": {
    "serverSecret": "CHANGE_THIS_SERVER_SECRET",
    "serverUrl": "$relay_url",
    "httpHeaders": "Cache-Control: no-cache\r\nPragma: no-cache\r\nUser-Agent: phpvpn-macos-e2e",
    "trustAnyTlsCertificate": true,
    "useDefaultServerSecretInWizard": true,
    "vpnCidr": "10.99.99.0/24",
    "virtualIp": "$virtual_ip",
    "autoDetectIpAddressInWizard": false,
    "accessKey": "$access_key",
    "useDefaultAccessKeyInWizard": true,
    "autoConnectOnStartup": false
  },
  "communicationContracts": {
    "$peer_ip": {
      "payloadKey": "$payload_key"
    }
  }
}
EOF
check "phpvpn set-config" cli_do set-config "$work/config.json"
cli_on get-config > "$work/get-config.txt" 2>&1 || true
printf '\n' | cat "$work/config.json" - > "$work/config.expected"
check "get-config returns the stored document byte for byte" cmp -s "$work/config.expected" "$work/get-config.txt"

# --- connect -----------------------------------------------------------------
check "phpvpn connect" cli_do connect
if wait_state "connected running yes" 30; then
    pass "connected/running/tunnelReady within 30 s"
    utun=$(utun_with_address "$virtual_ip")
    if [ -n "$utun" ]; then
        pass "utun interface $utun carries inet $virtual_ip"
        "$ifconfig" "$utun" > "$work/interface.txt" 2>&1 || true
        "$netstat" -rn -f inet > "$work/routes-connected.txt" 2>&1 || true
        check "netstat routes 10.99.99/24 through $utun" has_vpn_route "$utun"
        check "route get $peer_ip resolves to $utun" test "$(route_interface "$peer_ip")" = "$utun"
    else
        fail "no utun interface carries inet $virtual_ip"
        "$ifconfig" > "$work/interfaces-connected.txt" 2>&1 || true
    fi
    cli_on status > "$work/status-connected.json" 2>&1 || true
    check "phpvpn info prints the verified server information" sh -c '"$1" --socket "$2" info > "$3" 2>&1' \
        sh "$cli" "$sock" "$work/info.txt"
    check "phpvpn diagnostics prints the diagnostics envelope" sh -c \
        '"$1" --socket "$2" diagnostics > "$3" 2>&1 && grep -q "\"diagnostics\"" "$3"' sh "$cli" "$sock" "$work/diagnostics.json"

    # --- disconnect --------------------------------------------------------------
    check "phpvpn disconnect" cli_do disconnect
    check "disconnected within 30 s" wait_state "disconnected idle" 30
    gone_wait=0
    while [ "$gone_wait" -lt 50 ]; do
        no_vpn_address && no_vpn_route && break
        sleep 0.1
        gone_wait=$((gone_wait + 1))
    done
    check "address $virtual_ip is gone after disconnect" no_vpn_address
    check "route 10.99.99/24 is gone after disconnect" no_vpn_route
    if [ -n "$utun" ]; then check "interface $utun is gone after disconnect" interface_gone "$utun"; fi
else
    fail "connected/running/tunnelReady within 30 s"
    cli_on status > "$work/status-failed.json" 2>&1 || true
    cat "$work/status-failed.json"
    show_service_log
fi

# --- server MAJOR version --------------------------------------------------------
# A second php relay of the next MAJOR version: the Wizard URL check, phpvpn info
# and Connect must all refuse it. The shipped relay carries the client version.
major=$(sed -n "s/^const SERVER_VERSION = '\([0-9]*\)\..*/\1/p" "$repository/server/php/phpvpn.php")
[ -n "$major" ] || die "SERVER_VERSION not found in server/php/phpvpn.php"
other="$((major + 1)).0.0"
version_port=$((port + 1))
version_url="http://127.0.0.1:$version_port/phpvpn.php"
mkdir -p "$work/relay-version/www" "$work/relay-version/buffer"
sed -e "s|^const BUFFER_DIRECTORY = './phpvpn_data';|const BUFFER_DIRECTORY = '$work/relay-version/buffer';|" \
    -e "s|^const SERVER_VERSION = '[^']*';|const SERVER_VERSION = '$other';|" \
    "$repository/server/php/phpvpn.php" > "$work/relay-version/www/phpvpn.php"
grep -q "^const SERVER_VERSION = '$other';" "$work/relay-version/www/phpvpn.php" || die "relay version not patched"
PHP_CLI_SERVER_WORKERS=4 "$php" -S "127.0.0.1:$version_port" -t "$work/relay-version/www" \
    > "$work/relay-version/relay.log" 2>&1 &
version_relay_pid=$!
attempt=0
until [ "$(curl -s -o /dev/null -w '%{http_code}' --max-time 2 "$version_url" || true)" = 200 ]; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 60 ] || ! kill -0 "$version_relay_pid" 2> /dev/null; then
        cat "$work/relay-version/relay.log" || true
        die "the php relay of version $other did not become available at $version_url"
    fi
    sleep 0.5
done
if "$probe" --socket "$sock" --relay "$version_url" --config-path "$base/config/config.json" \
        --virtual-ip "$virtual_ip" --expect-url-error server_version_incompatible > "$work/wizard-version.log" 2>&1; then
    pass "Wizard URL check refuses server $other (see wizard-version.log)"
else
    fail "Wizard URL check of server $other"
    cat "$work/wizard-version.log"
fi
sed "s|$relay_url|$version_url|" "$work/config.json" > "$work/config-version.json"
check "phpvpn set-config pointing at server $other" cli_do set-config "$work/config-version.json"
check "phpvpn info refuses server $other with server_version_incompatible" sh -c \
    '! "$1" --socket "$2" info > "$3" 2>&1 && grep -q "^server_version_incompatible: .*server version $4\." "$3"' \
    sh "$cli" "$sock" "$work/info-version.txt" "$other"
check "phpvpn connect to server $other is accepted as a command" cli_do connect
check "Connect to server $other ends in error" wait_state "error idle" 30
cli_on status > "$work/status-version.json" 2>&1 || true
check "status reports server_version_incompatible" grep -q '"code": "server_version_incompatible"' "$work/status-version.json"
check "the URL checkpoint fails for server $other" grep -q '"serverUrlCheck": "failed"' "$work/status-version.json"
check "no interface carries $virtual_ip for server $other" no_vpn_address
kill "$version_relay_pid" 2> /dev/null
wait "$version_relay_pid" 2> /dev/null || true
version_relay_pid=
check "phpvpn set-config back to the first relay" cli_do set-config "$work/config.json"

# --- singleton -----------------------------------------------------------------
second_status=0
# shellcheck disable=SC2086
"$service" --foreground $service_args > "$work/second-instance.log" 2>&1 || second_status=$?
check "a second instance on the same runtime directory is refused (exit $second_status)" \
    test "$second_status" = 1
check "the refusal names the running instance" grep -q "another phpvpn service instance is running" "$work/second-instance.log"
check "the first instance still answers" sh -c '"$1" --socket "$2" status > /dev/null 2>&1' sh "$cli" "$sock"

# --- SIGTERM -------------------------------------------------------------------
started=$(date +%s)
kill -TERM "$service_pid"
stop_wait=0
while kill -0 "$service_pid" 2> /dev/null && [ "$stop_wait" -lt 450 ]; do
    sleep 0.1
    stop_wait=$((stop_wait + 1))
done
term_status=0
wait "$service_pid" 2> /dev/null || term_status=$?
service_pid=
elapsed=$(( $(date +%s) - started ))
check "SIGTERM stops the service with exit code 0 (exit $term_status, ${elapsed} s)" test "$term_status" = 0
check "the control socket is removed after SIGTERM" test ! -e "$sock"
check "the service logged the ordered stop" grep -q "stop requested (SIGTERM)" "$work/service.log"
check "the service file log was written" test -s "$base/logs/phpvpn-service.log"

# --- host state --------------------------------------------------------------------
default_route > "$work/default-route-after.txt"
check "default route unchanged" cmp -s "$work/default-route-before.txt" "$work/default-route-after.txt"
check "no interface carries $virtual_ip after the run" no_vpn_address
check "no route 10.99.99/24 after the run" no_vpn_route

echo "RESULT: $passes passed, $failures failed"
[ "$failures" = 0 ]
