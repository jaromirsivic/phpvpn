#!/usr/bin/env bash
# Phone settings and Doze snapshot (label in $1).
export PATH="$PATH:/c/Users/User/AppData/Local/Android/Sdk/platform-tools"
export ANDROID_SERIAL=64090DLCR000R5 MSYS_NO_PATHCONV=1
echo "== $1 $(date -u +%T)"
echo "stay_on_while_plugged_in=$(adb shell settings get global stay_on_while_plugged_in)"
echo "screen_off_timeout=$(adb shell settings get system screen_off_timeout)"
echo "wifi_on=$(adb shell settings get global wifi_on)"
echo "deep=$(adb shell dumpsys deviceidle get deep) light=$(adb shell dumpsys deviceidle get light)"
adb shell dumpsys battery | grep -E "powered|status|level"
adb shell dumpsys deviceidle | grep -E "^  (mLightEnabled|mDeepEnabled|mForceIdle|mScreenOn|mCharging|mMotionActive|mState|mLightState)"
adb shell dumpsys power | grep -m1 -o "mWakefulness=[A-Za-z]*"
echo "appops ACTIVATE_VPN: $(adb shell appops get io.github.jaromirsivic.phpvpn ACTIVATE_VPN)"
