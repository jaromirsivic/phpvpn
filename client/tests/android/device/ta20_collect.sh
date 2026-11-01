#!/usr/bin/env bash
# T-A20 collection after wake ($1 = label/prefix). Keeps only the app's and Doze-related lines.
export PATH="$PATH:/c/Users/User/AppData/Local/Android/Sdk/platform-tools"
export ANDROID_SERIAL=64090DLCR000R5 MSYS_NO_PATHCONV=1
P=work/device/$1
bash client/tests/android/device/diag.sh > $P-diag.txt 2>&1
adb shell dumpsys deviceidle > $P-deviceidle-full.tmp
grep -E "^  m(State|LightState|Force|ScreenOn|Charging|MotionActive|NextAlarm|NextIdle|NextLightIdle|CurLightIdleBudget)|Idle|idle" $P-deviceidle-full.tmp | grep -v "^    [a-z]" | head -80 > $P-deviceidle.txt
rm -f $P-deviceidle-full.tmp
adb shell dumpsys batterystats --checkin > /dev/null 2>&1
adb shell dumpsys batterystats com.pilandrome.phpvpn > $P-batterystats-app.txt 2>&1
adb shell dumpsys batterystats > $P-batterystats-full.tmp 2>&1
{
  grep -E "Time on battery|Time on battery screen off|Screen on:|Device light idling|Device full idling|Idle mode|Light idle mode|Idle mode time|Total run time|Discharge|Wifi (on|running)|Wi-Fi (on|running)|Connectivity changes|Battery level|Start clock|Estimated power use|Capacity:|Computed drain|Uid u0a151" $P-batterystats-full.tmp | head -60
  echo "---- uid u0a151 block"
  awk '/^  u0a151:/{f=1} f&&/^  (u0a|[0-9])/&&!/^  u0a151:/{f=0} f' $P-batterystats-full.tmp | head -80
} > $P-batterystats.txt
rm -f $P-batterystats-full.tmp
grep -iE "recover|soft|401|unauthor|time sync|timestamp|clock|tunnel|data exchange|establish|error|FATAL" work/device/logcat-app.txt | grep -E " phpvpn" > $P-logcat-recovery.txt
echo "collected $(date -u +%T)"
