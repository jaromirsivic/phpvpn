#!/usr/bin/env bash
# Host-driven WP05 service tests on one emulator (plan-android-v001 T-A10,
# T-A12, T-A16, and the Always-on entry). They kill or restart the app
# process, which an instrumented test inside that process cannot survive.
# Commands go through the debug-only DebugVpnCommandReceiver (Connect and
# Disconnect take the production ConnectActions path); facts come from
# logcat (tags phpvpn, phpvpn-debug), `run-as` (intent.json, journal),
# `ip`, dumpsys and the UI hierarchy. Every check prints PASS or FAIL.
#
# Usage (Git Bash on the Windows host, emulator with a debug build):
#   client/tests/android/service-lifecycle-tests.sh <serial> <relay url> <app-debug.apk> [block file] [tests]
#   e.g. emulator-5554 http://10.0.2.2:18296/phpvpn.php client/android/app/build/outputs/apk/debug/app-debug.apk \
#        work/wp05-relay/block "ta10 ta16 ta12 alwayson"
# The optional block file makes the relay proxy of the WP04 procedure close
# every connection while it exists (T-A16 restart limit without a connection).
set -u
export MSYS_NO_PATHCONV=1

SERIAL=$1
URL=$2
APK=$3
BLOCK=${4:-}
TESTS=${5:-"ta10 ta16 ta12 alwayson"}
PKG=io.github.jaromirsivic.phpvpn
RECEIVER=$PKG/phpvpn.android.debug.DebugVpnCommandReceiver
SERVICE=phpvpn.android.service.PhpvpnVpnService
FAILURES=0

