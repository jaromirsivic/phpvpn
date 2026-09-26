#!/bin/sh
# Builds the phpvpn Debian package from the client/ tree (plan-v001 section
# 4.13). Run as an unprivileged user on Ubuntu 24.04 with the Build-Depends of
# packaging/linux/debian/control installed; dh_auto_test runs the unprivileged
# ctest suite.
#
#   sh tools/linux/package-deb.sh <output-directory>
#
# debhelper expects debian/ at the source root, so the script builds from a
# staging copy of client/ with packaging/linux/debian copied to debian/.
# PHPVPN_PACKAGE_RELEASE sets the Debian revision (default 1); another value
# adds a rebuild entry to the staged changelog. Only the binary packages
# (phpvpn_<version>-<revision>_amd64.deb, later also phpvpn-tray) are copied to
# <output-directory>; dbgsym packages stay in the removed staging directory.
set -eu
output=${1:?usage: package-deb.sh <output-directory>}
release=${PHPVPN_PACKAGE_RELEASE:-1}
client=$(cd "$(dirname "$0")/../.." && pwd)

case $release in
    ''|*[!0-9]*) echo "package-deb.sh: PHPVPN_PACKAGE_RELEASE must be a positive integer" >&2; exit 2 ;;
esac
if [ "$(id -u)" = 0 ] && [ "${PHPVPN_ALLOW_ROOT_BUILD:-0}" != 1 ]; then
    echo "package-deb.sh: build as an unprivileged user (the tests must not run as root)" >&2
    exit 2
fi

version=$(sed -n '/^project(phpvpn-client/,/)/s/^ *VERSION \([0-9][0-9.]*\).*/\1/p' "$client/CMakeLists.txt")
[ -n "$version" ] || { echo "package-deb.sh: no project version in CMakeLists.txt" >&2; exit 1; }

mkdir -p "$output"
output=$(cd "$output" && pwd)
top=$(mktemp -d "${TMPDIR:-/tmp}/phpvpn-deb.XXXXXX")
trap 'rm -rf "$top"' EXIT INT TERM
source=$top/phpvpn-$version

cp -R "$client" "$source"
rm -rf "$source/third_party/wintun" "$source/debian"
cp -R "$client/packaging/linux/debian" "$source/debian"
chmod 0755 "$source/debian/rules"

changelog_version=$(cd "$source" && dpkg-parsechangelog -S Version)
if [ "$changelog_version" != "$version-1" ]; then
    echo "package-deb.sh: debian/changelog version '$changelog_version' differs from '$version-1'" >&2
    exit 1
fi
if [ "$release" != 1 ]; then
    maintainer=$(sed -n 's/^Maintainer:[[:space:]]*//p' "$source/debian/control")
    {
        printf 'phpvpn (%s-%s) unstable; urgency=medium\n\n' "$version" "$release"
        printf '  * Rebuild %s of the unchanged source.\n\n' "$release"
        # Dated after the previous entry: a rebuild is never older than it.
        timestamp=$(cd "$source" && dpkg-parsechangelog -S Timestamp)
        printf ' -- %s  %s\n\n' "$maintainer" "$(date -u -R -d "@$((timestamp + release - 1))")"
        cat "$source/debian/changelog"
    } > "$source/debian/changelog.new"
    mv "$source/debian/changelog.new" "$source/debian/changelog"
fi

(cd "$source" && dpkg-buildpackage -us -uc -b)

found=0
for package in "$top"/phpvpn*_*.deb; do
    [ -f "$package" ] || continue
    case ${package##*/} in
        *-dbgsym_*) continue ;;
    esac
    cp "$package" "$output/"
    echo "package: $output/${package##*/}"
    found=$((found + 1))
done
[ "$found" -gt 0 ] || { echo "package-deb.sh: dpkg-buildpackage produced no package" >&2; exit 1; }
