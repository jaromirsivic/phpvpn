#!/usr/bin/env bash
# WP11 host-driven scenarios on one emulator that need the relay to go away,
# the device to idle or a real reboot (plan-android-v001 T-A08, T-A12 with a
# reboot, T-A15, T-A20 on an emulator, T-A21). Connect, Disconnect and status
# go through the debug-only DebugVpnCommandReceiver (production ConnectActions
# path); facts come from logcat, dumpsys and the UI hierarchy. Every check
# prints PASS, FAIL, INFO (measurement) or NOT RUN.
#
# Usage (Git Bash on the Windows host, worktree root, debug APK built):
#   client/tests/android/wp11-scenarios.sh <serial> <relay url> <app-debug.apk> <tests>
#   tests: ta08 ta12reboot ta15 ta20 ta21 (space separated, in this order is fine)
# Environment:
#   RELAY_STOP / RELAY_START  host commands that stop and start the relay
#     (default: D:\nginx\stop.bat and start.bat; every emulator and the
#     Windows client lose the relay meanwhile, run T-A08 alone).
#   PEER_IP                   T-A21 sends UDP to this address inside vpnCidr
#     (default 10.99.99.2: the Windows client of peer-traffic-tests.sh; any
#     address of the prefix works, packets without a peer are dropped by the relay).
#   CONFIG_FILE               a whole configuration document written with
#     CONFIGURE_FILE instead of CONFIGURE (peer tests); default: CONFIGURE <relay url>.
#   TA20_IDLE_S               seconds in forced Doze (default 600).
#   TA21_SECONDS              seconds of UDP load (default 180).
#   PEER_PYTHON               python on the host that runs the UDP sink of T-A21 on
#     PEER_IP (tcp_echo.py udp-sink); without it the peer may answer ICMP port
#     unreachable and toybox nc stops sending. For full queues give a CONFIG_FILE
#     with runtime.maxQueuedPacketAgeMs raised (make_peer_config.py ... maxQueuedPacketAgeMs=600000),
#     otherwise packets older than 5 s are dropped long before 128 MiB are queued.
set -u
export MSYS_NO_PATHCONV=1

SERIAL=$1
URL=$2
APK=$3
TESTS=${4:-"ta08"}
PKG=io.github.jaromirsivic.phpvpn
RECEIVER=$PKG/phpvpn.android.debug.DebugVpnCommandReceiver
# The commands go through eval: backslashes of a Windows path are doubled.
RELAY_STOP=${RELAY_STOP:-'cmd.exe /c D:\\nginx\\stop.bat'}
RELAY_START=${RELAY_START:-'cmd.exe /c D:\\nginx\\start.bat'}
PEER_IP=${PEER_IP:-10.99.99.2}
CONFIG_FILE=${CONFIG_FILE:-}
TA20_IDLE_S=${TA20_IDLE_S:-600}
TA21_SECONDS=${TA21_SECONDS:-180}
WORK=${WP11_WORK:-work/wp11}
FAILURES=0
mkdir -p "$WORK"

