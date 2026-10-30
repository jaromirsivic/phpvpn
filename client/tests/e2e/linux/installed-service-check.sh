#!/bin/bash
# Real connection check of the INSTALLED phpvpn.service (plan-v001 5/WP06 step
# 5, reused by the final acceptance with a longer duration). Root only.
#
#   sudo bash tests/e2e/linux/installed-service-check.sh [minutes] [build-dir]
#
# The installed service (systemd, main network namespace, /usr/bin/phpvpn) is
# client A 10.99.99.2. The real Python relay runs in the main namespace on
# 127.0.0.1:18094 only. Peer B 10.99.99.3 is an isolated phpvpn-service
# instance from build-dir in the private namespace phpvpn-inst-b; it reaches
# the relay through a Unix socket bridge, so the check adds no interface,
# address or route to the main namespace other than the phpvpn tunnel of the
# installed service itself. The installed service is configured with the
# unprivileged CLI (the invoking sudo user), connects, pings B, transfers
# 50 MiB with SHA-256 and holds the connection for [minutes] (default 5) with
# a ping every second, then disconnects. Default routes, route count and DNS
# (resolvectl status) must be unchanged afterwards. The previous configuration
# of the installed service is restored and the service stays installed,
# running and disconnected.
set -euo pipefail

minutes=${1:-5}
client=$(cd "$(dirname "$0")/../../.." && pwd)
repository=$(cd "$client/.." && pwd)
build=$(cd "${2:-$repository/work/build/linux-release}" && pwd)
python_server=${PHPVPN_SERVER_PYTHON:-python3}
work=${PHPVPN_E2E_WORK:-$repository/work/installed-check-$(date +%Y%m%d-%H%M%S)}
user=${SUDO_USER:-nobody}
ns=phpvpn-inst-b
base=/run/phpvpn-inst
relay_url=http://127.0.0.1:18094/phpvpn.php
payload_key=918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d

failures=0
passes=0
pass() { passes=$((passes + 1)); echo "PASS: $*"; }
fail() { failures=$((failures + 1)); echo "FAIL: $*"; }
die() { echo "FATAL: $*"; exit 1; }
check() { local message=$1; shift; if "$@"; then pass "$message"; else fail "$message"; fi; }
quiet() { "$@" >> "$work/commands.log" 2>&1; }

[ "$(id -u)" = 0 ] || die "run as root"
[ -x /usr/bin/phpvpn ] && [ -x /usr/libexec/phpvpn/phpvpn-service ] || die "phpvpn is not installed in /usr"
systemctl is-active --quiet phpvpn.service || die "phpvpn.service is not active"
! ip netns list | grep -qw $ns || die "network namespace $ns already exists"
[ ! -e "$base" ] || die "$base already exists"
mkdir -p "$work"
echo "work directory: $work"
as_user() { setpriv --reuid="$(id -u "$user")" --regid="$(id -g "$user")" --init-groups "$@"; }
main_phpvpn() { as_user /usr/bin/phpvpn "$@"; }
peer_phpvpn() { "$build/apps/phpvpn" --socket "$base/b/run/control-v1.sock" "$@"; }

baseline() {
    ip -4 route show default
    ip -6 route show default
    ip -4 route show table all | grep -vc 'dev phpvpn' || true
    resolvectl status 2> /dev/null | grep -v 'Current DNS Server' || cat /etc/resolv.conf
}
baseline > "$work/main-before.txt"
ip route > "$work/ip-route-before.txt"
resolvectl status > "$work/resolvectl-before.txt" 2>&1 || true
main_phpvpn get-config > "$work/previous-config.json" 2> /dev/null || true

relay_pid= bridge_main= bridge_peer= peer_pid=
cleanup() {
    set +e
    main_phpvpn disconnect > /dev/null 2>&1
    [ -n "$peer_pid" ] && kill -TERM "$peer_pid" 2> /dev/null && wait "$peer_pid" 2> /dev/null
    for pid in $bridge_peer $bridge_main $relay_pid; do kill "$pid" 2> /dev/null; wait "$pid" 2> /dev/null; done
    ip netns del $ns 2> /dev/null
    rm -rf "$base"
    if [ -s "$work/previous-config.json" ]; then
        head -c -1 "$work/previous-config.json" > "$work/previous-config.restore"
        chmod 0644 "$work/previous-config.restore"
        main_phpvpn set-config "$work/previous-config.restore" > /dev/null && echo "previous configuration restored"
    fi
    echo "passes=$passes failures=$failures (logs in $work)"
}
trap cleanup EXIT

