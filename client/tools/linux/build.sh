#!/bin/sh
# Configures, builds and tests the client with one of the Linux CMake presets.
#
#   sh tools/linux/build.sh <preset> [extra cmake configure arguments]
#
# Presets: linux-debug, linux-release, linux-asan, linux-clang, linux-headless.
# PHPVPN_BUILD_DIR overrides the preset binary directory, for example inside a
# container that shares the source tree with the host build.
set -eu
preset=${1:?usage: build.sh <preset> [extra cmake configure arguments]}
shift
cd "$(dirname "$0")/../.."

case $preset in
    linux-*) ;;
    *) echo "build.sh: '$preset' is not a Linux preset" >&2; exit 2 ;;
esac

if [ -n "${PHPVPN_BUILD_DIR:-}" ]; then
    cmake --preset "$preset" -B "$PHPVPN_BUILD_DIR" "$@"
    cmake --build "$PHPVPN_BUILD_DIR"
    ctest --test-dir "$PHPVPN_BUILD_DIR" --output-on-failure
else
    cmake --preset "$preset" "$@"
    cmake --build --preset "$preset"
    ctest --preset "$preset"
fi