adb_() { adb -s "$SERIAL" "$@"; }
sh_() { adb_ shell "$@" | tr -d '\r'; }
log() { echo "[$(date +%H:%M:%S)] $*"; }
check() { if [ "$2" = 0 ]; then echo "PASS $1 ${3:-}"; else echo "FAIL $1 ${3:-}"; FAILURES=$((FAILURES + 1)); fi; }
cmd_() { sh_ am broadcast -n "$RECEIVER" -a "phpvpn.android.debug.$1" "${@:2}" >/dev/null; }
status_() { cmd_ STATUS; sleep 1; sh_ logcat -d -v brief -s phpvpn-debug:I | grep " status " | tail -1 | sed 's/.*status //'; }
status_json() { cmd_ STATUS_JSON; sleep 1; sh_ logcat -d -v brief -s phpvpn-debug:I | grep " status-json " | tail -1 | sed 's/.*status-json //'; }
json_field() { echo "$1" | grep -o "\"$2\": *[^,}]*" | head -1 | sed 's/.*: *//; s/"//g'; }
wait_status() {
  local deadline=$((SECONDS + $2)) s=""
  while [ $SECONDS -lt $deadline ]; do
    s=$(status_)
    if echo "$s" | grep -Eq "$1"; then echo "$s"; return 0; fi
  done
  echo "$s"; return 1
}
relay_stop() { eval "$RELAY_STOP" < /dev/null > "$WORK/relay-stop.log" 2>&1; }
relay_start() { eval "$RELAY_START" < /dev/null > "$WORK/relay-start.log" 2>&1; }
relay_up() { curl -s -m 3 -o /dev/null -w '%{http_code}' "$(echo "$URL" | sed 's#//10\.0\.2\.2#//127.0.0.1#')" | grep -q 200; }
launch() { sh_ input keyevent KEYCODE_WAKEUP; sh_ wm dismiss-keyguard; sh_ am start -W -n "$PKG/phpvpn.android.MainActivity" >/dev/null; sleep 2; }
ui_dump() { sh_ uiautomator dump /sdcard/wp11-ui.xml >/dev/null 2>&1; sh_ cat /sdcard/wp11-ui.xml; }
ui_has() { ui_dump | grep -q "$1"; }
pss_kb() { sh_ dumpsys meminfo "$PKG" | grep -E '^ *TOTAL PSS:|^ *TOTAL ' | head -1 | grep -o '[0-9]\+' | head -1; }
rss_kb() { sh_ dumpsys meminfo "$PKG" | grep -o 'TOTAL RSS: *[0-9]*' | grep -o '[0-9]*$'; }
screenshot() { adb_ exec-out screencap -p > "$WORK/$1.png"; }

configure() {
  if [ -n "$CONFIG_FILE" ]; then
    adb_ shell "run-as $PKG sh -c 'cat > files/wp11-scenario.json'" < "$CONFIG_FILE"
    cmd_ CONFIGURE_FILE --es file wp11-scenario.json
  else
    cmd_ CONFIGURE --es url "$URL" --ez autoConnect "${1:-false}"
  fi
  sleep 2
}

setup() {
  log "install $APK on $SERIAL (API $(sh_ getprop ro.build.version.sdk)), relay $URL"
  adb_ install -r -t -g "$APK" | tail -1
  sh_ am force-stop "$PKG"
  sh_ appops set "$PKG" ACTIVATE_VPN allow
  launch
  sh_ logcat -c
  cmd_ DISCONNECT
  wait_status "state=(disconnected|error) phase=idle" 30 >/dev/null
  configure false
}

connect() {
  local begun=$SECONDS s
  cmd_ CONNECT
  s=$(wait_status "state=connected phase=running tunnel=yes" "${1:-90}")
  local rc=$?
  echo "$s ($((SECONDS - begun)) s)"
  return $rc
}

disconnect() {
  cmd_ DISCONNECT
  wait_status "state=(disconnected|error) phase=idle" 30 >/dev/null
}

# Polls the status every ~2 s for <seconds> and prints "<t> <state> <phase> <tunnel> <error> <recoveryCount>" lines.
trace() {
  local until=$((SECONDS + $1)) begun=$SECONDS j
  while [ $SECONDS -lt $until ]; do
    j=$(status_json)
    echo "$((SECONDS - begun)) $(json_field "$j" state) $(json_field "$j" phase) $(json_field "$j" tunnelReady) $(json_field "$j" lastError) $(json_field "$j" recoveryCount)"
  done
}

