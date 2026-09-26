#!/bin/bash
# Linux CLI smoke test (plan-v001 5/WP07). Root only, because it starts an
# isolated phpvpn-service instance; every CLI call itself is unprivileged in
# spirit (a few run explicitly as nobody).
#
#   sudo bash tests/e2e/linux/cli-smoke.sh [build-dir]
#
# One private network namespace phpvpn-cli-smoke holds the real Python relay on
# 127.0.0.1:18096 and the service instance; the main namespace is untouched.
# Covers every command, exit codes 0/1/2, the error line format, a stopped
# service, a stale socket, a denied socket, a non-root peer, an unreadable and
# an invalid file, a file over the 512 KiB limit and a non-ASCII file name.
#
# build-dir defaults to work/build/linux-debug. PHPVPN_SERVER_PYTHON is the
# interpreter with requirements-server.txt. Logs stay in PHPVPN_E2E_WORK
# (default work/cli-smoke-<timestamp>).
set -euo pipefail

client=$(cd "$(dirname "$0")/../../.." && pwd)
repository=$(cd "$client/.." && pwd)
build=$(cd "${1:-$repository/work/build/linux-debug}" && pwd)
service="$build/apps/phpvpn-service"
cli="$build/apps/phpvpn"
python_server=${PHPVPN_SERVER_PYTHON:-python3}
work=${PHPVPN_E2E_WORK:-$repository/work/cli-smoke-$(date +%Y%m%d-%H%M%S)}
ns=phpvpn-cli-smoke
base=/run/phpvpn-cli-smoke
socket=$base/run/control-v1.sock
relay_url=http://127.0.0.1:18096/phpvpn.php

failures=0
passes=0
pass() { passes=$((passes + 1)); echo "PASS: $*"; }
fail() { failures=$((failures + 1)); echo "FAIL: $*"; }
die() { echo "FATAL: $*"; exit 1; }

[ "$(id -u)" = 0 ] || die "run as root"
[ -x "$service" ] && [ -x "$cli" ] || die "missing binaries in $build"
! ip netns list | grep -qw $ns || die "network namespace $ns already exists"
[ ! -e "$base" ] || die "$base already exists"
mkdir -p "$work"
echo "work directory: $work"

service_pid=
relay_pid=
fake_pid=
cleanup() {
    set +e
    [ -n "$service_pid" ] && kill -TERM "$service_pid" 2> /dev/null && wait "$service_pid" 2> /dev/null
    [ -n "$relay_pid" ] && kill "$relay_pid" 2> /dev/null && wait "$relay_pid" 2> /dev/null
    [ -n "$fake_pid" ] && kill "$fake_pid" 2> /dev/null
    ip netns del $ns 2> /dev/null
    rm -rf "$base"
    echo "passes=$passes failures=$failures (logs in $work)"
}
trap cleanup EXIT

# expect <exit> <stdout-pattern|-> <stderr-pattern|-> <description> -- command...
# Patterns are extended regular expressions matched against the whole output.
expect() {
    local code=$1 out=$2 err=$3 description=$4
    shift 5
    local actual=0
    "$@" > "$work/out.txt" 2> "$work/err.txt" || actual=$?
    {
        echo "+ $* (exit $actual)"; cat "$work/out.txt"; sed 's/^/stderr: /' "$work/err.txt"
    } >> "$work/cli.log"
    if [ "$actual" != "$code" ]; then
        fail "$description: exit $actual, expected $code ($(head -c 300 "$work/err.txt"))"
    elif [ "$out" != - ] && ! grep -Eq -- "$out" "$work/out.txt"; then
        fail "$description: stdout does not match /$out/"
    elif [ "$err" != - ] && ! grep -Eq -- "$err" "$work/err.txt"; then
        fail "$description: stderr does not match /$err/ ($(head -c 300 "$work/err.txt"))"
    else
        pass "$description (exit $code)"
    fi
}
phpvpn() { "$cli" --socket "$socket" "$@"; }
as_nobody() { setpriv --reuid=65534 --regid=65534 --clear-groups "$@"; }

