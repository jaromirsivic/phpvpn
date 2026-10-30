#!/bin/sh
# Builds the macOS installer package of the client (plan-macos-v001 step 8),
# the macOS counterpart of tools/linux/package-rpm.sh and
# build-packages-in-container.sh.
#
#   sh tools/macos/package-pkg.sh <build directory> <output directory>
#
# <build directory> is a configured and built macOS preset (by convention
# work/build/macos-release). The script
#
#   1. installs the build into a private staging root with
#      DESTDIR=<staging> cmake --install <build> --prefix /,
#   2. checks the staged file set (service, CLI, launchd daemon plist and,
#      when built, the tray bundle with its launch agent) and the arm64 slices,
#   3. signs the service, the CLI and the tray bundle ad hoc with the hardened
#      runtime (codesign --force -s - --options runtime); there is no
#      Developer ID, the product archive itself stays unsigned,
#   4. builds the component package com.pilandrome.phpvpn with pkgbuild
#      (scripts from packaging/macos/pkg/scripts, the tray bundle not
#      relocatable) and the product archive phpvpn-<version>-arm64.pkg with
#      productbuild and packaging/macos/pkg/distribution.xml,
#   5. writes <output>/SHA256SUMS (shasum -a 256 over the package) and
#      <output>/build-provenance.json (source commit and dirty flag, macOS,
#      Xcode, SDK, compiler, OpenSSL and libcurl versions, tray inclusion and
#      the package hash),
#   6. copies README.md, install.sh and uninstall.sh of packaging/macos/dist
#      next to it. install.sh still carries the placeholder signing key pin:
#      client/tools/macos/make-macos-dist.ps1 stages the signed folder of the
#      distribution from this output directory. Until then the package can be
#      installed directly with sudo installer -pkg <package> -target /.
#
# The source commit and the dirty flag (uncommitted changes under client/)
# come from git; a copy without .git must set PHPVPN_SOURCE_COMMIT and
# PHPVPN_SOURCE_DIRTY. The output directory must not hold an earlier package.
set -eu

usage() {
    echo "usage: sh tools/macos/package-pkg.sh <build directory> <output directory>"
}
fail() {
    printf 'package-pkg.sh: %s\n' "$*" >&2
    exit 1
}
json_string() {
    # Values are tool and system identifiers; quotes, backslashes and control
    # characters are dropped rather than escaped (as in the Linux provenance).
    printf '"%s"' "$(printf '%s' "$1" | tr -d '\000-\037"\\')"
}

[ $# -eq 2 ] || { usage >&2; exit 2; }
[ "$(uname -s)" = Darwin ] || fail "the package is built on macOS"
for tool in cmake codesign pkgbuild productbuild pkgutil lsbom plutil lipo shasum sw_vers xcodebuild xcrun git; do
    command -v "$tool" > /dev/null 2>&1 || fail "the tool '$tool' is missing (Xcode command line tools, cmake)"
done

client=$(cd "$(dirname "$0")/../.." && pwd -P)
build=$(cd "$1" 2> /dev/null && pwd -P) || fail "the build directory $1 does not exist"
cache=$build/CMakeCache.txt
[ -f "$cache" ] || fail "$build is not a configured CMake build directory"
cache_value() {
    sed -n "s/^$1:[A-Z]*=//p" "$cache" | head -n 1
}
home=$(cache_value CMAKE_HOME_DIRECTORY)
{ [ -n "$home" ] && [ "$(cd "$home" 2> /dev/null && pwd -P)" = "$client" ]; } ||
    fail "$build was not configured from $client"

version=$(sed -n '/^project(phpvpn-client/,/)/s/^ *VERSION \([0-9][0-9.]*\).*/\1/p' "$client/CMakeLists.txt")
case $version in
    ''|*[!0-9.]*) fail "the client version was not found in client/CMakeLists.txt" ;;
esac
minimum_os=$(cache_value CMAKE_OSX_DEPLOYMENT_TARGET)
[ "$minimum_os" = 14.0 ] || fail "the build targets macOS '$minimum_os', the package requires CMAKE_OSX_DEPLOYMENT_TARGET 14.0"
architectures=$(cache_value CMAKE_OSX_ARCHITECTURES)
case $architectures in
    ''|arm64) ;;
    *) fail "the build targets '$architectures', the package is arm64 only" ;;
esac

identifier=com.pilandrome.phpvpn
package_name=phpvpn-$version-arm64.pkg
component_name=phpvpn-component.pkg
pkg_source=$client/packaging/macos/pkg
dist_source=$client/packaging/macos/dist
for required in "$pkg_source/distribution.xml" "$pkg_source/scripts/preinstall" "$pkg_source/scripts/postinstall" \
                "$pkg_source/resources/welcome.txt" \
                "$dist_source/README.md" "$dist_source/install.sh" "$dist_source/uninstall.sh"; do
    [ -f "$required" ] || fail "$required is missing"