# --- T-A08 -------------------------------------------------------------------
ta08_outage() { # ta08_outage <name> <seconds down>
  local name=$1 down=$2 before after file="$WORK/ta08-$1-$SERIAL.trace"
  before=$(json_field "$(status_json)" recoveryCount)
  log "T-A08 $name: relay down for $down s (recoveryCount before: $before)"
  relay_stop
  relay_up && echo "INFO T-A08 $name the relay still answers after RELAY_STOP"
  trace "$down" > "$file"
  launch
  local ui05=1
  ui_has 'text="Reconnecting"' && ui05=0
  screenshot "ta08-$name-down-$SERIAL"
  relay_start
  local restarted=$SECONDS
  local deadline=$((SECONDS + 30))
  until relay_up || [ $SECONDS -ge $deadline ]; do sleep 1; done
  trace 5 >> /dev/null
  local s; s=$(wait_status "state=connected phase=running tunnel=yes" 180)
  local rc=$?
  local back=$((SECONDS - restarted))
  after=$(json_field "$(status_json)" recoveryCount)
  local first_reconnecting first_recovering
  first_reconnecting=$(awk '$3 == "reconnecting" || $2 == "recovering" {print $1; exit}' "$file")
  first_recovering=$(awk '$2 == "recovering" {print $1; exit}' "$file")
  echo "INFO T-A08 $name trace (t state phase tunnel lastError recoveryCount): $(awk '{printf "%s|", $0}' "$file" | cut -c1-600)"
  check "T-A08 $name left connected/running while the relay was down" "$([ -n "$first_reconnecting" ] && echo 0 || echo 1)" "first reconnecting/recovering after ${first_reconnecting:-never} s, recovering after ${first_recovering:-never} s"
  check "T-A08 $name 05 Reconnecting shown in the UI" "$ui05"
  check "T-A08 $name back to connected after the relay returned" "$rc" "after $back s: $s"
  launch
  local ui01=1
  ui_has 'text="You are connected"' && ui01=0
  check "T-A08 $name 01 shown again" "$ui01"
  echo "INFO T-A08 $name recoveryCount $before -> $after"
  T08_FIRST_RECOVERING=$first_recovering
  T08_RECOVERIES=$((after - before))
}

ta08() {
  log "T-A08: soft recovery and recovery"
  local s; s=$(connect 90)
  check "T-A08 connected before" $? "$s"
  ta08_outage short 20
  check "T-A08 short outage: soft recovery only (no recoveryCount increase)" "$([ "$T08_RECOVERIES" = 0 ] && echo 0 || echo 1)" "recoveries $T08_RECOVERIES, recovering at ${T08_FIRST_RECOVERING:-never}"
  ta08_outage long 120
  check "T-A08 long outage: full recovery (state recovering, recoveryCount increased)" "$([ -n "$T08_FIRST_RECOVERING" ] && [ "$T08_RECOVERIES" -ge 1 ] && echo 0 || echo 1)" "recoveries $T08_RECOVERIES"
  disconnect
}

# --- T-A12 with a real reboot ---------------------------------------------------
ta12reboot() {
  log "T-A12: autoConnectOnStartup true, real reboot of the emulator"
  configure true
  local s; s=$(connect 90)
  check "T-A12 reboot: connected once (consent granted)" $? "$s"
  disconnect
  local begun=$SECONDS
  adb_ reboot
  adb_ wait-for-device
  until [ "$(sh_ getprop sys.boot_completed)" = 1 ]; do sleep 2; done
  log "booted after $((SECONDS - begun)) s"
  local logs="" deadline=$((SECONDS + 120))
  while ! echo "$logs" | grep -q "boot:" && [ $SECONDS -lt $deadline ]; do
    sleep 3
    logs=$(sh_ logcat -d -v brief -s phpvpn:I | grep "boot:")
  done
  check "T-A12 reboot: boot receiver connects" "$(echo "$logs" | grep -q 'autoConnectOnStartup is on, connecting' && echo 0 || echo 1)" "$(echo "$logs" | tail -1 | sed 's/.*: boot/boot/')"
  s=$(wait_status "state=connected phase=running tunnel=yes" 120)
  check "T-A12 reboot: connected after boot" $? "$s, $((SECONDS - begun)) s after adb reboot"
  check "T-A12 reboot: notification" "$(sh_ dumpsys notification --noredact | grep -q "pkg=$PKG.*id=9 " && echo 0 || echo 1)"
  disconnect
  configure false
}

# --- T-A15 Always-on with lockdown ------------------------------------------------
tap_node() {
  local bounds
  bounds=$(ui_dump | tr '>' '\n' | grep -E "$1" | head -1 | grep -o 'bounds="[^"]*"' | grep -o '[0-9]*' | tr '\n' ' ')
  [ -z "$bounds" ] && return 1
  set -- $bounds
  sh_ input tap $((($1 + $3) / 2)) $((($2 + $4) / 2))
  sleep 3
}

ta15_warning() { # ta15_warning <check name> <screenshot name>
  sh_ am start -W -f 0x10008000 -n "$PKG/phpvpn.android.MainActivity" >/dev/null
  sleep 2
  tap_node 'text="Settings"' || true
  local found=1
  for _ in 1 2 3 4; do
    ui_has 'Block connections without VPN is on' && { found=0; break; }
    sh_ input swipe 500 1500 500 600 300
    sleep 1
  done
  screenshot "$2"
  check "$1" "$found"
}

