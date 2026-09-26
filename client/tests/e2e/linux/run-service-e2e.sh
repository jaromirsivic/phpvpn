#!/bin/bash
# Linux service end-to-end test (plan-v001 5/WP06 step 4). Root only.
#
#   sudo bash tests/e2e/linux/run-service-e2e.sh [build-dir]
#
# Everything happens in three private network namespaces; the main namespace
# of the machine is never changed (its default routes and DNS are compared
# before and after):
#
#   phpvpn-e2e-relay  bridge 192.168.77.1/24, the real Python relay phpvpn.py
#                     (wizard-default profile, memory storage) on :18095
#   phpvpn-e2e-a      192.168.77.2, isolated phpvpn-service instance A (10.99.99.2)
#   phpvpn-e2e-b      192.168.77.3, isolated phpvpn-service instance B (10.99.99.3)
#
# Each instance gets its own config, state and runtime directory under
# /run/phpvpn-e2e through the hidden test options of phpvpn-service, and is
# driven with the real CLI through its test-only --socket option.
#
# Steps: Wizard IPC exercise on A (POSIX config path, finish with commitId),
# set-config on both, connect, status connected/running/tunnelReady, interface
# address and route, ping A->B over the tunnel, 50 MiB TCP with SHA-256,
# diagnostics counters grow, disconnect < 30 s with interface and route gone,
# kill -9 during a connection, restart with clean recovery and reconnect,
# autoConnectOnStartup=true connects after a restart, SIGTERM with an active
# connection cleans up within 45 s, main namespace unchanged.
#
# build-dir defaults to work/build/linux-debug. PHPVPN_SERVER_PYTHON is the
# interpreter with requirements-server.txt (default python3). Logs and
# artifacts stay in PHPVPN_E2E_WORK (default work/e2e-linux-<timestamp>).
set -euo pipefail

client=$(cd "$(dirname "$0")/../../.." && pwd)
repository=$(cd "$client/.." && pwd)
build=$(cd "${1:-$repository/work/build/linux-debug}" && pwd)
service="$build/apps/phpvpn-service"
cli="$build/apps/phpvpn"
probe="$build/tests/phpvpn-wizard-ipc-probe"
python_server=${PHPVPN_SERVER_PYTHON:-python3}
work=${PHPVPN_E2E_WORK:-$repository/work/e2e-linux-$(date +%Y%m%d-%H%M%S)}
transfer_mib=${PHPVPN_E2E_TRANSFER_MIB:-50}

ns_relay=phpvpn-e2e-relay
ns_a=phpvpn-e2e-a
ns_b=phpvpn-e2e-b
base=/run/phpvpn-e2e
relay_ip=192.168.77.1
relay_port=18095
relay_url="http://$relay_ip:$relay_port/phpvpn.php"
payload_key=918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d

failures=0
passes=0
pass() { passes=$((passes + 1)); echo "PASS: $*"; }
fail() { failures=$((failures + 1)); echo "FAIL: $*"; }
die() { echo "FATAL: $*"; exit 1; }
check() { local message=$1; shift; if "$@"; then pass "$message"; else fail "$message"; fi; }
now_ms() { date +%s%3N; }

[ "$(id -u)" = 0 ] || die "run as root"
for binary in "$service" "$cli" "$probe"; do [ -x "$binary" ] || die "missing $binary"; done
for tool in ip python3 curl ping cmp; do command -v "$tool" > /dev/null || die "missing $tool"; done
"$python_server" -c 'import uvicorn, cryptography' 2> /dev/null || die "PHPVPN_SERVER_PYTHON lacks the relay requirements"
for ns in $ns_relay $ns_a $ns_b; do
    ! ip netns list | grep -qw "$ns" || die "network namespace $ns already exists"
done
[ ! -e "$base" ] || die "$base already exists"
mkdir -p "$work"
echo "work directory: $work"

main_baseline() {
    ip -4 route show default
    ip -6 route show default
    ip -4 route show table all type unicast | grep -v ' linkdown' | grep -c . || true
    cat /etc/resolv.conf
    if command -v resolvectl > /dev/null; then resolvectl dns 2> /dev/null || true; fi
}
main_baseline > "$work/main-before.txt"

