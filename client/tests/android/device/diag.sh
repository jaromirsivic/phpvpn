#!/usr/bin/env bash
# Prints the phone's diagnostics line (debug receiver) and the Windows status counters, one line each.
. "$(dirname "$0")/env.sh"
adb shell am broadcast -n com.pilandrome.phpvpn/phpvpn.android.debug.DebugVpnCommandReceiver -a phpvpn.android.debug.DIAGNOSTICS >/dev/null
sleep 1.5
line=$(grep 'phpvpn-debug: diagnostics' work/device/logcat-app.txt | tail -1)
echo "host $(date -u +%T) phone ${line:11:12} ${line#*diagnostics }"
"$(python -I "$(dirname "$0")/../../../../tools/phpvpn_config.py" tool phpvpnClient | tr '\\' '/' || echo phpvpn)" status | python -I -c "
import json,sys; s=json.load(sys.stdin)['status']
print('windows', ' '.join(f'{k}={s[k]}' for k in ('state','phase','tunnelReady','dataPollsTotal','dataPollsEmpty','ipv4PacketsSent','ipv4PacketsReceived','recoveryCount')))"
