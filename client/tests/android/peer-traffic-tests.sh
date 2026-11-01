#!/usr/bin/env bash
# WP11 host-driven peer scenarios on one emulator (plan-android-v001 T-A03,
# T-A04 and the Wi-Fi <-> mobile data switch): the emulator connects as a
# second peer to the relay of a desktop client that runs on the host (the
# Windows client, or the Linux client in WSL) and traffic goes both ways
# across 10.99.99.0/24. Every check prints PASS, FAIL or INFO (measurement).
#
# Usage (Git Bash on the Windows host, worktree root, debug APK built):
#   client/tests/android/peer-traffic-tests.sh <serial> <relay url seen by the emulator> \
#       <peer config.json> <own ip> <app-debug.apk> [tests]
#   e.g. emulator-5556 http://10.0.2.2/phpvpn.php C:/ProgramData/phpvpn/config.json 10.99.99.3 \
#        client/android/app/build/outputs/apk/debug/app-debug.apk "connect ta03 ta04 netswitch disconnect"
# Preconditions: the peer is connected (state connected, tunnel ready) to the
# same relay, its contract for <own ip> exists, the relay uses the default
# access keys (profile wizard-default), and inbound ICMP and TCP from
# 10.99.99.0/24 reach the peer (Windows firewall rule for ping and an
# inbound rule for $PEER_PYTHON).
# Environment: PEER_PYTHON (python of the echo server on the peer, default
# python), PEER_PING (default ping, Windows syntax -n/-w when it is the
# Windows ping).
set -u
export MSYS_NO_PATHCONV=1

SERIAL=$1
URL=$2
PEER_CONFIG=$3
OWN_IP=$4
APK=$5
TESTS=${6:-"connect ta03 ta04 netswitch disconnect"}
PEER_PYTHON=${PEER_PYTHON:-python}
PEER_PING=${PEER_PING:-ping}
HERE=$(dirname "$0")
PKG=com.pilandrome.phpvpn
RECEIVER=$PKG/phpvpn.android.debug.DebugVpnCommandReceiver
WORK=${WP11_WORK:-work/wp11}
FAILURES=0
mkdir -p "$WORK"

adb_() { adb -s "$SERIAL" "$@"; }
sh_() { adb_ shell "$@" | tr -d '\r'; }
log() { echo "[$(date +%H:%M:%S)] $*"; }
check() { if [ "$2" = 0 ]; then echo "PASS $1 ${3:-}"; else echo "FAIL $1 ${3:-}"; FAILURES=$((FAILURES + 1)); fi; }
cmd_() { sh_ am broadcast -n "$RECEIVER" -a "phpvpn.android.debug.$1" "${@:2}" >/dev/null; }
status_() { cmd_ STATUS; sleep 1; sh_ logcat -d -v brief -s phpvpn-debug:I | grep " status " | tail -1 | sed 's/.*status //'; }
wait_status() {
  local deadline=$((SECONDS + $2)) s=""
  while [ $SECONDS -lt $deadline ]; do
    s=$(status_)
    if echo "$s" | grep -Eq "$1"; then echo "$s"; return 0; fi
  done
  echo "$s"; return 1
}
PEER_IP=$(python -I -c "import json,sys; print(json.load(open(sys.argv[1], encoding='utf-8-sig'))['masterSetup']['virtualIp'])" "$PEER_CONFIG")