declare -A service_pid=()
relay_pid=
cleanup() {
    set +e
    for name in "${!service_pid[@]}"; do
        kill -TERM "${service_pid[$name]}" 2> /dev/null
    done
    for name in "${!service_pid[@]}"; do
        for _ in $(seq 1 90); do kill -0 "${service_pid[$name]}" 2> /dev/null || break; sleep 0.5; done
        kill -KILL "${service_pid[$name]}" 2> /dev/null
    done
    [ -n "$relay_pid" ] && kill "$relay_pid" 2> /dev/null && wait "$relay_pid" 2> /dev/null
    for ns in $ns_a $ns_b $ns_relay; do ip netns del "$ns" 2> /dev/null; done
    rm -rf "$base"
    echo "passes=$passes failures=$failures (logs in $work)"
}
trap cleanup EXIT

# --- topology --------------------------------------------------------------
for ns in $ns_relay $ns_a $ns_b; do ip netns add "$ns"; ip -n "$ns" link set lo up; done
ip -n $ns_relay link add br0 type bridge
ip -n $ns_relay addr add $relay_ip/24 dev br0
ip -n $ns_relay link set br0 up
octet=2
for ns in $ns_a $ns_b; do
    ip -n $ns_relay link add "p$octet" type veth peer name eth0 netns "$ns"
    ip -n $ns_relay link set "p$octet" master br0 up
    ip -n "$ns" addr add "192.168.77.$octet/24" dev eth0
    ip -n "$ns" link set eth0 up
    octet=$((octet + 1))
done

# --- relay -----------------------------------------------------------------
mkdir -p "$work/relay"
sed -e "s|^STORAGE_DRIVER = 'disk'|STORAGE_DRIVER = 'memory'|" \
    -e "s|^LISTEN_HOST = '127.0.0.1'|LISTEN_HOST = '$relay_ip'|" \
    -e "s|^LISTEN_PORT = 8000|LISTEN_PORT = $relay_port|" \
    "$repository/phpvpn.py" > "$work/relay/phpvpn.py"
grep -q "^LISTEN_HOST = '$relay_ip'" "$work/relay/phpvpn.py" || die "relay listen host not patched"
ip netns exec $ns_relay "$python_server" -B "$work/relay/phpvpn.py" > "$work/relay/relay.log" 2>&1 &
relay_pid=$!
for _ in $(seq 1 60); do
    code=$(ip netns exec $ns_a curl -s -o /dev/null -w '%{http_code}' --max-time 2 "$relay_url" || true)
    [ "$code" = 200 ] && break
    sleep 0.5
done
[ "$code" = 200 ] || die "the relay did not become available"
pass "real Python relay reachable from client namespaces at $relay_url"

# --- instances -------------------------------------------------------------
mkdir -m 0755 "$base"
for name in a b; do mkdir -m 0755 "$base/$name"; done
sock() { echo "$base/$1/run/control-v1.sock"; }
ns_of() { [ "$1" = a ] && echo $ns_a || echo $ns_b; }
cli_on() { local name=$1; shift; "$cli" --socket "$(sock "$name")" "$@"; }
# Commands whose JSON answer only matters through the exit code go to cli.log.
cli_do() { echo "+ phpvpn $*" >> "$work/cli.log"; cli_on "$@" >> "$work/cli.log" 2>&1; }

start_service() {
    local name=$1
    ip netns exec "$(ns_of "$name")" "$service" --foreground \
        --config-dir "$base/$name/config" --state-dir "$base/$name/state" --runtime-dir "$base/$name/run" \
        >> "$work/service-$name.log" 2>&1 &
    service_pid[$name]=$!
    for _ in $(seq 1 100); do
        cli_on "$name" status > /dev/null 2>&1 && return 0
        kill -0 "${service_pid[$name]}" 2> /dev/null || return 1
        sleep 0.1
    done
    return 1
}