done

mkdir -p "$2"
output=$(cd "$2" && pwd -P)
for name in SHA256SUMS build-provenance.json README.md install.sh uninstall.sh; do
    [ ! -e "$output/$name" ] || fail "$output already holds $name"
done
for existing in "$output"/*.pkg; do
    [ ! -e "$existing" ] || fail "$output already holds ${existing##*/}"
done

scratch=$(mktemp -d "${TMPDIR:-/tmp}/phpvpn-pkg.XXXXXX") || fail "no temporary directory"
trap 'rm -rf "$scratch"' EXIT
trap 'exit 1' INT TERM HUP
root=$scratch/root
mkdir "$root" "$scratch/packages" "$scratch/resources" "$scratch/scripts"

# ------------------------------------------------------- 1. staging root
DESTDIR=$root cmake --install "$build" --prefix / > "$scratch/install.log" 2>&1 || {
    cat "$scratch/install.log" >&2
    fail "cmake --install failed"
}

# ------------------------------------------------------ 2. staged file set
service=$root/Library/PrivilegedHelperTools/phpvpn-service
cli=$root/usr/local/bin/phpvpn
daemon_plist=$root/Library/LaunchDaemons/$identifier.plist
app="$root/Applications/phpvpn Tray.app"
tray=$app/Contents/MacOS/phpvpn-tray
agent_plist=$root/Library/LaunchAgents/$identifier-tray.plist
for required in "$service" "$cli" "$daemon_plist"; do
    [ -f "$required" ] || fail "the staged file ${required#"$root"} is missing"
done
if [ -d "$app" ]; then
    tray_included=true
    for required in "$tray" "$app/Contents/Info.plist" "$agent_plist"; do
        [ -f "$required" ] || fail "the staged file ${required#"$root"} is missing"
    done
    bundle_id=$(plutil -extract CFBundleIdentifier raw -o - "$app/Contents/Info.plist" 2> /dev/null) || bundle_id=
    [ "$bundle_id" = "$identifier-tray" ] || fail "the tray bundle identifier is '$bundle_id', expected $identifier-tray"
else
    tray_included=false
    [ ! -e "$agent_plist" ] || fail "the tray launch agent is staged without the tray bundle"
fi

# Nothing else may be installed: no configuration, no test programs.
find "$root" \( -type f -o -type l \) > "$scratch/staged"
while IFS= read -r path; do
    relative=${path#"$root"/}
    case $relative in
        Library/PrivilegedHelperTools/phpvpn-service|usr/local/bin/phpvpn|Library/LaunchDaemons/$identifier.plist)
            [ ! -L "$path" ] || fail "the staged file $relative is a symbolic link" ;;
        Library/LaunchAgents/$identifier-tray.plist)
            [ ! -L "$path" ] || fail "the staged file $relative is a symbolic link" ;;
        "Applications/phpvpn Tray.app/"*) ;;
        *) fail "unexpected staged file $relative" ;;
    esac
done < "$scratch/staged"

for plist in "$daemon_plist" "$agent_plist"; do
    [ -f "$plist" ] || continue
    plutil -lint -s "$plist" || fail "${plist#"$root"} is not a valid property list"
    chmod 0644 "$plist"
done
check_arm64() {
    archs=$(lipo -archs "$1" 2> /dev/null) || fail "${1#"$root"} is not a Mach-O executable"
    archs=$(printf '%s' "$archs" | tr -s ' \n' ' ' | sed 's/^ //; s/ $//')
    [ "$archs" = arm64 ] || fail "${1#"$root"} holds the architectures '$archs', expected arm64 only"
    chmod 0755 "$1"
}
check_arm64 "$service"
check_arm64 "$cli"
[ "$tray_included" = false ] || check_arm64 "$tray"

# Directory modes as on the target system; pkgbuild records root:wheel.
find "$root" -type d -exec chmod 0755 {} +
[ ! -d "$root/Applications" ] || chmod 0775 "$root/Applications"

# ------------------------------------------------------ 3. ad hoc signature
sign() {
    codesign --force --sign - --options runtime --timestamp=none --identifier "$2" "$1" ||
        fail "codesign failed for ${1#"$root"}"
}
sign "$service" "$identifier.service"
sign "$cli" "$identifier.cli"
if [ "$tray_included" = true ]; then
    # --deep is deprecated: sign nested code first, then the main executable,
    # then the bundle itself.
    find "$app/Contents" -type f -perm -u+x ! -path "$tray" > "$scratch/nested"
    while IFS= read -r nested; do
        case $nested in
            "$app/Contents/MacOS/"*|"$app/Contents/Frameworks/"*|"$app/Contents/Helpers/"*) ;;
            *) continue ;;
        esac
        codesign --force --sign - --options runtime --timestamp=none "$nested" ||
            fail "codesign failed for ${nested#"$root"}"
    done < "$scratch/nested"
    sign "$tray" "$identifier-tray"
    sign "$app" "$identifier-tray"
