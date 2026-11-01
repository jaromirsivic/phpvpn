#!/usr/bin/env bash
# T-A20 start: battery reported as unplugged (Doze needs it, USB stays connected), batterystats reset, screen off,
# then the Doze state every 5 min for $1 minutes.
export PATH="$PATH:/c/Users/User/AppData/Local/Android/Sdk/platform-tools"
export ANDROID_SERIAL=64090DLCR000R5 MSYS_NO_PATHCONV=1
echo "unplug $(date -u +%T.%3N)"; adb shell dumpsys battery unplug
echo "batterystats reset $(date -u +%T.%3N)"; adb shell dumpsys batterystats --reset
echo "KEYCODE_SLEEP $(date -u +%T.%3N)"; adb shell input keyevent KEYCODE_SLEEP
bash client/tests/android/device/doze_watch.sh "${1:-30}"
echo "watch end $(date -u +%T.%3N)"
