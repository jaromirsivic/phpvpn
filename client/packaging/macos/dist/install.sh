#!/bin/sh
# Installs the phpvpn client for macOS from this folder (plan-macos-v001
# step 8), the counterpart of the Linux install.sh.
#
#   sudo sh install.sh [--headless] [--verify-only]
#
# The order is fail closed: nothing is installed or changed until every
# verification passed.
#
#   1. root (the script re-runs itself once through sudo),
#   2. macOS 14 or newer (sw_vers) on Apple Silicon (arm64),
#   3. the manifest signature: the public key of release-signing.cer must match
#      the SHA-256 of the pinned signing key below, and release-manifest.sig
#      must verify release-manifest.json with it (RSA PKCS#1 v1.5, SHA-256),
#   4. the size and SHA-256 of every file listed in the manifest, verified on a
#      private root owned copy that is also the installation source; an
#      unlisted package or script in the folder is refused,
#   5. installer -pkg <copy>/phpvpn-<version>-arm64.pkg -target /; its
#      scripts stop an older installation, start the launchd daemon and, unless
#      --headless is given, register the tray for the logged in user,
#   6. the control socket /private/var/run/phpvpn/control-v1.sock within 30
#      seconds,
#   7. phpvpn status.
#
# --verify-only stops after step 4 without installing anything and needs no
# root. --headless installs no tray autostart (PHPVPN_INSTALL_TRAY=0).
# The tools used are part of macOS: openssl (LibreSSL), shasum, installer.
set -eu

# Set by the packager from the configured publisher certificate.
PINNED_SPKI_SHA256=@PHPVPN_SIGNING_SPKI_SHA256@
SOCKET=/private/var/run/phpvpn/control-v1.sock
SOCKET_TIMEOUT=30
MINIMUM_MACOS=14
CLI=/usr/local/bin/phpvpn
STATE_DIR=/private/var/db/phpvpn
OPTIONS_MARKER=$STATE_DIR/pending-install-options
LOG_DIR=/private/var/log/phpvpn

usage() {
    echo "usage: sudo sh install.sh [--headless] [--verify-only]"
}
refuse() {
    printf 'installation refused: %s\n' "$*" >&2
    exit 1
}
failed() {
    printf 'installation failed: %s\n' "$*" >&2
    exit 1
}

headless=false
verify_only=false
for argument in "$@"; do
    case $argument in
        --headless) headless=true ;;
        --verify-only) verify_only=true ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done

package=$(cd "$(dirname "$0")" && pwd -P)

case $PINNED_SPKI_SHA256 in
    *[!0-9a-f]*|'')
        refuse "this install.sh is the unstaged template of a package build without a signing key pin; use the signed distribution folder (or install the .pkg directly with sudo installer -pkg <file> -target /)" ;;
esac

# ----------------------------------------------------------------- 1. root
if [ "$verify_only" = false ] && [ "$(id -u)" != 0 ]; then
    if command -v sudo > /dev/null 2>&1; then
        exec sudo sh "$package/install.sh" "$@"
    fi
    refuse "run the installer as root: sudo sh install.sh"
fi

# ---------------------------------------------------- 2. system and CPU
[ "$(uname -s)" = Darwin ] || refuse "this package is for macOS, the system is $(uname -s)"
os_version=$(sw_vers -productVersion 2> /dev/null) || refuse "the macOS version could not be read (sw_vers)"
case $os_version in
    ''|*[!0-9.]*) refuse "the macOS version '$os_version' is not recognized" ;;
esac
os_major=${os_version%%.*}
[ "$os_major" -ge "$MINIMUM_MACOS" ] || refuse "macOS $os_version is older than the supported macOS $MINIMUM_MACOS"
# A shell under Rosetta reports x86_64; hw.optional.arm64 names the CPU.
if [ "$(uname -m)" != arm64 ] && [ "$(sysctl -n hw.optional.arm64 2> /dev/null)" != 1 ]; then
    refuse "this package is for Apple Silicon (arm64), the Mac has a $(uname -m) processor"
fi

for tool in openssl shasum sed tr od wc head cut grep awk installer; do
    command -v "$tool" > /dev/null 2>&1 || refuse "the tool '$tool' is missing (it is part of macOS)"
done

# Root: a private directory below the sticky /private/tmp, never below a
# TMPDIR that sudo may have kept from the user. --verify-only: the user's own.
if [ "$(id -u)" = 0 ]; then temporary=/private/tmp; else temporary=${TMPDIR:-/tmp}; fi
work=$(mktemp -d "${temporary%/}/phpvpn-install.XXXXXX") || refuse "no temporary directory"
trap 'rm -rf "$work"' EXIT
trap 'exit 1' INT TERM HUP
chmod 0700 "$work"
mkdir "$work/verify" "$work/files"

# --------------------------------------------- 3. manifest and signature
# cp -X: the copies do not inherit extended attributes such as quarantine.
for name in release-manifest.json release-manifest.sig release-signing.cer; do
    [ -f "$package/$name" ] || refuse "the package is incomplete, $name is missing"
    cp -X "$package/$name" "$work/verify/$name"