ta15() {
  log "T-A15: Always-on with Block connections without VPN (lockdown)"
  disconnect
  sh_ logcat -c
  sh_ am force-stop com.android.settings
  sh_ am start -W -a android.settings.VPN_SETTINGS >/dev/null
  sleep 3
  if ! tap_node 'resource-id="com.android.settings:id/settings_button"' || ! tap_node 'text="Always-on VPN"'; then
    echo "NOT RUN T-A15: the VPN settings page could not be driven"
    return
  fi
  if ! tap_node 'text="Block connections without VPN"'; then
    echo "NOT RUN T-A15: no lockdown switch"
    return
  fi
  # The confirmation dialog of the lockdown switch, when the system shows one.
  tap_node 'text="(Turn on|TURN ON|OK|Ok)"' || true
  screenshot "ta15-settings-$SERIAL"
  echo "INFO T-A15 settings: always_on_vpn_app=$(sh_ settings get secure always_on_vpn_app) lockdown=$(sh_ settings get secure always_on_vpn_lockdown)"
  check "T-A15 Always-on with lockdown set by the system" "$([ "$(sh_ settings get secure always_on_vpn_app)" = "$PKG" ] && [ "$(sh_ settings get secure always_on_vpn_lockdown)" = 1 ] && echo 0 || echo 1)"
  local trace_file="$WORK/ta15-$SERIAL.trace"
  trace 60 > "$trace_file"
  echo "INFO T-A15 status trace 60 s: $(awk '{printf "%s|", $0}' "$trace_file" | cut -c1-500)"
  local j; j=$(status_json)
  echo "INFO T-A15 final state=$(json_field "$j" state) phase=$(json_field "$j" phase) tunnel=$(json_field "$j" tunnelReady) lastError=$(json_field "$j" lastError) serverUrlCheck=$(json_field "$j" serverUrlCheck)"
  echo "INFO T-A15 phpvpn log: $(sh_ logcat -d -v brief -s phpvpn:I phpvpn:W phpvpn:E | grep -iE 'always|lockdown|error|fail' | tail -6 | sed 's/^.\/phpvpn[^:]*: //' | tr '\n' '|')"
  # Other apps (uid 2000) in lockdown: the internet and the relay host.
  echo "INFO T-A15 shell uid: $(sh_ "if echo | nc -w 5 www.google.com 443 >/dev/null 2>&1; then echo internet=ok; else echo internet=blocked; fi; if echo | nc -w 3 10.0.2.2 80 >/dev/null 2>&1; then echo relay_host=ok; else echo relay_host=blocked; fi" | tr '\n' ' ')"
  # Settings (10) shows the Always-on state and the lockdown warning.
  ta15_warning "T-A15 the app warns about lockdown (Settings 10), lockdown switched on while connected" "ta15-app-settings-$SERIAL"
  # Since the B1 fix the service samples isLockdownEnabled on every status poll;
  # a Disconnect and Connect (a new start command) must keep the warning.
  disconnect
  connect 90 >/dev/null
  echo "INFO T-A15 lockdown log after the reconnect: $(sh_ logcat -d -v brief -s phpvpn:W | grep -c 'lockdown is enabled') line(s)"
  ta15_warning "T-A15 the app warns about lockdown (Settings 10) after a reconnect" "ta15-app-settings-reconnect-$SERIAL"
  # Switch Always-on and lockdown off again.
  sh_ am force-stop com.android.settings
  sh_ am start -W -a android.settings.VPN_SETTINGS >/dev/null
  sleep 3
  tap_node 'resource-id="com.android.settings:id/settings_button"' && tap_node 'text="Always-on VPN"'
  sh_ input keyevent KEYCODE_HOME
  check "T-A15 Always-on switched off again" "$([ "$(sh_ settings get secure always_on_vpn_app)" = "null" ] && echo 0 || echo 1)" "lockdown=$(sh_ settings get secure always_on_vpn_lockdown)"
  disconnect
}