# --- usage -----------------------------------------------------------------
expect 2 '^usage: phpvpn <command>' - "no command prints the usage" -- "$cli"
expect 2 '^usage: phpvpn <command>' - "an unknown command prints the usage" -- "$cli" frobnicate
expect 2 - '^set-config needs the path of a configuration document$' "set-config without a file" -- phpvpn set-config

# --- service unavailable ---------------------------------------------------
mkdir -m 0755 "$base" "$base/run"
# Unprivileged calls run a copy outside the (possibly private) home directory.
install -m 0755 "$cli" "$base/phpvpn"
expect 1 - 'not reachable.*the service is not running' "status without a service fails" -- phpvpn status
expect 1 - '^hint: the phpvpn service is not running or its socket is missing \(systemctl status phpvpn\)$' \
    "a missing socket gets the service-not-running hint" -- phpvpn status
if [ ! -e /run/phpvpn/control-v1.sock ]; then
    expect 1 - 'hint: the phpvpn service is not running' "the production socket path gets the same hint" -- "$cli" status
fi
python3 -c 'import socket,sys; s=socket.socket(socket.AF_UNIX); s.bind(sys.argv[1]); s.close()' "$socket"
expect 1 - 'hint: the phpvpn service is not running' "a stale socket (ECONNREFUSED) gets the same hint" -- phpvpn diagnostics
rm -f "$socket"
mkdir -m 0700 "$base/fake"
chown 65534:65534 "$base/fake"
# Called directly, not through the shell function, so $! is the python process itself
# (setpriv execs it) and the kill below does not leave it running.
setpriv --reuid=65534 --regid=65534 --clear-groups python3 -c 'import socket,sys,time
s=socket.socket(socket.AF_UNIX); s.bind(sys.argv[1]); s.listen(4)
while True:
    c,_=s.accept(); time.sleep(0.2); c.close()' "$base/fake/control-v1.sock" &
fake_pid=$!
for _ in $(seq 1 50); do [ -S "$base/fake/control-v1.sock" ] && break; sleep 0.1; done
expect 1 - 'the socket peer is not the phpvpn service' "a non-root peer is refused before sending" \
    -- "$cli" --socket "$base/fake/control-v1.sock" status
expect 1 - '^hint: the control socket is not served by the root phpvpn service$' "the invalid peer hint" \
    -- "$cli" --socket "$base/fake/control-v1.sock" set-config "$repository/client/tests/e2e/linux/cli-smoke.sh"
kill "$fake_pid"; wait "$fake_pid" 2> /dev/null || true; fake_pid=

# --- relay and service -----------------------------------------------------
ip netns add $ns
ip -n $ns link set lo up
mkdir -p "$work/relay"
sed -e "s|^STORAGE_DRIVER = 'disk'|STORAGE_DRIVER = 'memory'|" -e "s|^LISTEN_PORT = 8000|LISTEN_PORT = 18096|" \
    "$repository/phpvpn.py" > "$work/relay/phpvpn.py"
ip netns exec $ns "$python_server" -B "$work/relay/phpvpn.py" > "$work/relay/relay.log" 2>&1 &
relay_pid=$!
for _ in $(seq 1 60); do
    [ "$(ip netns exec $ns curl -s -o /dev/null -w '%{http_code}' --max-time 2 $relay_url || true)" = 200 ] && break
    sleep 0.5
done
ip netns exec $ns "$service" --foreground --config-dir "$base/config" --state-dir "$base/state" \
    --runtime-dir "$base/run" > "$work/service.log" 2>&1 &
service_pid=$!
for _ in $(seq 1 100); do phpvpn status > /dev/null 2>&1 && break; sleep 0.1; done