fi
codesign --verify --strict --verbose=2 "$service" || fail "the service signature does not verify"
codesign --verify --strict --verbose=2 "$cli" || fail "the CLI signature does not verify"
if [ "$tray_included" = true ]; then
    codesign --verify --strict --verbose=2 "$app" || fail "the tray bundle signature does not verify"
fi

# ------------------------------------------------------------ 4. packages
# The scripts get their executable mode here: a Windows checkout does not
# record it in git.
cp "$pkg_source/scripts/preinstall" "$pkg_source/scripts/postinstall" "$scratch/scripts/"
chmod 0755 "$scratch/scripts/preinstall" "$scratch/scripts/postinstall"
cp "$pkg_source/resources/welcome.txt" "$scratch/resources/welcome.txt"

# A relocatable bundle would be installed over another copy with the same
# bundle identifier anywhere on the disk instead of /Applications.
pkgbuild --analyze --root "$root" "$scratch/component.plist" > /dev/null || fail "pkgbuild --analyze failed"
index=0
while plutil -extract "$index" xml1 -o /dev/null "$scratch/component.plist" > /dev/null 2>&1; do
    plutil -replace "$index.BundleIsRelocatable" -bool NO "$scratch/component.plist" ||
        fail "the component property list could not be edited"
    index=$((index + 1))
done
if [ "$tray_included" = true ] && [ "$index" = 0 ]; then
    fail "pkgbuild did not find the tray bundle"
fi

pkgbuild --root "$root" \
    --component-plist "$scratch/component.plist" \
    --identifier "$identifier" \
    --version "$version" \
    --install-location / \
    --ownership recommended \
    --scripts "$scratch/scripts" \
    "$scratch/packages/$component_name" || fail "pkgbuild failed"

# distribution.xml references the component by file name and carries the
# version as a placeholder.
grep -qF -e ">$component_name</pkg-ref>" "$pkg_source/distribution.xml" ||
    fail "distribution.xml does not reference $component_name"
[ "$(grep -c '@PHPVPN_VERSION@' "$pkg_source/distribution.xml")" = 1 ] ||
    fail "distribution.xml must hold exactly one @PHPVPN_VERSION@ placeholder"
sed "s/@PHPVPN_VERSION@/$version/" "$pkg_source/distribution.xml" > "$scratch/distribution.xml"

productbuild --distribution "$scratch/distribution.xml" \
    --package-path "$scratch/packages" \
    --resources "$scratch/resources" \
    "$scratch/$package_name" || fail "productbuild failed"

# The product archive must hold the component with its scripts and payload.
pkgutil --expand "$scratch/$package_name" "$scratch/expanded" || fail "the product archive could not be expanded"
[ -f "$scratch/expanded/Distribution" ] || fail "the product archive holds no Distribution"
component=$scratch/expanded/$component_name
for script in preinstall postinstall; do
    [ -f "$component/Scripts/$script" ] || fail "the component holds no $script script"
done
lsbom -s -f "$component/Bom" > "$scratch/bom" || fail "the component bill of materials could not be read"
for expected in ./Library/PrivilegedHelperTools/phpvpn-service ./usr/local/bin/phpvpn "./Library/LaunchDaemons/$identifier.plist"; do
    grep -qxF -e "$expected" "$scratch/bom" || fail "the payload does not hold $expected"
done
if [ "$tray_included" = true ]; then
    for expected in "./Applications/phpvpn Tray.app/Contents/MacOS/phpvpn-tray" "./Library/LaunchAgents/$identifier-tray.plist"; do
        grep -qxF -e "$expected" "$scratch/bom" || fail "the payload does not hold $expected"
    done
fi

cp "$scratch/$package_name" "$output/$package_name"

# ---------------------------------------------------- 5. sums, provenance
(cd "$output" && shasum -a 256 "$package_name" > SHA256SUMS)
package_sha=$(shasum -a 256 "$output/$package_name" | cut -d ' ' -f 1)
package_bytes=$(wc -c < "$output/$package_name" | tr -d ' ')

commit=${PHPVPN_SOURCE_COMMIT:-}
dirty=${PHPVPN_SOURCE_DIRTY:-}
if git -C "$client" rev-parse --git-dir > /dev/null 2>&1; then
    commit=$(git -C "$client" rev-parse HEAD)
    # Only client/ goes into the package.
    if [ -n "$(git -C "$client" status --porcelain -- .)" ]; then dirty=true; else dirty=false; fi
fi
printf '%s' "$commit" | grep -Eq '^[0-9a-f]{40}$' ||
    fail "the tree has no .git; set PHPVPN_SOURCE_COMMIT to the full source commit"
