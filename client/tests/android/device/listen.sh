#!/system/bin/sh
# Echo listener on the phone for host->phone TCP tests; ends after $1 seconds.
toybox timeout ${1:-120} toybox nc -L -p 5555 cat