# --- T-A20 on an emulator ------------------------------------------------------------
ta20() {
  log "T-A20: forced Doze (deviceidle force-idle) with the connection up for $TA20_IDLE_S s"
  local s; s=$(connect 90)
  check "T-A20 connected before" $? "$s"
  sh_ logcat -c
  local before; before=$(status_json)
  sh_ dumpsys battery unplug
  sh_ input keyevent KEYCODE_SLEEP
  sleep 2
  sh_ dumpsys deviceidle force-idle >/dev/null
  echo "INFO T-A20 deviceidle: $(sh_ dumpsys deviceidle get deep) / light $(sh_ dumpsys deviceidle get light)"
  local file="$WORK/ta20-$SERIAL.trace" t0=$SECONDS
  : > "$file"
  while [ $((SECONDS - t0)) -lt "$TA20_IDLE_S" ]; do
    local j p
    j=$(status_json)
    p=$(sh_ ping -c 1 -W 5 "$PEER_IP" | grep -o 'time=[0-9.]* ms' | head -1)
    echo "$((SECONDS - t0)) $(json_field "$j" state) $(json_field "$j" phase) $(json_field "$j" recoveryCount) polls=$(json_field "$j" dataPollsTotal) ${p:-ping=lost}" >> "$file"
    sleep 20
  done
  local during; during=$(status_json)
  echo "INFO T-A20 trace: $(awk '{printf "%s|", $0}' "$file" | cut -c1-900)"
  local polls_before polls_during
  polls_before=$(json_field "$before" dataPollsTotal); polls_during=$(json_field "$during" dataPollsTotal)
  echo "INFO T-A20 data polls during $TA20_IDLE_S s idle: $((polls_during - polls_before)) ($(awk -v n=$((polls_during - polls_before)) -v s="$TA20_IDLE_S" 'BEGIN {printf "%.2f", n / s}') per s)"
  check "T-A20 engine still connected in Doze" "$(json_field "$during" state | grep -q connected && echo 0 || echo 1)" "state=$(json_field "$during" state) phase=$(json_field "$during" phase)"
  sh_ dumpsys deviceidle unforce >/dev/null
  sh_ input keyevent KEYCODE_WAKEUP
  sh_ dumpsys battery reset
  local woke=$SECONDS
  s=$(wait_status "state=connected phase=running tunnel=yes" 120)
  check "T-A20 connected after unforce" $? "after $((SECONDS - woke)) s: $s"
  local first_ping=""
  local deadline=$((SECONDS + 60))
  while [ -z "$first_ping" ] && [ $SECONDS -lt $deadline ]; do
    first_ping=$(sh_ ping -c 1 -W 3 "$PEER_IP" | grep -o 'time=[0-9.]* ms')
  done
  echo "INFO T-A20 first peer ping after unforce: ${first_ping:-none} ($((SECONDS - woke)) s)"
  local unauthorized recoveries
  unauthorized=$(sh_ logcat -d -v brief | grep -c -E 'HTTP[^0-9]*401|unauthori[sz]ed')
  recoveries=$(( $(json_field "$(status_json)" recoveryCount) - $(json_field "$before" recoveryCount) ))
  check "T-A20 no 401 in the log" "$([ "$unauthorized" = 0 ] && echo 0 || echo 1)" "$unauthorized lines"
  check "T-A20 at most one recovery" "$([ "$recoveries" -le 1 ] && echo 0 || echo 1)" "$recoveries"
  echo "INFO T-A20 batterystats (app uid): $(sh_ dumpsys batterystats --charged "$PKG" | grep -E 'Wake lock|Foreground service|Mobile network|Wifi network' | head -6 | tr -s ' ' | tr '\n' '|')"
  disconnect
}

# --- T-A21 memory under full queues -------------------------------------------------
# Kernel -> tun direction (what the app reads): TX bytes and TX drops of the tun device.
tun_tx() { sh_ cat /proc/net/dev | awk '$1 ~ /^tun[0-9]+:$/ {print $10; exit}'; }
tun_drop() { sh_ cat /proc/net/dev | awk '$1 ~ /^tun[0-9]+:$/ {print $13; exit}'; }

ta21_sample() { # one trace line: t phase pss rss MiB written into tun state sent req MiB
  local p r j tx
  p=$(pss_kb); r=$(rss_kb); j=$(status_json); tx=$(tun_tx)
  [ -n "$p" ] && [ "$p" -gt "$T21_MAX_PSS" ] && T21_MAX_PSS=$p
  [ -n "$r" ] && [ "$r" -gt "$T21_MAX_RSS" ] && T21_MAX_RSS=$r
  echo "$((SECONDS - T21_T0)) $1 pss=$p rss=$r tun_mib=$(( (${tx:-0} - ${T21_TX0:-0}) / 1048576 )) tun_drop=$(tun_drop) state=$(json_field "$j" state) sent=$(json_field "$j" ipv4PacketsSent) req_mib=$(( $(json_field "$j" serializedDataRequestBodyBytes) / 1048576 ))" >> "$T21_FILE"
}