case $dirty in
    true|false) ;;
    *) fail "the tree has no .git; set PHPVPN_SOURCE_DIRTY to true or false" ;;
esac

compiler_path=$(cache_value CMAKE_CXX_COMPILER)
compiler=$("${compiler_path:-c++}" --version 2> /dev/null | head -n 1) || compiler=
# The OpenSSL the build linked statically: the include directory of the
# CMake cache, PHPVPN_OPENSSL_ROOT, then the Homebrew openssl@3 prefix.
openssl_include=$(cache_value OPENSSL_INCLUDE_DIR)
openssl_tool=
if [ -n "$openssl_include" ] && [ -x "${openssl_include%/include}/bin/openssl" ]; then
    openssl_tool=${openssl_include%/include}/bin/openssl
elif [ -n "${PHPVPN_OPENSSL_ROOT:-}" ] && [ -x "$PHPVPN_OPENSSL_ROOT/bin/openssl" ]; then
    openssl_tool=$PHPVPN_OPENSSL_ROOT/bin/openssl
elif command -v brew > /dev/null 2>&1; then
    brew_prefix=$(brew --prefix openssl@3 2> /dev/null) || brew_prefix=
    if [ -n "$brew_prefix" ] && [ -x "$brew_prefix/bin/openssl" ]; then openssl_tool=$brew_prefix/bin/openssl; fi
fi
openssl=
if [ -n "$openssl_tool" ]; then openssl=$("$openssl_tool" version 2> /dev/null) || openssl=; fi
libcurl=$(/usr/bin/curl --version 2> /dev/null | head -n 1 | sed -n 's/.*\(libcurl\/[0-9][0-9.]*\).*/\1/p') || libcurl=
xcode=$(xcodebuild -version 2> /dev/null | head -n 1) || xcode=
sdk=$(xcrun --sdk macosx --show-sdk-version 2> /dev/null) || sdk=

{
    printf '{"formatVersion":1,"product":"phpvpn","platform":"macos","version":%s,' "$(json_string "$version")"
    printf '"architecture":"arm64","minimumOsVersion":%s,' "$(json_string "$minimum_os")"
    printf '"sourceCommit":%s,"sourceDirty":%s,' "$(json_string "$commit")" "$dirty"
    printf '"builtOn":%s,"buildVersion":%s,"hostArchitecture":%s,' \
        "$(json_string "$(sw_vers -productVersion)")" "$(json_string "$(sw_vers -buildVersion)")" \
        "$(json_string "$(uname -m)")"
    printf '"xcode":%s,"sdk":%s,"compiler":%s,"cmake":%s,' \
        "$(json_string "${xcode:-unknown}")" "$(json_string "macosx ${sdk:-unknown}")" \
        "$(json_string "${compiler:-unknown}")" "$(json_string "$(cmake --version | head -n 1)")"
    printf '"libraries":{"openssl":%s,"libcurl":%s},' \
        "$(json_string "${openssl:-unknown}")" "$(json_string "${libcurl:-unknown}")"
    printf '"runner":{"imageOs":%s,"imageVersion":%s,"repository":%s,"runId":%s,"runAttempt":%s},' \
        "$(json_string "${ImageOS:-}")" "$(json_string "${ImageVersion:-}")" "$(json_string "${GITHUB_REPOSITORY:-}")" \
        "$(json_string "${GITHUB_RUN_ID:-}")" "$(json_string "${GITHUB_RUN_ATTEMPT:-}")"
    printf '"trayIncluded":%s,"codeSignature":"ad hoc","builtAt":%s,' \
        "$tray_included" "$(json_string "$(date -u +%Y-%m-%dT%H:%M:%SZ)")"
    printf '"packages":[{"file":%s,"bytes":%s,"sha256":"%s"}]}\n' \
        "$(json_string "$package_name")" "$package_bytes" "$package_sha"
} > "$output/build-provenance.json"

# -------------------------------------------------- 6. installer entry points
cp "$dist_source/README.md" "$dist_source/install.sh" "$dist_source/uninstall.sh" "$output/"
chmod 0644 "$output/README.md"
chmod 0755 "$output/install.sh" "$output/uninstall.sh"

if [ "$tray_included" = true ]; then tray_text=included; else tray_text="not included (headless build)"; fi
if [ "$dirty" = true ]; then dirty_text=" (dirty)"; else dirty_text=; fi
echo ""
echo "package       : $output/$package_name"
echo "version       : $version (arm64, macOS $minimum_os or newer)"
echo "source        : $commit$dirty_text"
echo "tray          : $tray_text"
echo "signature     : ad hoc code signature, unsigned product archive"
echo "sha256        : $package_sha"
echo "next step     : copy $output to work/macos-packages/latest of the Windows checkout and run tools/make-distribution.ps1"
