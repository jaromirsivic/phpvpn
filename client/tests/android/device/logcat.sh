#!/usr/bin/env bash
# Device tests (device-report-2026-11-01.md): background logcat capture into work/device (gitignored).
# Run from the repository root: the app's uid (all buffers) and system lines that mention the app or VPN.
# Usage: client/tests/android/device/logcat.sh <app uid, e.g. 10151>
export PATH="$PATH:${ANDROID_HOME:-$LOCALAPPDATA/Android/Sdk}/platform-tools"
export ANDROID_SERIAL=${ANDROID_SERIAL:-64090DLCR000R5}
mkdir -p work/device
adb logcat -v threadtime,UTC,year -b main,system,crash,events -T 1 --uid="$1" >> work/device/logcat-app.txt 2>&1 &
adb logcat -v threadtime,UTC,year -b main,system,crash -T 1 2>&1 \
  | grep --line-buffered -i "phpvpn\|$1\|VpnService\|Vpn \|Vpn:\|DeviceIdle\|tun0\|ANR in\|FATAL" >> work/device/logcat-system.txt &
wait