expect 0 '"state": "disconnected"' - "status prints the pretty JSON envelope" -- phpvpn status
expect 0 '"status": \{' - "status works for an unprivileged user (public socket)" -- as_nobody "$base/phpvpn" --socket "$socket" status
chmod 0700 "$base/run"
expect 1 - '^hint: access to the phpvpn control socket was denied' "a denied socket gets the permission hint" \
    -- as_nobody "$base/phpvpn" --socket "$socket" status
chmod 0755 "$base/run"
expect 0 '"diagnostics": \{' - "diagnostics prints the pretty JSON envelope" -- phpvpn diagnostics
expect 0 '"masterSetup"' - "get-config prints the startup default document" -- phpvpn get-config

expect 1 - '^the configuration document could not be read$' "set-config with a missing file" -- phpvpn set-config "$work/missing.json"
expect 1 - '^the configuration document could not be read$' "set-config with a directory" -- phpvpn set-config "$work"
printf '{"masterSetup": ' > "$work/invalid.json"
expect 1 - '^config_invalid_json: ' "set-config with invalid JSON reports the service error code" -- phpvpn set-config "$work/invalid.json"
python3 -c 'import sys; open(sys.argv[1],"w").write("{\"pad\": \"" + "x" * 600000 + "\"}")' "$work/large.json"
expect 1 - '^config_too_large: ' "set-config over 512 KiB reports config_too_large" -- phpvpn set-config "$work/large.json"
python3 -c 'import sys; open(sys.argv[1],"w").write("{\"pad\": \"" + "x" * 8000000 + "\"}")' "$work/huge.json"
expect 1 - '^config_too_large: ' "set-config of an 8 MB file is bounded and still reports config_too_large" -- phpvpn set-config "$work/huge.json"
rm -f "$work/large.json" "$work/huge.json"

python3 - "$work/config.json" "$relay_url" <<'PY'
import json, sys
json.dump({"masterSetup": {
    "serverSecret": "CHANGE_THIS_SERVER_SECRET", "serverUrl": sys.argv[2],
    "httpHeaders": "Cache-Control: no-cache\r\nUser-Agent: phpvpn-cli-smoke",
    "trustAnyTlsCertificate": True, "vpnCidr": "10.99.99.0/24", "virtualIp": "10.99.99.4",
    "accessKey": "04" * 32, "autoConnectOnStartup": False},
    "communicationContracts": {"10.99.99.5": {"payloadKey": "ab" * 32}}}, open(sys.argv[1], "w"), indent=2)
PY
unicode_file="$work/konfigurace-žluťoučký kůň.json"
cp "$work/config.json" "$unicode_file"
expect 0 '"result": "ok"' - "set-config with a valid document" -- phpvpn set-config "$work/config.json"
expect 0 '"result": "ok"' - "set-config with a non-ASCII file name" -- phpvpn set-config "$unicode_file"
phpvpn get-config > "$work/get-config.txt"
if printf '\n' | cat "$work/config.json" - | cmp -s - "$work/get-config.txt"; then
    pass "get-config prints the stored document unchanged"
else
    fail "get-config differs from the stored document"
fi
expect 0 '"serverVersion"' - "info prints the verified server information" -- phpvpn info
expect 0 '"status": \{' - "connect" -- phpvpn connect
for _ in $(seq 1 150); do phpvpn status 2> /dev/null | grep -q '"tunnelReady": "yes"' && break; sleep 0.2; done
expect 0 '"state": "connected"' - "status after connect" -- phpvpn status
expect 0 '"status": \{' - "disconnect" -- phpvpn disconnect
for _ in $(seq 1 150); do phpvpn status 2> /dev/null | grep -q '"state": "disconnected"' && break; sleep 0.2; done
expect 0 '"state": "disconnected"' - "status after disconnect" -- phpvpn status

kill -TERM "$service_pid"; wait "$service_pid" 2> /dev/null || true; service_pid=
expect 1 - 'hint: the phpvpn service is not running' "status after the service stopped" -- phpvpn status
expect 1 - 'hint: the phpvpn service is not running' "connect after the service stopped" -- phpvpn connect

echo "RESULT: $passes passed, $failures failed"
[ "$failures" = 0 ]
