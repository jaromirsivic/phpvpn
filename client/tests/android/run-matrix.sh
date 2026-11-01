#!/usr/bin/env bash
# WP11: one instrumented module on several emulators at the same time, each
# against its own relay (deploy-wp11-relays.ps1). Logs per API level go to
# <log root>/api<NN>/; the summaries are printed at the end.
#
# Usage (Git Bash, worktree root, APKs built):
#   client/tests/android/run-matrix.sh <app|bridge> <log root> <serial>=<relay url> [...]
#   e.g. client/tests/android/run-matrix.sh app work/wp11/logs \
#          emulator-5556=http://10.0.2.2/wp11/api36/phpvpn.php emulator-5558=http://10.0.2.2/wp11/api35/phpvpn.php
set -u
MODULE=$1
LOGROOT=$2
shift 2
HERE=$(dirname "$0")
pids=()
outs=()
for pair in "$@"; do
  serial=${pair%%=*}
  url=${pair#*=}
  api=$(adb -s "$serial" shell getprop ro.build.version.sdk | tr -d '\r')
  out="$LOGROOT/api$api-$MODULE.out"
  mkdir -p "$LOGROOT/api$api"
  bash "$HERE/run-instrumented.sh" "$serial" "$MODULE" "$url" "$LOGROOT/api$api" > "$out" 2>&1 &
  pids+=($!)
  outs+=("$out")
done
rc=0
for i in "${!pids[@]}"; do
  wait "${pids[$i]}" || rc=1
  grep -v '^PASS ' "${outs[$i]}"
done
exit $rc