adb_() { adb -s "$SERIAL" "$@"; }
sh_() { adb_ shell "$@" | tr -d '\r'; }
log() { echo "[$(date +%H:%M:%S)] $*"; }
check() { # check <name> <condition result 0/1> <detail>
  if [ "$2" = 0 ]; then echo "PASS $1 ${3:-}"; else echo "FAIL $1 ${3:-}"; FAILURES=$((FAILURES + 1)); fi
}
pid_() { sh_ pidof "$PKG"; }
cmd_() { sh_ am broadcast -n "$RECEIVER" -a "phpvpn.android.debug.$1" "${@:2}" >/dev/null; }
# The last "status ..." line the debug receiver logged after a fresh STATUS.
status_() {
  cmd_ STATUS
  sleep 1
  sh_ logcat -d -v brief -s phpvpn-debug:I | grep " status " | tail -1 | sed 's/.*status //'
}
wait_status() { # wait_status <regex> <seconds>
  local deadline=$((SECONDS + $2)) s=""
  while [ $SECONDS -lt $deadline ]; do
    s=$(status_)
    if echo "$s" | grep -Eq "$1"; then echo "$s"; return 0; fi
  done
  echo "$s"; return 1
}
wait_pid_change() { # wait_pid_change <old pid> <seconds>: prints the new pid
  local deadline=$((SECONDS + $2)) p=""
  while [ $SECONDS -lt $deadline ]; do
    p=$(pid_)
    if [ -n "$p" ] && [ "$p" != "$1" ]; then echo "$p"; return 0; fi
    sleep 0.5
  done
  echo "$p"; return 1
}
intent_json() { sh_ run-as "$PKG" cat no_backup/phpvpn/state/intent.json 2>/dev/null; }
journal_present() { sh_ run-as "$PKG" ls no_backup/phpvpn/state/ 2>/dev/null | grep -q ownership.json; }
tun_address() { sh_ ip -4 addr show 2>/dev/null | grep -o "inet 10\.99\.99\.[0-9]*/24" | head -1; }
# Only a record with a live process counts: after a SIGKILL with the VPN up,
# Android 14/15 keep a record with app=null that is never restarted.
service_running() {
  sh_ dumpsys activity services "$PKG" |
    awk -v svc="$SERVICE" '/ServiceRecord\{/ { inside = index($0, svc) > 0 } inside && /^ *app=ProcessRecord/ { found = 1 } END { exit !found }'
}
notification_present() { sh_ dumpsys notification --noredact | grep -q "pkg=$PKG.*id=9 "; }
ui_has() { sh_ uiautomator dump /sdcard/wp05-ui.xml >/dev/null 2>&1; sh_ cat /sdcard/wp05-ui.xml | grep -q "$1"; }
logs_of() { sh_ logcat -d -v brief --pid="$1" -s phpvpn:I; }
# A debuggable build starts slowly on a loaded emulator (class verification,
# no AOT): poll for the outcome instead of fixed sleeps.
wait_log() { # wait_log <pid> <regex> <seconds>: prints the process log
  local deadline=$((SECONDS + $3)) out=""
  while [ $SECONDS -lt $deadline ]; do
    out=$(logs_of "$1")
    if echo "$out" | grep -Eq "$2"; then break; fi
    sleep 2
  done
  echo "$out"
}
wait_ui() { # wait_ui <text> <seconds>; on failure prints the visible texts
  local deadline=$((SECONDS + $2))
  while [ $SECONDS -lt $deadline ]; do
    ui_has "$1" && return 0
    sleep 2
  done
  echo "  visible: $(sh_ cat /sdcard/wp05-ui.xml | tr '>' '\n' | grep -o 'text="[^"]\+"' | head -12 | tr '\n' ' ')"
  return 1
}
wait_intent() { # wait_intent <regex> <seconds>
  local deadline=$((SECONDS + $2))
  while [ $SECONDS -lt $deadline ]; do
    intent_json | grep -Eq "$1" && return 0
    sleep 2
  done
  return 1
}
launch() { sh_ input keyevent KEYCODE_WAKEUP; sh_ wm dismiss-keyguard; sh_ am start -W -n "$PKG/phpvpn.android.MainActivity" >/dev/null; sleep 2; }
home() { sh_ input keyevent KEYCODE_HOME; sleep 1; }
connected() { wait_status "state=connected phase=running tunnel=yes" "${1:-60}"; }
disconnect_all() {
  cmd_ DISCONNECT
  wait_status "state=(disconnected|error) phase=idle" 30 >/dev/null
  local deadline=$((SECONDS + 10))
  while service_running && [ $SECONDS -lt $deadline ]; do sleep 0.5; done
}
kill_hard() { # SIGKILL like the low memory killer; run-as has the app uid
  sh_ run-as "$PKG" kill -9 "$1"
}

setup() {
  log "install $APK on $SERIAL (API $(sh_ getprop ro.build.version.sdk))"
  adb_ install -r -t "$APK" | tail -1
  # A clean start: no service record left from an earlier run.
  sh_ am force-stop "$PKG"
  sh_ pm grant "$PKG" android.permission.POST_NOTIFICATIONS
  # The consent dialog itself is covered by VpnServiceTest (T-A02, T-A11).
  sh_ appops set "$PKG" ACTIVATE_VPN allow
  [ -n "$BLOCK" ] && rm -f "$BLOCK"
  launch
  sh_ logcat -c
  cmd_ CONFIGURE --es url "$URL" --ez autoConnect false
  sleep 1
  disconnect_all
}

ta10() {
  log "T-A10: am kill while disconnected"
  launch
  cmd_ CONNECT
  connected 60 >/dev/null
  check "T-A10 connected before" $? "$(tun_address)"
  disconnect_all
  check "T-A10 journal removed after Disconnect" "$(journal_present && echo 1 || echo 0)"
  home
  local before try; before=$(pid_)
  # am kill ends only a process that is safe to kill (cached); right after
  # HOME it may still count as recently visible, so it is repeated.
  for try in 1 2 3 4 5; do
    sh_ am kill "$PKG"
    sleep 3
    [ -z "$(pid_)" ] && break
  done
  check "T-A10 am kill ended the cached process" "$([ -z "$(pid_)" ] && echo 0 || echo 1)" "pid before=$before after=$(pid_), attempts $try"
  sleep 5
  check "T-A10 no sticky restart without a service" "$(service_running && echo 1 || echo 0)"
  check "T-A10 intent.json wantConnected false" "$(intent_json | grep -q '"wantConnected":false' && echo 0 || echo 1)" "$(intent_json)"
  check "T-A10 no journal" "$(journal_present && echo 1 || echo 0)"
  launch
  local s; s=$(wait_status "state=disconnected phase=idle" 15)
  check "T-A10 state after return" $? "$s"
  check "T-A10 no tunnel" "$([ -z "$(tun_address)" ] && echo 0 || echo 1)"
  check "T-A10 no restart text on 02" "$(ui_has 'The connection ended' && echo 1 || echo 0)"
}

ta16() {
  log "T-A16 a: SIGKILL while connected (tunnel up), flag and consent"
  launch
  cmd_ CONNECT
  connected 60 >/dev/null
  home
  local old; old=$(pid_)
  sh_ am kill "$PKG"
  sleep 2
  check "T-A16 am kill leaves a foreground service process alone" "$([ "$(pid_)" = "$old" ] && echo 0 || echo 1)" "pid $old -> $(pid_)"
  kill_hard "$old"
  local started=$SECONDS new
  new=$(wait_pid_change "$old" 60)
  # Android does not always restart the sticky service when the process dies
  # with the VPN interface up: the system unbinds the dying service ("Exception
  # when unbinding service") and sometimes no restart is scheduled. Both
  # outcomes are checked: R9 reconnects, or the fallback explains it on 02.
  if [ -n "$new" ]; then
    echo "INFO T-A16 a sticky restart pid $old -> $new after $((SECONDS - started)) s"
    local s; s=$(connected 90)
    check "T-A16 a reconnected by R9" $? "$s"
    local logs; logs=$(logs_of "$new")
    check "T-A16 a R9 decision logged" "$(echo "$logs" | grep -q 'reconnecting (restart 1)' && echo 0 || echo 1)"
    check "T-A16 a stale journal retired at start" "$(echo "$logs" | grep -q 'retiring the journal' && echo 0 || echo 1)"
    check "T-A16 a exactly one establish" "$([ "$(echo "$logs" | grep -c 'the VPN interface was established')" = 1 ] && echo 0 || echo 1)"
    check "T-A16 a notification back" "$(notification_present && echo 0 || echo 1)"
    disconnect_all
  else
    echo "INFO T-A16 a no sticky restart within 60 s (pid $old)"
    launch
    wait_intent '"wantConnected":false.*"notice":"process_restarted"' 60
    check "T-A16 a fallback: flag cleared, notice stored" $? "$(intent_json)"
    check "T-A16 a no tunnel after the kill" "$([ -z "$(tun_address)" ] && echo 0 || echo 1)"
    wait_ui 'The connection ended' 60
    check "T-A16 a 02 explains the end" $?
    check "T-A16 a stale journal retired at start" "$(sh_ logcat -d -v brief --pid="$(pid_)" -s phpvpn:W | grep -q 'retiring the journal' && echo 0 || echo 1)"
    check "T-A16 a no journal left" "$(journal_present && echo 1 || echo 0)"
  fi

  if [ -z "$BLOCK" ]; then
    echo "NOT RUN T-A16 b, c, d (no relay block file given)"
    return
  fi
  # b to d run without a tunnel (relay blocked: connecting or recovering),
  # where Android does restart the sticky service.
  log "T-A16 b: kill while recovering without consent -> no reconnect, 02 text"
  start_recovering
  sh_ appops set "$PKG" ACTIVATE_VPN ignore
  local logs; logs=$(restart_logs)
  check "T-A16 b not reconnecting (no_consent)" "$(echo "$logs" | grep -q 'not reconnecting (no_consent)' && echo 0 || echo 1)"
  check "T-A16 b service stopped" "$(service_running && echo 1 || echo 0)"
  check "T-A16 b flag cleared, notice stored" "$(intent_json | grep -q '"wantConnected":false.*"notice":"process_restarted"' && echo 0 || echo 1)" "$(intent_json)"
  rm -f "$BLOCK"
  launch
  wait_ui 'The connection ended' 60
  check "T-A16 b 02 explains the restart" $?
  sh_ appops set "$PKG" ACTIVATE_VPN allow

  log "T-A16 c: kill while recovering without the flag -> nothing, no text"
  start_recovering
  printf '%s' '{"version":1,"wantConnected":false,"restartsElapsedMs":[],"notice":null}' |
    adb_ shell "run-as $PKG sh -c 'cat > no_backup/phpvpn/state/intent.json'"
  logs=$(restart_logs)
  check "T-A16 c not reconnecting (not_wanted)" "$(echo "$logs" | grep -q 'not reconnecting (not_wanted)' && echo 0 || echo 1)"
  check "T-A16 c no notice" "$(intent_json | grep -q '"notice":null' && echo 0 || echo 1)" "$(intent_json)"
  check "T-A16 c no service" "$(service_running && echo 1 || echo 0)"

  log "T-A16 d: three restarts without connected in 10 min, the fourth gives up"
  start_recovering
  local round
  for round in 1 2 3 4; do
    logs=$(restart_logs)
    if [ $round -le 3 ]; then
      check "T-A16 d restart $round reconnects" "$(echo "$logs" | grep -q "reconnecting (restart $round)" && echo 0 || echo 1)"
    else
      check "T-A16 d restart 4 gives up (restart_limit)" "$(echo "$logs" | grep -q 'not reconnecting (restart_limit)' && echo 0 || echo 1)"
      check "T-A16 d flag cleared, notice stored" "$(intent_json | grep -q '"wantConnected":false.*"notice":"process_restarted"' && echo 0 || echo 1)" "$(intent_json)"
      check "T-A16 d service stopped" "$(service_running && echo 1 || echo 0)"
    fi
  done
  rm -f "$BLOCK"
  launch
  disconnect_all
}

start_recovering() { # Connect with the relay blocked: connecting or recovering, no tunnel
  launch
  disconnect_all
  touch "$BLOCK"
  cmd_ CONNECT
  wait_status "state=(connecting|recovering)" 30 >/dev/null
  home
}

restart_logs() { # SIGKILL, wait for the restarted process, print its phpvpn log
  local before after begun=$SECONDS
  before=$(pid_)
  kill_hard "$before"
  after=$(wait_pid_change "$before" 240)
  echo "restart pid $before -> ${after:-none} after $((SECONDS - begun)) s" >&2
  wait_log "$after" 'restarted after the process ended' 90
  sleep 3
}

boot() { # boot <expect regex> <name>
  home
  sh_ am kill "$PKG"
  sleep 1
  sh_ logcat -c
  sh_ am broadcast -a android.intent.action.BOOT_COMPLETED -p "$PKG" 2>&1 | tail -1
  local logs="" deadline=$((SECONDS + 60))
  while [ -z "$logs" ] && [ $SECONDS -lt $deadline ]; do
    sleep 2
    logs=$(sh_ logcat -d -v brief -s phpvpn:I | grep "boot:")
  done
  check "$2" "$(echo "$logs" | grep -Eq "$1" && echo 0 || echo 1)" "$(echo "$logs" | tail -1)"
}

ta12() {
  log "T-A12: BOOT_COMPLETED with autoConnectOnStartup true/false"
  # The shell user may not send BOOT_COMPLETED (Permission Denial); root may
  # (google_apis images). adbd restarts as root only on this emulator.
  adb_ root >/dev/null
  adb_ wait-for-device
  sleep 2
  if [ "$(sh_ getprop ro.build.version.sdk)" -ge 35 ]; then
    sh_ am compat enable FGS_BOOT_COMPLETED_RESTRICTIONS "$PKG"
  fi
  launch
  cmd_ CONFIGURE --es url "$URL" --ez autoConnect true
  sleep 1
  disconnect_all
  boot "autoConnectOnStartup is on, connecting" "T-A12 autoConnect true starts Connect"
  local s; s=$(connected 60)
  check "T-A12 autoConnect true connected" $? "$s"
  check "T-A12 foreground service with notification" "$(service_running && notification_present && echo 0 || echo 1)"
  disconnect_all

  sh_ appops set "$PKG" ACTIVATE_VPN ignore
  boot "consent is missing; no Connect" "T-A12 autoConnect true without consent only logs"
  check "T-A12 no service without consent" "$(service_running && echo 1 || echo 0)"
  sh_ appops set "$PKG" ACTIVATE_VPN allow

  launch
  cmd_ CONFIGURE --es url "$URL" --ez autoConnect false
  sleep 1
  boot "autoConnectOnStartup is off" "T-A12 autoConnect false does nothing"
  sleep 3
  check "T-A12 no service with autoConnect false" "$(service_running && echo 1 || echo 0)"
  s=$(status_)
  check "T-A12 still disconnected" "$(echo "$s" | grep -q 'state=disconnected' && echo 0 || echo 1)" "$s"
  adb_ unroot >/dev/null
  adb_ wait-for-device
  sleep 2
}

# Always-on through the system VPN settings (life cycle step 6), driven by the UI hierarchy.
tap_node() { # tap the center of the first node whose attributes match the regex
  adb_ shell uiautomator dump /sdcard/wp05-ui.xml >/dev/null 2>&1
  local bounds
  bounds=$(adb_ shell cat /sdcard/wp05-ui.xml | tr '>' '
' | grep -E "$1" | head -1 | grep -o 'bounds="[^"]*"' | grep -o '[0-9]*' | tr '
' ' ')
  [ -z "$bounds" ] && return 1
  set -- $bounds
  adb_ shell input tap $((($1 + $3) / 2)) $((($2 + $4) / 2))
  sleep 3
}

alwayson() {
  log "Always-on: enabled in the system VPN settings -> the system starts the service"
  # Settings lists only VPN apps with a consent.
  sh_ appops set "$PKG" ACTIVATE_VPN allow
  launch
  cmd_ CONFIGURE --es url "$URL" --ez autoConnect false
  sleep 1
  disconnect_all
  home
  sh_ logcat -c
  # A fresh Settings task: a resumed one may still show a sub-page.
  sh_ am force-stop com.android.settings
  sh_ am start -W -a android.settings.VPN_SETTINGS >/dev/null
  sleep 3
  if ! tap_node 'resource-id="com.android.settings:id/settings_button"' || ! tap_node 'text="Always-on VPN"'; then
    echo "NOT RUN Always-on: the settings page could not be driven (see the manual procedure)"
    home
    return
  fi
  check "Always-on set by the system" "$([ "$(sh_ settings get secure always_on_vpn_app)" = "$PKG" ] && echo 0 || echo 1)"
  local s; s=$(connected 60)
  check "Always-on start connects" $? "$s"
  check "Always-on start logged" "$(sh_ logcat -d -v brief -s phpvpn:I | grep -q 'Always-on start: connecting' && echo 0 || echo 1)"
  tap_node 'text="Always-on VPN"'
  check "Always-on switched off again" "$([ "$(sh_ settings get secure always_on_vpn_app)" = "null" ] && echo 0 || echo 1)"
  home
  disconnect_all
}

setup
for test in $TESTS; do "$test"; done
echo "failures: $FAILURES"
exit $FAILURES