setup() {
  log "install $APK on $SERIAL (API $(sh_ getprop ro.build.version.sdk)), own $OWN_IP, peer $PEER_IP, relay $URL"
  adb_ install -r -t -g "$APK" | tail -1
  sh_ am force-stop "$PKG"
  sh_ appops set "$PKG" ACTIVATE_VPN allow
  sh_ input keyevent KEYCODE_WAKEUP
  sh_ wm dismiss-keyguard
  sh_ am start -W -n "$PKG/phpvpn.android.MainActivity" >/dev/null
  sleep 3
  sh_ logcat -c
  cmd_ DISCONNECT
  wait_status "state=(disconnected|error) phase=idle" 30 >/dev/null
  local doc="$WORK/peer-config-$SERIAL.json"
  python -I "$HERE/make_peer_config.py" "$PEER_CONFIG" "$OWN_IP" "$URL" "$doc" || exit 1
  adb_ shell "run-as $PKG sh -c 'cat > files/wp11-peer.json'" < "$doc"
  rm -f "$doc"
  cmd_ CONFIGURE_FILE --es file wp11-peer.json
  sleep 2
  local r; r=$(sh_ logcat -d -v brief -s phpvpn-debug:I | grep "configure-file" | tail -1)
  check "configure peer document" "$(echo "$r" | grep -q 'configure-file ok' && echo 0 || echo 1)" "${r##*: }"
}

connect() {
  log "connect"
  local begun=$SECONDS s
  cmd_ CONNECT
  s=$(wait_status "state=connected phase=running tunnel=yes" 90)
  check "connected" $? "$s ($((SECONDS - begun)) s)"
  check "tun address $OWN_IP" "$(sh_ ip -4 addr show | grep -q "inet $OWN_IP/" && echo 0 || echo 1)"
}

ping_device() { # ping_device <ip> <count>: prints "loss% min/avg/max"
  local out; out=$(sh_ ping -c "$2" -W 3 "$1")
  local loss rtt
  loss=$(echo "$out" | grep -o '[0-9.]*% packet loss' | cut -d% -f1)
  rtt=$(echo "$out" | grep -o 'rtt[^=]*= [0-9./]*' | sed 's/.*= //')
  echo "${loss:-100} ${rtt:--}"
}

ping_host() { # Windows ping from the host: prints "loss% min/avg/max"
  local out; out=$($PEER_PING -n "$2" -w 3000 "$1" | tr -d '\r')
  local sent received
  sent=$2
  received=$(echo "$out" | grep -c "TTL=")
  local times; times=$(echo "$out" | grep -o 'time[=<][0-9]*ms' | grep -o '[0-9]*' | sort -n)
  local min max avg
  min=$(echo "$times" | head -1); max=$(echo "$times" | tail -1)
  avg=$(echo "$times" | awk '{s+=$1} END {if (NR) printf "%.0f", s/NR}')
  echo "$(( (sent - received) * 100 / sent )) ${min:-}/${avg:-}/${max:-}"
}

