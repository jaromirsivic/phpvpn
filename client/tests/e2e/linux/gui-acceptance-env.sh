#!/bin/bash
# Relay and peer for the interactive tray acceptance on a desktop VM (WP13).
#
#   sudo bash tests/e2e/linux/gui-acceptance-env.sh start [build-dir]
#   sudo bash tests/e2e/linux/gui-acceptance-env.sh stop
#
# start runs the real Python relay (memory storage) on 127.0.0.1:18094 in the
# main namespace and an isolated peer phpvpn-service 10.99.99.3 in the private
# network namespace phpvpn-gui-peer, reaching the relay through a Unix socket
# bridge like installed-service-check.sh. The peer connects and stays up until
# stop. The installed service is not touched: the tester configures it through
# the tray Wizard / Communication contracts with the printed values.
# PHPVPN_SERVER_PYTHON is the interpreter with requirements-server.txt.
set -euo pipefail

action=${1:?usage: gui-acceptance-env.sh start|stop [build-dir]}
client=$(cd "$(dirname "$0")/../../.." && pwd)
repository=$(cd "$client/.." && pwd)
python_server=${PHPVPN_SERVER_PYTHON:-python3}
ns=phpvpn-gui-peer
base=/run/phpvpn-gui
relay_url=http://127.0.0.1:18094/phpvpn.php
payload_key=918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d

[ "$(id -u)" = 0 ] || { echo "run as root"; exit 1; }

stop() {
    for name in peer bridge-peer bridge-main relay; do
        if [ -f "$base/$name.pid" ]; then
            kill "$(cat "$base/$name.pid")" 2> /dev/null || true
            rm -f "$base/$name.pid"
        fi
    done
    sleep 1
    ip netns del $ns 2> /dev/null || true
    rm -rf "$base"
    echo "stopped"
}

if [ "$action" = stop ]; then stop; exit 0; fi
[ "$action" = start ] || { echo "unknown action $action"; exit 2; }
build=$(cd "${2:-$repository/work/build/linux-release}" && pwd)
[ ! -e "$base" ] || { echo "$base exists, run stop first"; exit 1; }
! ip netns list | grep -qw $ns || { echo "namespace $ns exists, run stop first"; exit 1; }

mkdir -m 0755 -p "$base/relay" "$base/b"
sed -e "s|^STORAGE_DRIVER = 'disk'|STORAGE_DRIVER = 'memory'|" -e "s|^LISTEN_PORT = 8000|LISTEN_PORT = 18094|" \
    "$repository/phpvpn.py" > "$base/relay/phpvpn.py"
setsid "$python_server" -B "$base/relay/phpvpn.py" > "$base/relay/relay.log" 2>&1 < /dev/null &
echo $! > "$base/relay.pid"
for _ in $(seq 1 100); do
    [ "$(curl -s -o /dev/null -w '%{http_code}' --max-time 2 $relay_url || true)" = 200 ] && break
    sleep 0.2
done
setsid python3 "$client/tests/e2e/linux/tcp_unix_bridge.py" unix-to-tcp "$base/relay.sock" 127.0.0.1:18094 \
    > "$base/bridge-main.log" 2>&1 < /dev/null &
echo $! > "$base/bridge-main.pid"
ip netns add $ns
ip -n $ns link set lo up
for _ in $(seq 1 50); do [ -S "$base/relay.sock" ] && break; sleep 0.1; done
setsid ip netns exec $ns python3 "$client/tests/e2e/linux/tcp_unix_bridge.py" tcp-to-unix 127.0.0.1:18094 \
    "$base/relay.sock" > "$base/bridge-peer.log" 2>&1 < /dev/null &
echo $! > "$base/bridge-peer.pid"
setsid ip netns exec $ns "$build/apps/phpvpn-service" --foreground --config-dir "$base/b/config" \
    --state-dir "$base/b/state" --runtime-dir "$base/b/run" > "$base/peer-service.log" 2>&1 < /dev/null &
echo $! > "$base/peer.pid"
peer() { "$build/apps/phpvpn" --socket "$base/b/run/control-v1.sock" "$@"; }
for _ in $(seq 1 100); do peer status > /dev/null 2>&1 && break; sleep 0.1; done
python3 - "$base/config-b.json" "$relay_url" "$payload_key" <<'PY'
import json, sys
path, url, key = sys.argv[1:]
json.dump({"masterSetup": {
    "serverSecret": "CHANGE_THIS_SERVER_SECRET", "serverUrl": url,
    "httpHeaders": "Cache-Control: no-cache\r\nUser-Agent: phpvpn-gui-peer",
    "trustAnyTlsCertificate": True, "vpnCidr": "10.99.99.0/24", "virtualIp": "10.99.99.3",
    "autoDetectIpAddressInWizard": False, "accessKey": "03" * 32,
    "autoConnectOnStartup": False},
    "communicationContracts": {"10.99.99.2": {"payloadKey": key}}}, open(path, "w"), indent=2)
PY
peer set-config "$base/config-b.json" > /dev/null
peer connect > /dev/null
for _ in $(seq 1 60); do peer status 2> /dev/null | grep -q '"state": "connected"' && break; sleep 0.5; done
peer status | grep -E '"state"|"phase"|"tunnelReady"' || true
cat <<TEXT

Relay:          $relay_url   (server secret CHANGE_THIS_SERVER_SECRET)
Peer:           10.99.99.3 connected (namespace $ns)
Values for the installed client (tray Wizard / contracts):
  Server URL      $relay_url
  Server secret   CHANGE_THIS_SERVER_SECRET
  Virtual IP      10.99.99.2   (VPN CIDR 10.99.99.0/24)
  Access key      $(printf '02%.0s' $(seq 1 32))
  Contract        10.99.99.3 payload key $payload_key
Stop with: sudo bash $0 stop
TEXT
