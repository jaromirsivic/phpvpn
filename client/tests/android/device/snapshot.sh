#!/usr/bin/env bash
# Phone settings and Doze snapshot (label in $1).
. "$(dirname "$0")/env.sh"
echo "== $1 $(date -u +%T)"
echo "stay_on_while_plugged_in=$(adb shell settings get global stay_on_while_plugged_in)"
echo "screen_off_timeout=$(adb shell settings get system screen_off_timeout)"
echo "wifi_on=$(adb shell settings get global wifi_on)"
echo "deep=$(adb shell dumpsys deviceidle get deep) light=$(adb shell dumpsys deviceidle get light)"
adb shell dumpsys battery | grep -E "powered|status|level"
adb shell dumpsys deviceidle | grep -E "^  (mLightEnabled|mDeepEnabled|mForceIdle|mScreenOn|mCharging|mMotionActive|mState|mLightState)"
adb shell dumpsys power | grep -m1 -o "mWakefulness=[A-Za-z]*"
echo "appops ACTIVATE_VPN: $(adb shell appops get com.pilandrome.phpvpn ACTIVATE_VPN)"
