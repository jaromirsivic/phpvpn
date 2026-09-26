#!/bin/sh
# Runs phpvpn-netns-tests as root inside a private network and mount namespace.
#
#   sudo sh tests/linux/netns/run-netns-tests.sh <build-dir> [test filter]
#
# The namespace starts with only a loopback interface, which is brought up.
# sysfs is remounted so /sys/class/net shows the interfaces of the new
# namespace. Every interface, address, route and the /etc/resolv.conf bind mount
# of the tests disappears with the namespace; the host network is never touched.
set -eu
build=${1:?usage: run-netns-tests.sh <build-dir> [test filter]}
filter=${2:-}

binary=$build/tests/phpvpn-netns-tests
if [ ! -x "$binary" ]; then
    echo "run-netns-tests.sh: $binary not found, build with PHPVPN_BUILD_PRIVILEGED_TESTS=ON" >&2
    exit 2
fi
if [ "$(id -u)" -ne 0 ]; then
    echo "run-netns-tests.sh: root is required (sudo)" >&2
    exit 2
fi
for tool in unshare ip mount; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "run-netns-tests.sh: $tool is required" >&2
        exit 2
    fi
done
binary=$(cd "$(dirname "$binary")" && pwd)/$(basename "$binary")

exec unshare --net --mount --propagation private sh -c '
    set -eu
    mount -t sysfs -o nosuid,nodev,noexec sysfs /sys
    ip link set lo up
    PHPVPN_NETNS_ISOLATED=1
    export PHPVPN_NETNS_ISOLATED
    exec "$0" "$@"
' "$binary" ${filter:+"$filter"}