ta21() {
  log "T-A21: UDP flood into the tunnel for $TA21_SECONDS s, memory of the app process"
  local s; s=$(connect 90)
  check "T-A21 connected before" $? "$s"
  local pid; pid=$(sh_ pidof "$PKG")
  local base_pss base_rss
  base_pss=$(pss_kb); base_rss=$(rss_kb)
  echo "INFO T-A21 baseline PSS ${base_pss} KiB RSS ${base_rss} KiB (pid $pid)"
  sh_ logcat -c
  sh_ logcat -b events -c
  # A UDP sink on the peer: no ICMP port unreachable that would stop the senders.
  local sink=""
  if [ -n "${PEER_PYTHON:-}" ]; then
    "$PEER_PYTHON" -I "$(dirname "$0")/tcp_echo.py" udp-sink "$PEER_IP" 9901 4 $((TA21_SECONDS + 120)) > "$WORK/ta21-sink-$SERIAL.log" 2>&1 &
    sink=$!
    sleep 2
  fi
  # Four senders of 1400 byte datagrams from the shell (uid 2000, routed into the tunnel).
  local i tx0; tx0=$(tun_tx)
  for i in 1 2 3 4; do
    adb_ shell "dd if=/dev/zero bs=1400 2>/dev/null | nc -u $PEER_IP 990$i >/dev/null 2>&1" > /dev/null 2>&1 &
  done
  T21_FILE="$WORK/ta21-$SERIAL.trace" T21_T0=$SECONDS T21_TX0=$tx0 T21_MAX_PSS=0 T21_MAX_RSS=0
  : > "$T21_FILE"
  while [ $((SECONDS - T21_T0)) -lt "$TA21_SECONDS" ]; do ta21_sample load; sleep 10; done
  sh_ "pkill -x nc; pkill -x dd" >/dev/null 2>&1
  local drain_end=$((SECONDS + 60))
  while [ $SECONDS -lt $drain_end ]; do ta21_sample drain; sleep 10; done
  local after_pss after_rss; after_pss=$(pss_kb); after_rss=$(rss_kb)
  echo "INFO T-A21 trace (t phase pss rss tun_mib state sent req_mib): $(awk '{printf "%s|", $0}' "$T21_FILE" | cut -c1-2400)"
  echo "INFO T-A21 max PSS ${T21_MAX_PSS} KiB (+$((T21_MAX_PSS - base_pss)) KiB), max RSS ${T21_MAX_RSS} KiB (+$((T21_MAX_RSS - base_rss)) KiB); after a 60 s drain PSS ${after_pss} KiB RSS ${after_rss} KiB"
  echo "INFO T-A21 device memory: $(sh_ cat /proc/meminfo | grep -E 'MemTotal|MemAvailable' | tr -s ' ' | tr '\n' ' ')"
  if [ -n "$sink" ]; then kill "$sink" 2>/dev/null; wait "$sink" 2>/dev/null; echo "INFO T-A21 peer: $(tail -1 "$WORK/ta21-sink-$SERIAL.log")"; fi
  local killed; killed=$(sh_ logcat -b events -d | grep -E 'am_kill|am_low_memory|lmk' | grep -c "$PKG")
  check "T-A21 the app process survived (same pid, no LMK kill)" "$([ "$(sh_ pidof "$PKG")" = "$pid" ] && [ "$killed" = 0 ] && echo 0 || echo 1)" "pid $pid -> $(sh_ pidof "$PKG"), kill events $killed"
  s=$(wait_status "state=connected phase=running tunnel=yes" 60)
  check "T-A21 still connected after the load" $? "$s"
  echo "INFO T-A21 queue/drop log lines: $(sh_ logcat -d -v brief -s phpvpn:W phpvpn:E | grep -ciE 'queue|drop')"
  disconnect
}

setup
for test in $TESTS; do "$test"; done
echo "failures: $FAILURES"
exit $FAILURES