# --- relay in the main namespace, loopback only ---------------------------
mkdir -p "$work/relay" "$base"
chmod 0755 "$base"
sed -e "s|^STORAGE_DRIVER = 'disk'|STORAGE_DRIVER = 'memory'|" -e "s|^LISTEN_PORT = 8000|LISTEN_PORT = 18094|" \
    "$repository/server/python/phpvpn.py" > "$work/relay/phpvpn.py"
"$python_server" -B "$work/relay/phpvpn.py" > "$work/relay/relay.log" 2>&1 &
relay_pid=$!
for _ in $(seq 1 60); do
    [ "$(curl -s -o /dev/null -w '%{http_code}' --max-time 2 $relay_url || true)" = 200 ] && break
    sleep 0.5
done
python3 "$client/tests/e2e/linux/tcp_unix_bridge.py" unix-to-tcp "$base/relay.sock" 127.0.0.1:18094 &
bridge_main=$!

# --- peer B in its own namespace ------------------------------------------
ip netns add $ns
ip -n $ns link set lo up
for _ in $(seq 1 50); do [ -S "$base/relay.sock" ] && break; sleep 0.1; done
ip netns exec $ns python3 "$client/tests/e2e/linux/tcp_unix_bridge.py" tcp-to-unix 127.0.0.1:18094 "$base/relay.sock" &
bridge_peer=$!
mkdir -m 0755 "$base/b"
ip netns exec $ns "$build/apps/phpvpn-service" --foreground --config-dir "$base/b/config" \
    --state-dir "$base/b/state" --runtime-dir "$base/b/run" > "$work/peer-service.log" 2>&1 &
peer_pid=$!
for _ in $(seq 1 100); do peer_phpvpn status > /dev/null 2>&1 && break; sleep 0.1; done

write_config() { # path own peer
    python3 - "$1" "$relay_url" "$2" "$3" "$payload_key" <<'PY'
import json, sys
path, url, own, peer, key = sys.argv[1:]
octet = int(own.rsplit('.', 1)[1])
json.dump({"masterSetup": {
    "serverSecret": "CHANGE_THIS_SERVER_SECRET", "serverUrl": url,
    "httpHeaders": "Cache-Control: no-cache\r\nUser-Agent: phpvpn-installed-check",
    "trustAnyTlsCertificate": True, "vpnCidr": "10.99.99.0/24", "virtualIp": own,
    "autoDetectIpAddressInWizard": False, "accessKey": ("%02x" % octet) * 32,
    "autoConnectOnStartup": False},
    "communicationContracts": {peer: {"payloadKey": key}}}, open(path, "w"), indent=2)
PY
    chmod 0644 "$1"
}
write_config "$base/config-a.json" 10.99.99.2 10.99.99.3
write_config "$base/config-b.json" 10.99.99.3 10.99.99.2
check "installed service accepts set-config from the unprivileged user $user" quiet main_phpvpn set-config "$base/config-a.json"
check "peer B accepts set-config" quiet peer_phpvpn set-config "$base/config-b.json"