done
manifest_file=$work/verify/release-manifest.json

if [ "$(head -c 3 "$manifest_file" | od -An -tx1 | tr -d ' \n')" = efbbbf ]; then
    refuse "the manifest carries a byte order mark and is therefore not canonical"
fi
if [ "$(wc -l < "$manifest_file" | tr -d ' ')" != 0 ] || [ "$(tr -d '\r' < "$manifest_file" | wc -c | tr -d ' ')" != "$(wc -c < "$manifest_file" | tr -d ' ')" ]; then
    refuse "the manifest is not in its canonical single line form"
fi

certificate=$work/verify/release-signing.cer
if grep -q -e '-----BEGIN CERTIFICATE-----' "$certificate"; then form=PEM; else form=DER; fi
openssl x509 -inform "$form" -in "$certificate" -noout -pubkey > "$work/verify/public.pem" 2> /dev/null ||
    refuse "release-signing.cer is not a readable certificate"
openssl pkey -pubin -in "$work/verify/public.pem" -outform DER -out "$work/verify/public.der" 2> /dev/null ||
    refuse "the public key of release-signing.cer could not be read"
spki=$(shasum -a 256 "$work/verify/public.der" | cut -d ' ' -f 1)
[ "$spki" = "$PINNED_SPKI_SHA256" ] || refuse "the package was signed by a key that is not pinned"
# OpenSSL prints "sha1 Fingerprint=", LibreSSL "SHA1 Fingerprint=".
thumbprint=$(openssl x509 -inform "$form" -in "$certificate" -noout -fingerprint -sha1 | sed 's/^.*=//; s/://g' | tr 'a-f' 'A-F')

detached=$(cat "$work/verify/release-manifest.sig")
algorithm=$(printf '%s' "$detached" | sed -n 's/^{"algorithm":"\([^"]*\)",.*/\1/p')
signed_thumbprint=$(printf '%s' "$detached" | sed -n 's/^{[^}]*,"thumbprint":"\([0-9A-Fa-f]*\)",.*/\1/p' | tr 'a-f' 'A-F')
signature=$(printf '%s' "$detached" | sed -n 's|^{.*,"signature":"\([A-Za-z0-9+/=]*\)"}$|\1|p')
[ "$algorithm" = RSA-PKCS1-SHA256 ] || refuse "the signature algorithm '$algorithm' is not supported"
[ -n "$signature" ] || refuse "release-manifest.sig holds no signature"
[ "$signed_thumbprint" = "$thumbprint" ] || refuse "the signature names an identity other than release-signing.cer"
printf '%s' "$signature" | openssl base64 -d -A > "$work/verify/signature.bin" 2> /dev/null ||
    refuse "the signature is not valid base64"
[ -s "$work/verify/signature.bin" ] || refuse "the signature is not valid base64"
openssl dgst -sha256 -verify "$work/verify/public.pem" -signature "$work/verify/signature.bin" \
    "$manifest_file" > /dev/null 2>&1 || refuse "the manifest signature did not verify"

# From here on the manifest content is authentic.
manifest=$(cat "$manifest_file")
manifest_string() {
    printf '%s' "$manifest" | sed -n "s/^{.*,\"$1\":\"\\([^\"]*\\)\",.*/\\1/p"
}
[ "$(printf '%s' "$manifest" | sed -n 's/^{"manifestVersion":\([0-9]*\),.*/\1/p')" = 1 ] ||
    refuse "the manifest format version is not supported"
[ "$(manifest_string product)" = phpvpn-client ] || refuse "the manifest is not a phpvpn client manifest"
[ "$(manifest_string platform)" = macos ] || refuse "the manifest is not a macOS client manifest"
[ "$(manifest_string architecture)" = arm64 ] || refuse "the manifest is not for arm64"
[ "$(manifest_string signingIdentity | tr 'a-f' 'A-F')" = "$thumbprint" ] ||
    refuse "the manifest names an identity other than release-signing.cer"
release_version=$(manifest_string releaseVersion)
case $release_version in
    ''|*[!0-9.]*) refuse "the manifest release version '$release_version' is not valid" ;;
esac
echo "manifest      : verified, release $release_version for arm64, signed by $thumbprint"

minimum=$(manifest_string minimumOsVersion)
case $minimum in
    ''|*[!0-9.]*) refuse "the manifest minimum macOS version '$minimum' is not valid" ;;
