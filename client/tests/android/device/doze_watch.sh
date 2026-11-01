#!/usr/bin/env bash
# T-A20: records the Doze state once every 5 min for $1 minutes (no other phone access).
. "$(dirname "$0")/env.sh"
MIN=${1:-30}
for ((i = 0; i <= MIN; i += 5)); do
  echo "$(date -u +%T) deep=$(adb shell dumpsys deviceidle get deep) light=$(adb shell dumpsys deviceidle get light) screen=$(adb shell dumpsys power | grep -m1 -o 'mWakefulness=[A-Za-z]*')"
  [ $i -lt $MIN ] && sleep 300
done
