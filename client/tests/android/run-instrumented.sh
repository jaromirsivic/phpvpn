#!/usr/bin/env bash
# WP11: runs the instrumented tests of one module on one emulator without
# Gradle, so that the three emulators of the matrix run at the same time,
# each against its own relay (deploy-wp11-relays.ps1). It does what
# `./gradlew :<module>:connectedDebugAndroidTest` does: uninstall, install
# the app and the test APK (-t -g), `am instrument -w -r`, uninstall again.
# Build the APKs first:
#   ./gradlew :app:assembleDebug :app:assembleDebugAndroidTest :bridge:assembleDebugAndroidTest
#
# Usage (Git Bash on the Windows host, worktree root):
#   client/tests/android/run-instrumented.sh <serial> <app|bridge> <relay url> <log dir> [class or class#method]
#   e.g. client/tests/android/run-instrumented.sh emulator-5556 app http://10.0.2.2/wp11/api36/phpvpn.php work/wp11/logs/api36
# Prints PASS/FAIL/SKIP per test and a summary line; exit code = failures.
set -u
export MSYS_NO_PATHCONV=1

SERIAL=$1
MODULE=$2
URL=$3
LOGDIR=$4
FILTER=${5:-}
ROOT=client/android
APP_PKG=io.github.jaromirsivic.phpvpn
case "$MODULE" in
  app)
    TEST_PKG=$APP_PKG.test
    RUNNER=phpvpn.android.PhpvpnTestRunner
    APKS="$ROOT/app/build/outputs/apk/debug/app-debug.apk $ROOT/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk"
    ;;
  bridge)
    TEST_PKG=phpvpn.android.bridge.test
    RUNNER=androidx.test.runner.AndroidJUnitRunner
    APKS="$ROOT/bridge/build/outputs/apk/androidTest/debug/bridge-debug-androidTest.apk"
    ;;
  *) echo "module must be app or bridge"; exit 2 ;;
esac

mkdir -p "$LOGDIR"
RAW="$LOGDIR/$MODULE-instrument.txt"
LOGCAT="$LOGDIR/$MODULE-logcat.txt"
adb_() { adb -s "$SERIAL" "$@"; }

API=$(adb_ shell getprop ro.build.version.sdk | tr -d '\r')
echo "[$(date +%H:%M:%S)] $MODULE on $SERIAL (API $API), relay $URL"
adb_ uninstall "$TEST_PKG" >/dev/null 2>&1
[ "$MODULE" = app ] && adb_ uninstall "$APP_PKG" >/dev/null 2>&1
for apk in $APKS; do
  adb_ install -r -t -g "$apk" | tail -1
done
adb_ logcat -c
ARGS=(-e phpvpnServerUrl "$URL")
[ -n "$FILTER" ] && ARGS+=(-e class "$FILTER")
start=$SECONDS
adb_ shell am instrument -w -r "${ARGS[@]}" "$TEST_PKG/$RUNNER" | tr -d '\r' > "$RAW"
elapsed=$((SECONDS - start))
adb_ logcat -d -v threadtime > "$LOGCAT"
# The screenshots of T-U02 (Wp08ScreenshotTest, WizardScreenshots, WP10) live in
# the app's external files and go away with the uninstall.
if [ "$MODULE" = app ]; then
  rm -rf "$LOGDIR/screenshots"
  adb_ pull "/sdcard/Android/data/$APP_PKG/files" "$LOGDIR/screenshots" >/dev/null 2>&1 &&
    echo "screenshots: $(find "$LOGDIR/screenshots" -name '*.png' | wc -l) PNG in $LOGDIR/screenshots"
fi
adb_ uninstall "$TEST_PKG" >/dev/null 2>&1
[ "$MODULE" = app ] && adb_ uninstall "$APP_PKG" >/dev/null 2>&1

# INSTRUMENTATION_STATUS_CODE: 1 start, 0 ok, -1 error, -2 failure, -3 ignored, -4 assumption failure.
awk -v api="$API" -v module="$MODULE" -v secs="$elapsed" '
  /^INSTRUMENTATION_STATUS: class=/ { cls = substr($0, index($0, "=") + 1) }
  /^INSTRUMENTATION_STATUS: test=/ { tst = substr($0, index($0, "=") + 1) }
  /^INSTRUMENTATION_STATUS_CODE: / {
    code = $2
    if (code == 1) next
    name = cls "#" tst
    if (code == 0) { pass++; print "PASS " name }
    else if (code == -3 || code == -4) { skip++; print "SKIP " name }
    else { fail++; print "FAIL " name }
  }
  /^INSTRUMENTATION_RESULT: shortMsg=/ || /^INSTRUMENTATION_FAILED/ { crash = crash " " $0 }
  /^INSTRUMENTATION_CODE: / { finished = 1 }
  /^INSTRUMENTATION_STATUS: numtests=/ { total = substr($0, index($0, "=") + 1) }
  END {
    # No final INSTRUMENTATION_CODE: the stream broke (adb server restarted, device gone).
    if (!finished) crash = crash " INCOMPLETE: " (pass + fail + skip) " of " total " tests reported"
    printf "SUMMARY module=%s api=%s pass=%d fail=%d skip=%d seconds=%d%s\n", module, api, pass, fail, skip, secs, (crash ? " CRASH:" crash : "")
    exit fail + (crash ? 1 : 0)
  }' "$RAW" | tee "$LOGDIR/$MODULE-summary.txt"
exit "${PIPESTATUS[0]}"