# JSON field of the status/diagnostics envelope: status_field <name> <path...>
json_field() { python3 -c 'import json,sys
value=json.load(sys.stdin)
for key in sys.argv[1:]: value=value[key]
print(value)' "$@"; }
vpn_state() { cli_on "$1" status 2> /dev/null | python3 -c 'import json,sys
s=json.load(sys.stdin)["status"]; print(s["state"], s["phase"], s["tunnelReady"])' 2> /dev/null || echo "unreachable"; }
wait_state() { # name "state phase [tunnelReady]" timeout_s, prefix match
    local deadline=$(( $(date +%s) + $3 )) current
    while :; do
        current=$(vpn_state "$1")
        [ "${current#"$2"}" != "$current" ] && return 0
        [ "$(date +%s)" -ge "$deadline" ] && { echo "  $1 state: $current (expected $2)"; return 1; }
        sleep 0.2
    done
}
has_interface() { ip -n "$(ns_of "$1")" link show phpvpn > /dev/null 2>&1; }
has_address() { ip -n "$(ns_of "$1")" -4 addr show dev phpvpn 2> /dev/null | grep -q "inet $2/24"; }
has_route() { ip -n "$(ns_of "$1")" -4 route show 10.99.99.0/24 2> /dev/null | grep -q "dev phpvpn.*metric 5"; }
no_tunnel() { ! has_interface "$1" && [ -z "$(ip -n "$(ns_of "$1")" -4 route show 10.99.99.0/24 2> /dev/null)" ]; }
ping_b() { ip netns exec $ns_a ping "$@" 10.99.99.3 >> "$work/ping.log" 2>&1; }
counter() { cli_on "$1" diagnostics | json_field diagnostics counters "$2"; }

write_config() { # name own peer autoConnect
    python3 - "$work/config-$1.json" "$relay_url" "$2" "$3" "$4" "$payload_key" <<'PY'
import json, sys
path, url, own, peer, auto, key = sys.argv[1:]
octet = int(own.rsplit('.', 1)[1])
document = {
    "masterSetup": {
        "serverSecret": "CHANGE_THIS_SERVER_SECRET",
        "serverUrl": url,
        "httpHeaders": "Cache-Control: no-cache\r\nPragma: no-cache\r\nUser-Agent: phpvpn-linux-e2e",
        "trustAnyTlsCertificate": True,
        "useDefaultServerSecretInWizard": True,
        "vpnCidr": "10.99.99.0/24",
        "virtualIp": own,
        "autoDetectIpAddressInWizard": False,
        "accessKey": ("%02x" % octet) * 32,
        "useDefaultAccessKeyInWizard": True,
        "autoConnectOnStartup": auto == "true",
    },
    "communicationContracts": {peer: {"payloadKey": key}},
}
with open(path, "w", encoding="utf-8") as out:
    json.dump(document, out, indent=2)
PY
}

check "service A starts in its namespace" start_service a
check "service B starts in its namespace" start_service b
check "A reports disconnected/idle" wait_state a "disconnected idle" 10

# --- Wizard IPC ------------------------------------------------------------
if "$probe" --socket "$(sock a)" --relay "$relay_url" --config-path "$base/a/config/config.json" \
        --virtual-ip 10.99.99.2 --document-out "$work/wizard-document.json" > "$work/wizard-probe.log" 2>&1; then
    pass "Wizard IPC open/check url/secret/access/finish with commitId (see wizard-probe.log)"
else
    fail "Wizard IPC probe"; cat "$work/wizard-probe.log"
fi
check "the committed Wizard document is the file at the POSIX config path" \
    cmp -s "$work/wizard-document.json" "$base/a/config/config.json"
cli_on a get-config > "$work/wizard-get-config.txt"
printf '\n' >> "$work/wizard-document.json"
check "phpvpn get-config prints the committed Wizard document" cmp -s "$work/wizard-document.json" "$work/wizard-get-config.txt"
check "config file is 0600 root in a 0700 directory" \
    test "$(stat -c '%a %u' "$base/a/config/config.json") $(stat -c '%a' "$base/a/config")" = "600 0 700"

# --- configure through the CLI ---------------------------------------------
write_config a 10.99.99.2 10.99.99.3 false
write_config b 10.99.99.3 10.99.99.2 false
check "phpvpn set-config A" cli_do a set-config "$work/config-a.json"
check "phpvpn set-config B" cli_do b set-config "$work/config-b.json"
cli_on a get-config > "$work/get-config-a.txt"
printf '\n' | cat "$work/config-a.json" - > "$work/config-a.expected"
check "get-config returns the stored document byte for byte" cmp -s "$work/config-a.expected" "$work/get-config-a.txt"

# --- connect ---------------------------------------------------------------
check "phpvpn connect A" cli_do a connect
check "phpvpn connect B" cli_do b connect
check "A connected/running/tunnelReady" wait_state a "connected running yes" 45
check "B connected/running/tunnelReady" wait_state b "connected running yes" 45
check "A interface phpvpn with 10.99.99.2/24" has_address a 10.99.99.2
check "A on-link route 10.99.99.0/24 metric 5" has_route a
check "B interface phpvpn with 10.99.99.3/24" has_address b 10.99.99.3
ip -n $ns_a -d link show phpvpn > "$work/interface-a.txt" 2>&1 || true

sent_before=$(counter a ipv4BytesSent)
received_before=$(counter b ipv4BytesReceived)
check "ping A -> B over the tunnel" ping_b -c 5 -i 0.3 -W 3
ip netns exec $ns_a ping -c 20 -i 0.2 -W 3 10.99.99.3 > "$work/ping-a-b.txt" 2>&1 || true

# --- TCP transfer ----------------------------------------------------------
cat > "$work/tcp_sink.py" <<'PY'
import hashlib, socket, sys
server = socket.create_server(("10.99.99.3", 5001))
server.settimeout(120)
connection, _ = server.accept()
connection.settimeout(120)
digest, total = hashlib.sha256(), 0
while True:
    chunk = connection.recv(262144)
    if not chunk:
        break
    digest.update(chunk)
    total += len(chunk)
print(total, digest.hexdigest())
PY
cat > "$work/tcp_source.py" <<'PY'
import socket, sys
with open(sys.argv[1], "rb") as source, socket.create_connection(("10.99.99.3", 5001), timeout=120) as connection:
    while True:
        chunk = source.read(262144)
        if not chunk:
            break
        connection.sendall(chunk)
    connection.shutdown(socket.SHUT_WR)
PY
head -c $((transfer_mib * 1024 * 1024)) /dev/urandom > "$work/transfer.bin"
expected="$(stat -c %s "$work/transfer.bin") $(sha256sum "$work/transfer.bin" | cut -d' ' -f1)"
ip netns exec $ns_b timeout 900 python3 "$work/tcp_sink.py" > "$work/tcp-sink.txt" 2> "$work/tcp-sink.err" &
sink=$!
sleep 1
started=$(now_ms)
if ip netns exec $ns_a timeout 900 python3 "$work/tcp_source.py" "$work/transfer.bin" 2> "$work/tcp-source.err"; then
    wait "$sink" || true
    elapsed=$(( $(now_ms) - started ))
    actual=$(cat "$work/tcp-sink.txt")
    if [ "$actual" = "$expected" ]; then
        pass "TCP $transfer_mib MiB A -> B with matching SHA-256 in ${elapsed} ms"
    else
        fail "TCP transfer digest mismatch: expected '$expected' got '$actual'"
    fi
else
    kill "$sink" 2> /dev/null || true
    fail "TCP transfer failed ($(cat "$work/tcp-source.err"))"
fi
rm -f "$work/transfer.bin"
sent_after=$(counter a ipv4BytesSent)
received_after=$(counter b ipv4BytesReceived)
check "diagnostics counters grow (A sent $sent_before -> $sent_after, B received $received_before -> $received_after)" \
    test "$sent_after" -gt $((sent_before + transfer_mib * 1024 * 1024)) -a "$received_after" -gt $((received_before + transfer_mib * 1024 * 1024))
cli_on a diagnostics > "$work/diagnostics-a.json"

# --- disconnect ------------------------------------------------------------
started=$(now_ms)
check "phpvpn disconnect A" cli_do a disconnect
check "A disconnected within 30 s" wait_state a "disconnected idle" 30
elapsed=$(( $(now_ms) - started ))
echo "  disconnect completed after ${elapsed} ms"
check "A interface and route are gone after disconnect" no_tunnel a

# --- kill -9 and recovery --------------------------------------------------
check "phpvpn connect A again" cli_do a connect
check "A connected again" wait_state a "connected running yes" 45
check "ownership journal exists while connected" test -s "$base/a/state/ownership.json"
kill -KILL "${service_pid[a]}"
wait "${service_pid[a]}" 2> /dev/null || true
unset 'service_pid[a]'
for _ in $(seq 1 50); do no_tunnel a && break; sleep 0.1; done
check "the kernel removed interface and route after kill -9" no_tunnel a
check "the ownership journal survived the crash" test -s "$base/a/state/ownership.json"
check "service A restarts after kill -9 (stale socket replaced)" start_service a
check "A is disconnected after the restart" wait_state a "disconnected idle" 10
check "recovery retired the journal cleanly" test ! -e "$base/a/state/ownership.json"
check "recovery logged the retired journal" grep -q "retiring the journal of a driver-owned interface already removed by the OS" "$work/service-a.log"
check "phpvpn connect A after recovery" cli_do a connect
check "A connected after recovery" wait_state a "connected running yes" 45
check "ping A -> B after recovery" ping_b -c 3 -W 3

# --- autoConnectOnStartup and SIGTERM with an active connection -------------
write_config a 10.99.99.2 10.99.99.3 true
check "set-config A with autoConnectOnStartup=true" cli_do a set-config "$work/config-a.json"
started=$(now_ms)
kill -TERM "${service_pid[a]}"
stopped=no
for _ in $(seq 1 450); do kill -0 "${service_pid[a]}" 2> /dev/null || { stopped=yes; break; }; sleep 0.1; done
wait "${service_pid[a]}" 2> /dev/null && term_status=0 || term_status=$?
elapsed=$(( $(now_ms) - started ))
unset 'service_pid[a]'
check "SIGTERM with an active connection stops A within 45 s (${elapsed} ms, exit $term_status)" test $stopped = yes -a "$term_status" = 0
check "SIGTERM teardown removed interface and route" no_tunnel a
check "SIGTERM teardown removed the socket and the journal" test ! -e "$(sock a)" -a ! -e "$base/a/state/ownership.json"
check "service A restarts with autoConnectOnStartup=true" start_service a
check "A connects on startup without a connect command" wait_state a "connected running yes" 45
check "ping A -> B after automatic startup connect" ping_b -c 3 -W 3
write_config a 10.99.99.2 10.99.99.3 false
check "set-config A back to autoConnectOnStartup=false" cli_do a set-config "$work/config-a.json"

# --- singleton -------------------------------------------------------------
second=$(ip netns exec $ns_a "$service" --foreground --config-dir "$base/a/config" --state-dir "$base/a/state" \
    --runtime-dir "$base/a/run" 2>&1 >/dev/null; echo "exit=$?")
check "a second instance on the same runtime directory is refused" grep -q "exit=1" <<< "$second"
check "the first instance still answers" cli_on a status > /dev/null

# --- final stop and main namespace ------------------------------------------
for name in a b; do
    kill -TERM "${service_pid[$name]}"
    wait "${service_pid[$name]}" 2> /dev/null || true
    unset "service_pid[$name]"
    check "instance $name stopped cleanly" no_tunnel "$name"
done
main_baseline > "$work/main-after.txt"
check "main namespace default routes, route count and DNS unchanged" cmp -s "$work/main-before.txt" "$work/main-after.txt"
check "no phpvpn interface in the main namespace" bash -c '! ip link show phpvpn > /dev/null 2>&1'

echo "RESULT: $passes passed, $failures failed"
[ "$failures" = 0 ]
