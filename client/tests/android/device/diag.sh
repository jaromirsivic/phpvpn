#!/usr/bin/env bash
# Prints the phone's diagnostics line (debug receiver) and the Windows status counters, one line each.
export PATH="$PATH:/c/Users/User/AppData/Local/Android/Sdk/platform-tools"
export ANDROID_SERIAL=64090DLCR000R5 MSYS_NO_PATHCONV=1
adb shell am broadcast -n io.github.jaromirsivic.phpvpn/phpvpn.android.debug.DebugVpnCommandReceiver -a phpvpn.android.debug.DIAGNOSTICS >/dev/null
sleep 1.5
line=$(grep 'phpvpn-debug: diagnostics' work/device/logcat-app.txt | tail -1)
echo "host $(date -u +%T) phone ${line:11:12} ${line#*diagnostics }"
"/c/Program Files/phpvpn/1.0.2/phpvpn.exe" status | python -I -c "
import json,sys; s=json.load(sys.stdin)['status']
print('windows', ' '.join(f'{k}={s[k]}' for k in ('state','phase','tunnelReady','dataPollsTotal','dataPollsEmpty','ipv4PacketsSent','ipv4PacketsReceived','recoveryCount')))"
