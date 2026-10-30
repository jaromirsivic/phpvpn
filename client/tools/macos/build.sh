#!/bin/sh
# Configures, builds and tests the client with one of the macOS CMake presets
# (plan-macos-v001 step 0), the macOS counterpart of tools/linux/build.sh.
#
#   sh tools/macos/build.sh <preset> [extra cmake configure arguments]
#
# Presets: macos-debug, macos-release, macos-asan, macos-headless. The presets
# link OpenSSL 3 statically from PHPVPN_OPENSSL_ROOT (default: the Homebrew
# openssl@3 prefix) and libcurl from the macOS SDK. PHPVPN_BUILD_DIR overrides
# the preset binary directory. Tests that need root (label "privileged") are
# left to the caller: sudo ctest --test-dir <build> -L privileged.
set -eu
preset=${1:?usage: build.sh <preset> [extra cmake configure arguments]}
shift
cd "$(dirname "$0")/../.."

case $preset in
    macos-*) ;;
    *) echo "build.sh: '$preset' is not a macOS preset" >&2; exit 2 ;;
esac

if [ -z "${PHPVPN_OPENSSL_ROOT:-}" ]; then
    if command -v brew > /dev/null 2>&1 && brew --prefix openssl@3 > /dev/null 2>&1; then
        PHPVPN_OPENSSL_ROOT=$(brew --prefix openssl@3)
    else
        echo "build.sh: set PHPVPN_OPENSSL_ROOT to an OpenSSL 3 prefix with static libraries (brew install openssl@3)" >&2
        exit 1
    fi
fi
export PHPVPN_OPENSSL_ROOT

if [ -n "${PHPVPN_BUILD_DIR:-}" ]; then
    cmake --preset "$preset" -B "$PHPVPN_BUILD_DIR" "$@"
    cmake --build "$PHPVPN_BUILD_DIR"
    ctest --test-dir "$PHPVPN_BUILD_DIR" --output-on-failure -LE privileged
else
    cmake --preset "$preset" "$@"
    cmake --build --preset "$preset"
    ctest --preset "$preset" -LE privileged
fi
