#!/usr/bin/env bash
# Wi-Fi off for $1 s with the VPN up, then on; logs the phone's state every 2 s (debug STATUS) to stdout.
. "$(dirname "$0")/env.sh"
OFF=${1:-60}
R=com.pilandrome.phpvpn/phpvpn.android.debug.DebugVpnCommandReceiver
st() {
  adb shell am broadcast -n $R -a phpvpn.android.debug.STATUS >/dev/null
  sleep 0.8
  l=$(grep 'phpvpn-debug: status' work/device/logcat-app.txt | tail -1)
  echo "$(date -u +%T) ${l#*status } | wifi=$(adb shell settings get global wifi_on) tun0=$(adb shell ip -4 addr show tun0 2>/dev/null | grep -c inet)"
}
echo "start $(date -u +%T)"; st
echo "wifi disable $(date -u +%T.%3N)"; adb shell svc wifi disable
end=$((SECONDS + OFF)); while [ $SECONDS -lt $end ]; do st; sleep 1; done
echo "wifi enable $(date -u +%T.%3N)"; adb shell svc wifi enable
end=$((SECONDS + 90)); while [ $SECONDS -lt $end ]; do st; sleep 1; done