ta03() {
  log "T-A03: emulator $OWN_IP <-> peer $PEER_IP"
  local r loss
  r=$(ping_device "$PEER_IP" 10); loss=${r%% *}
  check "T-A03 ping emulator -> peer" "$([ "${loss%.*}" -le 10 ] && echo 0 || echo 1)" "loss ${loss}% rtt min/avg/max/mdev ${r#* } ms"
  r=$(ping_host "$OWN_IP" 10); loss=${r%% *}
  check "T-A03 ping peer -> emulator" "$([ "$loss" -le 10 ] && echo 0 || echo 1)" "loss ${loss}% rtt min/avg/max/mdev ${r#* } ms"

  # TCP echo, emulator as client: an echo server on the peer address.
  local port=7701 srvlog="$WORK/echo-server-$SERIAL.log"
  "$PEER_PYTHON" -I "$HERE/tcp_echo.py" server "$PEER_IP" $port 300 > "$srvlog" 2>&1 &
  local srv=$!
  sleep 2
  local size
  for size in 1024 65536 1048576; do
    sh_ "head -c $size /dev/urandom > /data/local/tmp/wp11-echo-in && (echo $size; cat /data/local/tmp/wp11-echo-in) > /data/local/tmp/wp11-echo-req"
    local t0 t1 out
    # /proc/uptime in centiseconds: toybox date +%N is not reliable on every API level.
    out=$(sh_ "t0=\$(tr -d . < /proc/uptime | cut -d' ' -f1); nc -w 10 -q 120 $PEER_IP $port < /data/local/tmp/wp11-echo-req > /data/local/tmp/wp11-echo-out; t1=\$(tr -d . < /proc/uptime | cut -d' ' -f1); echo \$(( (t1 - t0) * 10 ))")
    local same; same=$(sh_ "cmp -s /data/local/tmp/wp11-echo-in /data/local/tmp/wp11-echo-out && echo same || echo differ")
    local ms=${out##*$'\n'}
    local rate=""
    [ -n "$ms" ] && [ "$ms" -gt 0 ] 2>/dev/null && rate="$(awk -v b="$size" -v m="$ms" 'BEGIN {printf "%.1f", b / 1024 / (m / 1000)}') KiB/s round trip"
    check "T-A03 TCP echo emulator -> peer $size B" "$([ "$same" = same ] && echo 0 || echo 1)" "${ms} ms $rate"
  done
  kill $srv 2>/dev/null
  sh_ rm -f /data/local/tmp/wp11-echo-in /data/local/tmp/wp11-echo-req /data/local/tmp/wp11-echo-out

  # TCP echo, peer as client: toybox nc as echo server on the emulator.
  port=7702
  adb_ shell "nc -L -p $port cat" > /dev/null 2>&1 &
  local ncpid=$!
  sleep 2
  for size in 1024 65536 1048576; do
    out=$("$PEER_PYTHON" -I "$HERE/tcp_echo.py" client "$OWN_IP" $port $size 120)
    check "T-A03 TCP echo peer -> emulator $size B" "$(echo "$out" | grep -q '^echo ok' && echo 0 || echo 1)" "$out"
  done
  sh_ pkill -f "nc -L -p $port" >/dev/null 2>&1
  kill $ncpid 2>/dev/null
}

# T-A04: the T-A01b checks of other apps (uid 2000) during a real connection.
ta04() {
  log "T-A04: other apps during the connection (DNS, HTTPS, IPv6, LAN, routes)"
  local fresh="wp11-$(date +%s).1-2-3-4.sslip.io" out
  out=$(sh_ "
    r=\$(ping -c 1 -W 3 www.google.com 2>&1 | head -1)
    case \"\$r\" in *'PING www.google.com ('*) echo dns=ok;; *) echo \"dns=fail \$r\";; esac
    f=\$(ping -c 1 -W 3 $fresh 2>&1 | head -1)
    case \"\$f\" in *'(1.2.3.4)'*) echo dnsfresh=ok;; *) echo \"dnsfresh=fail \$f\";; esac
    if echo | nc -w 5 www.google.com 443 >/dev/null 2>&1; then echo tcp443=ok; else echo tcp443=fail; fi
    if [ -x /data/local/tmp/phpvpn-tls-probe ]; then
      t=\$(/data/local/tmp/phpvpn-tls-probe https://www.google.com/ 2>&1 | grep 'verify=system-store' | head -1)
      case \"\$t\" in *'curl=0 '*) echo https=ok;; *) echo \"https=fail \$t\";; esac
    else echo https=missing; fi
    if ping6 -c 1 -W 3 fec0::2 >/dev/null 2>&1; then echo ipv6=ok; else echo ipv6=fail; fi
    if ping -c 1 -W 3 10.0.2.2 >/dev/null 2>&1; then echo lan=ok; else echo lan=fail; fi
    if echo | nc -w 3 10.0.2.2 5037 >/dev/null 2>&1; then echo lantcp=ok; else echo lantcp=fail; fi
    echo route_internet=\$(ip route get 8.8.8.8 uid 2000 2>&1 | grep -o 'dev [a-z0-9_]*' | head -1)
    echo route_vpn=\$(ip route get $PEER_IP uid 2000 2>&1 | grep -o 'dev [a-z0-9_]*' | head -1)
    echo route_lan=\$(ip route get 10.0.2.2 uid 2000 2>&1 | grep -o 'dev [a-z0-9_]*' | head -1)
    echo route_ipv6=\$(ip -6 route get 2001:4860:4860::8888 uid 2000 2>&1 | grep -o 'dev [a-z0-9_]*' | head -1)
  ")
  local key value
  for key in dns dnsfresh tcp443 https ipv6 lan lantcp; do
    value=$(echo "$out" | grep "^$key=" | head -1 | cut -d= -f2-)
    check "T-A04 $key" "$([ "${value%% *}" = ok ] && echo 0 || echo 1)" "$value"
  done
  local internet vpn lan v6
  internet=$(echo "$out" | grep '^route_internet=' | cut -d= -f2-)
  vpn=$(echo "$out" | grep '^route_vpn=' | cut -d= -f2-)
  lan=$(echo "$out" | grep '^route_lan=' | cut -d= -f2-)
  v6=$(echo "$out" | grep '^route_ipv6=' | cut -d= -f2-)
  check "T-A04 internet route outside the tunnel" "$(echo "$internet" | grep -qv tun && echo 0 || echo 1)" "$internet"
  check "T-A04 vpnCidr route into the tunnel" "$(echo "$vpn" | grep -q tun && echo 0 || echo 1)" "$vpn"
  check "T-A04 LAN route outside the tunnel" "$(echo "$lan" | grep -qv tun && echo 0 || echo 1)" "$lan"
  check "T-A04 IPv6 route outside the tunnel" "$(echo "$v6" | grep -qv tun && echo 0 || echo 1)" "${v6:-no route}"
  local vpndns; vpndns=$(sh_ dumpsys connectivity | grep -A3 "VPN CONNECTED" | grep -o 'DnsAddresses: \[[^]]*\]' | head -1)
  echo "INFO T-A04 VPN network ${vpndns:-DnsAddresses not listed}"
  r=$(ping_device "$PEER_IP" 3)
  check "T-A04 peer still reachable" "$([ "${r%% *}" != 100 ] && echo 0 || echo 1)" "loss ${r%% *}%"
}

# Wi-Fi -> mobile data -> Wi-Fi with the connection up (emulator radio).
netswitch() {
  log "T-A04 network switch: Wi-Fi off (mobile data), then on"
  local begun s r
  sh_ svc wifi disable
  begun=$SECONDS
  sleep 3
  echo "INFO default network after Wi-Fi off: $(sh_ ip route get 8.8.8.8 | grep -o 'dev [a-z0-9_]*' | head -1)"
  s=$(wait_status "state=connected phase=running tunnel=yes" 120)
  r=$(ping_device "$PEER_IP" 3)
  local deadline=$((SECONDS + 120))
  while [ "${r%% *}" = 100 ] && [ $SECONDS -lt $deadline ]; do sleep 3; r=$(ping_device "$PEER_IP" 3); done
  check "T-A04 mobile data: peer reachable again" "$([ "${r%% *}" != 100 ] && echo 0 || echo 1)" "after $((SECONDS - begun)) s, $s, recoveries: $(sh_ logcat -d -v brief -s phpvpn:I | grep -ci 'recover')"
  sh_ svc wifi enable
  begun=$SECONDS
  sleep 5
  echo "INFO default network after Wi-Fi on: $(sh_ ip route get 8.8.8.8 | grep -o 'dev [a-z0-9_]*' | head -1)"
  s=$(wait_status "state=connected phase=running tunnel=yes" 120)
  r=$(ping_device "$PEER_IP" 3)
  deadline=$((SECONDS + 120))
  while [ "${r%% *}" = 100 ] && [ $SECONDS -lt $deadline ]; do sleep 3; r=$(ping_device "$PEER_IP" 3); done
  check "T-A04 Wi-Fi back: peer reachable" "$([ "${r%% *}" != 100 ] && echo 0 || echo 1)" "after $((SECONDS - begun)) s, $s"
}

disconnect() {
  log "disconnect"
  cmd_ DISCONNECT
  local s; s=$(wait_status "state=disconnected phase=idle" 30)
  check "disconnected" $? "$s"
}

setup
for test in $TESTS; do "$test"; done
echo "failures: $FAILURES"
exit $FAILURES
