#!/bin/sh
# Builds the phpvpn RPM from the client/ tree with packaging/linux/rpm/phpvpn.spec
# (plan-v001 section 4.13). Run as an unprivileged user on the target Fedora
# release with the build dependencies of the spec installed; %check runs the
# unprivileged ctest suite.
#
#   sh tools/linux/package-rpm.sh <output-directory>
#
# PHPVPN_PACKAGE_RELEASE sets the RPM release (default 1). Only the binary
# packages are copied to <output-directory> (phpvpn-<version>-<release>.x86_64.rpm
# and phpvpn-tray-<version>-<release>.x86_64.rpm); debuginfo and debugsource stay in the temporary
# rpmbuild tree, which is removed.
set -eu
output=${1:?usage: package-rpm.sh <output-directory>}
release=${PHPVPN_PACKAGE_RELEASE:-1}
client=$(cd "$(dirname "$0")/../.." && pwd)
spec=$client/packaging/linux/rpm/phpvpn.spec

case $release in
    ''|*[!0-9]*) echo "package-rpm.sh: PHPVPN_PACKAGE_RELEASE must be a positive integer" >&2; exit 2 ;;
esac
if [ "$(id -u)" = 0 ] && [ "${PHPVPN_ALLOW_ROOT_BUILD:-0}" != 1 ]; then
    echo "package-rpm.sh: build as an unprivileged user (the %check tests must not run as root)" >&2
    exit 2
fi

version=$(sed -n '/^project(phpvpn-client/,/)/s/^ *VERSION \([0-9][0-9.]*\).*/\1/p' "$client/CMakeLists.txt")
spec_version=$(sed -n 's/^Version:[[:space:]]*//p' "$spec")
if [ -z "$version" ] || [ "$version" != "$spec_version" ]; then
    echo "package-rpm.sh: spec Version '$spec_version' differs from the CMake project version '$version'" >&2
    exit 1
fi

mkdir -p "$output"
output=$(cd "$output" && pwd)
top=$(mktemp -d "${TMPDIR:-/tmp}/phpvpn-rpm.XXXXXX")
trap 'rm -rf "$top"' EXIT INT TERM
mkdir -p "$top/SOURCES" "$top/stage"

# Source snapshot: the client tree without the Windows only tunnel driver blobs.
cp -R "$client" "$top/stage/phpvpn-$version"
rm -rf "$top/stage/phpvpn-$version/third_party/wintun"
tar -C "$top/stage" -czf "$top/SOURCES/phpvpn-$version.tar.gz" "phpvpn-$version"

# A rebuild with another release gets its own changelog entry in a staged copy
# of the spec, with the date and packager of the entry it follows.
if [ "$release" = 1 ]; then
    cp "$spec" "$top/phpvpn.spec"
else
    previous=$(sed -n '/^%changelog$/{n;p;q}' "$spec")
    entry=$(printf '%s' "$previous" | sed "s/ - [^ ]*\$/ - $version-$release/")
    awk -v entry="$entry" -v release="$release" \
        '{ print } /^%changelog$/ { print entry; print "- Rebuild " release " of the unchanged source."; print "" }' \
        "$spec" > "$top/phpvpn.spec"
fi

rpmbuild -bb \
    --define "_topdir $top" \
    --define "phpvpn_release $release" \
    "$top/phpvpn.spec"

found=0
for package in "$top"/RPMS/*/phpvpn-*.rpm; do
    [ -f "$package" ] || continue
    case ${package##*/} in
        *-debuginfo-*|*-debugsource-*) continue ;;
    esac
    cp "$package" "$output/"
    echo "package: $output/${package##*/}"
    found=$((found + 1))
done
[ "$found" -gt 0 ] || { echo "package-rpm.sh: rpmbuild produced no package" >&2; exit 1; }