state() { "$@" status 2> /dev/null | python3 -c 'import json,sys
s=json.load(sys.stdin)["status"]; print(s["state"], s["phase"], s["tunnelReady"])' 2> /dev/null || echo unreachable; }
wait_state() { # expected timeout command...
    local expected=$1 deadline=$(( $(date +%s) + $2 )) current
    shift 2
    while :; do
        current=$(state "$@")
        [ "${current#"$expected"}" != "$current" ] && return 0
        [ "$(date +%s)" -ge "$deadline" ] && { echo "  state: $current (expected $expected)"; return 1; }
        sleep 0.2
    done
}
check "phpvpn connect (installed service)" quiet main_phpvpn connect
check "peer connect" quiet peer_phpvpn connect
check "installed service connected/running/tunnelReady" wait_state "connected running yes" 45 main_phpvpn
check "peer connected/running/tunnelReady" wait_state "connected running yes" 45 peer_phpvpn
ip addr show dev phpvpn > "$work/phpvpn-interface.txt" 2>&1 || true
ip route show 10.99.99.0/24 >> "$work/phpvpn-interface.txt" 2>&1 || true
check "main namespace interface phpvpn has 10.99.99.2/24" grep -q "inet 10.99.99.2/24" "$work/phpvpn-interface.txt"
check "on-link route 10.99.99.0/24 dev phpvpn metric 5" grep -q "10.99.99.0/24 dev phpvpn.*metric 5" "$work/phpvpn-interface.txt"
check "NetworkManager leaves phpvpn unmanaged" bash -c "nmcli -t -f DEVICE,STATE device status | grep -q '^phpvpn:unmanaged$'"
check "ping from the main namespace to peer B over the tunnel" quiet ping -c 5 -W 3 10.99.99.3

cat > "$work/tcp_sink.py" <<'PY'
import hashlib, socket
server = socket.create_server(("10.99.99.3", 5001)); server.settimeout(120)
connection, _ = server.accept(); connection.settimeout(120)
digest, total = hashlib.sha256(), 0
while chunk := connection.recv(262144):
    digest.update(chunk); total += len(chunk)
print(total, digest.hexdigest())
PY
head -c $((50 * 1024 * 1024)) /dev/urandom > "$work/transfer.bin"
expected="$(stat -c %s "$work/transfer.bin") $(sha256sum "$work/transfer.bin" | cut -d' ' -f1)"
ip netns exec $ns timeout 900 python3 "$work/tcp_sink.py" > "$work/tcp-sink.txt" 2>&1 &
sink=$!
sleep 1
started=$(date +%s%3N)
python3 -c 'import socket,sys
with open(sys.argv[1],"rb") as f, socket.create_connection(("10.99.99.3",5001),timeout=120) as c:
    while chunk := f.read(262144): c.sendall(chunk)
    c.shutdown(socket.SHUT_WR)' "$work/transfer.bin" || true
wait "$sink" || true
elapsed=$(( $(date +%s%3N) - started ))
rm -f "$work/transfer.bin"
check "TCP 50 MiB main -> peer with matching SHA-256 (${elapsed} ms)" test "$(cat "$work/tcp-sink.txt")" = "$expected"

echo "holding the connection for $minutes minute(s) with a ping every second"
count=$((minutes * 60))
ping -c "$count" -i 1 -W 3 10.99.99.3 > "$work/ping-hold.txt" 2>&1 || true
tail -3 "$work/ping-hold.txt"
received=$(sed -n 's/.* \([0-9]*\) received.*/\1/p' "$work/ping-hold.txt")
still=$(state main_phpvpn)
check "held $minutes min: ${received:-0}/$count pings answered, state after the hold: $still" \
    test "${received:-0}" -ge $((count * 99 / 100)) -a "${still#connected running yes}" != "$still"
main_phpvpn diagnostics > "$work/diagnostics.json"

started=$(date +%s%3N)
check "phpvpn disconnect (installed service)" quiet main_phpvpn disconnect
check "installed service disconnected within 30 s" wait_state "disconnected idle" 30 main_phpvpn
echo "  disconnect completed after $(( $(date +%s%3N) - started )) ms"
check "phpvpn interface and route removed" bash -c '! ip link show phpvpn > /dev/null 2>&1 && [ -z "$(ip route show 10.99.99.0/24)" ]'
baseline > "$work/main-after.txt"
ip route > "$work/ip-route-after.txt"
resolvectl status > "$work/resolvectl-after.txt" 2>&1 || true
check "default routes, route count and DNS (resolvectl) unchanged" cmp -s "$work/main-before.txt" "$work/main-after.txt"
check "ip route output identical before and after" cmp -s "$work/ip-route-before.txt" "$work/ip-route-after.txt"
journalctl -u phpvpn --since "-$((minutes + 10)) min" --no-pager -o short-iso > "$work/journal.txt" 2>&1 || true

echo "RESULT: $passes passed, $failures failed"
[ "$failures" = 0 ]
