#!/usr/bin/env bash
# T-A20 forced Doze: screen off, `deviceidle force-idle` for $1 s with the host pinging, then unforce and wake.
export PATH="$PATH:/c/Users/User/AppData/Local/Android/Sdk/platform-tools"
export ANDROID_SERIAL=64090DLCR000R5 MSYS_NO_PATHCONV=1
D=${1:-300}
python -I client/tests/android/device/pinger.py work/device/force-ping.txt $((D + 150)) 10 10.99.99.5 &
sleep 60
bash client/tests/android/device/diag.sh
echo "KEYCODE_SLEEP $(date -u +%T.%3N)"; adb shell input keyevent KEYCODE_SLEEP
echo "force-idle $(date -u +%T.%3N): $(adb shell dumpsys deviceidle force-idle)"
echo "deep=$(adb shell dumpsys deviceidle get deep) light=$(adb shell dumpsys deviceidle get light)"
sleep $D
echo "deep=$(adb shell dumpsys deviceidle get deep) light=$(adb shell dumpsys deviceidle get light) $(date -u +%T)"
echo "unforce $(date -u +%T.%3N): $(adb shell dumpsys deviceidle unforce)"
echo "KEYCODE_WAKEUP $(date -u +%T.%3N)"; adb shell input keyevent KEYCODE_WAKEUP
bash client/tests/android/device/diag.sh
wait
echo "end $(date -u +%T)"