esac
version_at_least() {
    have_major=${1%%.*}
    have_minor=${1#"$have_major"}; have_minor=${have_minor#.}; have_minor=${have_minor%%.*}
    need_major=${2%%.*}
    need_minor=${2#"$need_major"}; need_minor=${need_minor#.}; need_minor=${need_minor%%.*}
    [ "$have_major" -gt "$need_major" ] ||
        { [ "$have_major" -eq "$need_major" ] && [ "${have_minor:-0}" -ge "${need_minor:-0}" ]; }
}
version_at_least "$os_version" "$minimum" || refuse "macOS $os_version is older than the supported macOS $minimum"

# -------------------------------------------------- 4. per file SHA-256
# One entry per line; awk terminates every line, so the last entry is read too.
printf '%s' "$manifest" | sed -n 's/^{.*,"files":\[\(.*\)\]}$/\1/p' | tr '{' '\n' | awk 'NF' > "$work/verify/entries"
[ -s "$work/verify/entries" ] || refuse "the manifest lists no files"
declared=$(printf '%s' "$manifest" | grep -o '"path":' | wc -l | tr -d ' ')
[ "$(wc -l < "$work/verify/entries" | tr -d ' ')" = "$declared" ] || refuse "the manifest file list is malformed"
: > "$work/verify/listed"
count=0
while IFS= read -r entry; do
    fields=$(printf '%s' "$entry" |
        sed -n 's/^"path":"\([A-Za-z0-9._+-]*\)","bytes":\([0-9]*\),"sha256":"\([0-9a-f]\{64\}\)"},\{0,1\}$/\1 \2 \3/p')
    [ -n "$fields" ] || refuse "the manifest holds a malformed file entry"
    set -- $fields
    name=$1 bytes=$2 hash=$3
    case $name in
        .*|-*|release-manifest.json|release-manifest.sig|release-signing.cer)
            refuse "the manifest lists the invalid file name $name" ;;
    esac
    [ -f "$package/$name" ] || refuse "the packaged file $name is missing"
    cp -X "$package/$name" "$work/files/$name"
    chmod 0644 "$work/files/$name"
    [ "$(wc -c < "$work/files/$name" | tr -d ' ')" = "$bytes" ] ||
        refuse "the packaged file $name does not match its recorded size"
    [ "$(shasum -a 256 "$work/files/$name" | cut -d ' ' -f 1)" = "$hash" ] ||
        refuse "the packaged file $name does not match its recorded hash"
    echo "$name" >> "$work/verify/listed"
    count=$((count + 1))
done < "$work/verify/entries"

is_listed() {
    grep -qxF -e "$1" "$work/verify/listed"
}
for file in "$package"/*.pkg "$package"/*.mpkg "$package"/*.sh; do
    [ -e "$file" ] || continue
    is_listed "${file##*/}" || refuse "the package holds the unexpected file ${file##*/}"
done
for name in install.sh uninstall.sh; do
    is_listed "$name" || refuse "the package does not hold $name"
done
pkg=phpvpn-$release_version-arm64.pkg
is_listed "$pkg" || refuse "the package does not hold $pkg"
pkgs=$(grep -c '\.pkg$' "$work/verify/listed" || true)
[ "$pkgs" = 1 ] || refuse "the package must hold exactly one installer package"
[ "$(head -c 4 "$work/files/$pkg")" = 'xar!' ] || refuse "$pkg is not a macOS installer package"
echo "package files : $count verified"

if [ "$verify_only" = true ]; then
    echo "verification  : passed for macOS $os_version ($pkg), nothing was installed"
    exit 0
fi

# ---------------------------------------------------------- 5. installer
# The package scripts read the headless choice from the environment and,
# because installer does not reliably pass it on, from a root only marker.
if [ -L "$STATE_DIR" ]; then refuse "$STATE_DIR is a symbolic link"; fi
if [ ! -d "$STATE_DIR" ]; then
    mkdir -p "$STATE_DIR"
    chown root:wheel "$STATE_DIR"
    chmod 0700 "$STATE_DIR"
fi
rm -f "$OPTIONS_MARKER"
if [ "$headless" = true ]; then
    PHPVPN_INSTALL_TRAY=0
    echo 'PHPVPN_INSTALL_TRAY=0' > "$OPTIONS_MARKER"
else
    PHPVPN_INSTALL_TRAY=1
fi
export PHPVPN_INSTALL_TRAY
trap 'rm -f "$OPTIONS_MARKER"; rm -rf "$work"' EXIT

echo "installing    : $pkg"
installer -pkg "$work/files/$pkg" -target / ||
    failed "installer could not install $pkg (/var/log/install.log, $LOG_DIR/launchd.log)"
rm -f "$OPTIONS_MARKER"

# ------------------------------------------------------------ 6. service
waited=0
until [ -S "$SOCKET" ]; do
    if [ "$waited" -ge "$SOCKET_TIMEOUT" ]; then
        failed "the service did not create $SOCKET within $SOCKET_TIMEOUT seconds ($LOG_DIR/phpvpn-service.log)"
    fi
    sleep 1
    waited=$((waited + 1))
done

# ------------------------------------------------------------- 7. status
"$CLI" status || failed "phpvpn status did not answer ($LOG_DIR/phpvpn-service.log)"

echo ""
echo "phpvpn $release_version installed successfully, the launchd service com.pilandrome.phpvpn is running."
if [ "$headless" = true ]; then
    echo "Headless installation: the tray does not start at login (open \"/Applications/phpvpn Tray.app\" starts it by hand)."
elif [ -d "/Applications/phpvpn Tray.app" ]; then
    echo "The tray runs in the menu bar of the logged in user and starts at every login."
fi
echo "Configure the connection with 'phpvpn set-config <file>' (or the tray Wizard) and connect with 'phpvpn connect'."
