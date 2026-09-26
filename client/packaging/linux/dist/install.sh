#!/bin/sh
# Installs the phpvpn client for Linux from this folder (plan-v001 section
# 4.13).
#
#   sudo sh install.sh [--headless | --with-tray] [--no-weak-deps] [--verify-only]
#
# The order is fail closed: nothing is installed or changed until every
# verification passed.
#
#   1. root (the script re-runs itself once through sudo),
#   2. the distribution from /etc/os-release (Fedora or Ubuntu),
#   3. the manifest signature: the public key of release-signing.cer must match
#      the SHA-256 of the pinned signing key below, and release-manifest.sig
#      must verify release-manifest.json with it (RSA PKCS#1 v1.5, SHA-256),
#   4. the size and SHA-256 of every file listed in the manifest, verified on a
#      private root owned copy that is also the installation source,
#   5. the packages with dnf or apt-get, their dependencies from the
#      distribution repositories,
#   6. phpvpn-tray when the folder holds it and the system has a graphical
#      login (default target graphical.target and a display manager);
#      --with-tray installs it anyway, --headless skips it. An installed
#      phpvpn-tray is always upgraded together with phpvpn, whose exact
#      release it requires,
#   7. systemctl enable --now phpvpn.service,
#   8. the control socket /run/phpvpn/control-v1.sock within 30 seconds,
#   9. phpvpn status.
#
# --verify-only stops after step 4 without installing anything and needs no
# root. --no-weak-deps installs no weak (Fedora) or recommended (Ubuntu)
# dependencies. A running tray is not stopped; a new tray version takes
# effect at the next login.
set -eu

# SHA-256 of the DER SubjectPublicKeyInfo of the phpvpn signing key
# (client/assets/signing/phpvpn-development-test.cer, the development
# certificate that also signs the Windows package).
PINNED_SPKI_SHA256=1ac5908c54a54dc3daa1465113407b11ff8ab817ca1fb9c65397136a7b0f15f0
SOCKET=/run/phpvpn/control-v1.sock
SOCKET_TIMEOUT=30

usage() {
    echo "usage: sudo sh install.sh [--headless | --with-tray] [--no-weak-deps] [--verify-only]"
}
refuse() {
    printf 'installation refused: %s\n' "$*" >&2
    exit 1
}
failed() {
    printf 'installation failed: %s\n' "$*" >&2
    exit 1
}

headless=auto
verify_only=false
weak_deps=true
for argument in "$@"; do
    case $argument in
        --headless) [ "$headless" != no ] || { usage >&2; exit 2; }; headless=yes ;;
        --with-tray) [ "$headless" != yes ] || { usage >&2; exit 2; }; headless=no ;;
        --verify-only) verify_only=true ;;
        --no-weak-deps) weak_deps=false ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done

package=$(cd "$(dirname "$0")" && pwd -P)

# ----------------------------------------------------------------- 1. root
if [ "$verify_only" = false ] && [ "$(id -u)" != 0 ]; then
    if command -v sudo > /dev/null 2>&1; then
        exec sudo sh "$package/install.sh" "$@"
    fi
    refuse "run the installer as root: sudo sh install.sh"
fi

# ------------------------------------------------------- 2. distribution
[ -r /etc/os-release ] || refuse "/etc/os-release is missing, the distribution is unknown"
os_value() {
    sed -n "s/^$1=//p" /etc/os-release | head -n 1 | tr -d "\"'"
}
os_id=$(os_value ID)
os_version=$(os_value VERSION_ID)
case $os_id in
    fedora) kind=rpm ;;
    ubuntu) kind=deb ;;
    *) refuse "the distribution '${os_id:-unknown}' is not supported (supported: Fedora, Ubuntu)" ;;
esac
case $os_version in
    ''|*[!0-9.]*) refuse "the $os_id version '${os_version}' is not recognized" ;;
esac
[ "$(uname -m)" = x86_64 ] || refuse "this package is for x86_64, the system is $(uname -m)"

for tool in openssl sha256sum sed tr od wc head cut; do
    command -v "$tool" > /dev/null 2>&1 || refuse "the verification tool '$tool' is missing (Fedora: dnf install openssl, Ubuntu: apt-get install openssl)"
done

work=$(mktemp -d "${TMPDIR:-/tmp}/phpvpn-install.XXXXXX") || refuse "no temporary directory"
trap 'rm -rf "$work"' EXIT
trap 'exit 1' INT TERM HUP
# Root owned and not writable by others, readable by the apt sandbox user.
chmod 0755 "$work"
mkdir "$work/verify" "$work/files"
chmod 0700 "$work/verify"

# --------------------------------------------- 3. manifest and signature
for name in release-manifest.json release-manifest.sig release-signing.cer; do
    [ -f "$package/$name" ] || refuse "the package is incomplete, $name is missing"
    cp "$package/$name" "$work/verify/$name"
done
manifest_file=$work/verify/release-manifest.json

if [ "$(head -c 3 "$manifest_file" | od -An -tx1 | tr -d ' \n')" = efbbbf ]; then
    refuse "the manifest carries a byte order mark and is therefore not canonical"
fi
if [ "$(wc -l < "$manifest_file" | tr -d ' ')" != 0 ] || [ "$(tr -d '\r' < "$manifest_file" | wc -c)" != "$(wc -c < "$manifest_file")" ]; then
    refuse "the manifest is not in its canonical single line form"
fi

certificate=$work/verify/release-signing.cer
if grep -q -e '-----BEGIN CERTIFICATE-----' "$certificate"; then form=PEM; else form=DER; fi
openssl x509 -inform "$form" -in "$certificate" -noout -pubkey > "$work/verify/public.pem" 2> /dev/null ||
    refuse "release-signing.cer is not a readable certificate"
spki=$(openssl pkey -pubin -in "$work/verify/public.pem" -outform DER 2> /dev/null | sha256sum | cut -d ' ' -f 1)
[ "$spki" = "$PINNED_SPKI_SHA256" ] || refuse "the package was signed by a key that is not pinned"
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
[ "$(manifest_string platform)" = linux ] || refuse "the manifest is not a Linux client manifest"
[ "$(manifest_string architecture)" = x64 ] || refuse "the manifest is not for x64"
[ "$(manifest_string signingIdentity | tr 'a-f' 'A-F')" = "$thumbprint" ] ||
    refuse "the manifest names an identity other than release-signing.cer"
release_version=$(manifest_string releaseVersion)
case $release_version in
    ''|*[!0-9.]*) refuse "the manifest release version '$release_version' is not valid" ;;
esac
echo "manifest      : verified, release $release_version for x64, signed by $thumbprint"

minimum=$(printf '%s' "$manifest" | sed -n "s/^{.*{\"id\":\"$os_id\",\"minimumVersion\":\"\\([0-9.]*\\)\"}.*/\\1/p")
[ -n "$minimum" ] || refuse "this package does not support $os_id"
version_at_least() {
    have_major=${1%%.*}
    have_minor=${1#"$have_major"}; have_minor=${have_minor#.}; have_minor=${have_minor%%.*}
    need_major=${2%%.*}
    need_minor=${2#"$need_major"}; need_minor=${need_minor#.}; need_minor=${need_minor%%.*}
    [ "$have_major" -gt "$need_major" ] ||
        { [ "$have_major" -eq "$need_major" ] && [ "${have_minor:-0}" -ge "${need_minor:-0}" ]; }
}
version_at_least "$os_version" "$minimum" || refuse "$os_id $os_version is older than the supported $os_id $minimum"

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
    cp "$package/$name" "$work/files/$name"
    chmod 0644 "$work/files/$name"
    [ "$(wc -c < "$work/files/$name" | tr -d ' ')" = "$bytes" ] ||
        refuse "the packaged file $name does not match its recorded size"
    [ "$(sha256sum "$work/files/$name" | cut -d ' ' -f 1)" = "$hash" ] ||
        refuse "the packaged file $name does not match its recorded hash"
    echo "$name" >> "$work/verify/listed"
    count=$((count + 1))
done < "$work/verify/entries"

is_listed() {
    grep -qxF -e "$1" "$work/verify/listed"
}
for file in "$package"/*.rpm "$package"/*.deb "$package"/*.sh; do
    [ -e "$file" ] || continue
    is_listed "${file##*/}" || refuse "the package holds the unexpected file ${file##*/}"
done
for name in install.sh uninstall.sh; do
    is_listed "$name" || refuse "the package does not hold $name"
done

if [ "$kind" = rpm ]; then
    main_pattern="phpvpn-$release_version-[0-9]*.x86_64.rpm"
    tray_pattern="phpvpn-tray-$release_version-[0-9]*.x86_64.rpm"
else
    main_pattern="phpvpn_$release_version-[0-9]*_amd64.deb"
    tray_pattern="phpvpn-tray_$release_version-[0-9]*_amd64.deb"
fi
main= tray= mains=0 trays=0
while IFS= read -r name; do
    case $name in
        $main_pattern) main=$name; mains=$((mains + 1)) ;;
        $tray_pattern) tray=$name; trays=$((trays + 1)) ;;
    esac
done < "$work/verify/listed"
[ "$mains" = 1 ] || refuse "the package must hold exactly one phpvpn $kind package for release $release_version"
[ "$trays" -le 1 ] || refuse "the package holds more than one phpvpn-tray $kind package"
echo "package files : $count verified"

if [ "$verify_only" = true ]; then
    echo "verification  : passed for $os_id $os_version ($main${tray:+, $tray}), nothing was installed"
    exit 0
fi

# ----------------------------------------------------------- 5. packages
[ -d /run/systemd/system ] || refuse "systemd is not running, phpvpn.service cannot be managed"
if [ "$kind" = rpm ]; then
    tray_installed() { rpm -q phpvpn-tray > /dev/null 2>&1; }
else
    tray_installed() { [ "$(dpkg-query -W -f='${Status}' phpvpn-tray 2> /dev/null)" = "install ok installed" ]; }
fi
tray_upgrade=false
if [ -z "$tray" ]; then
    [ "$headless" != no ] || refuse "--with-tray was given, but the folder holds no phpvpn-tray package"
    headless=yes
elif tray_installed; then
    # phpvpn-tray requires the exact phpvpn release: upgrading phpvpn alone
    # would fail, so an installed tray always follows.
    [ "$headless" != yes ] || tray_upgrade=true
    headless=no
elif [ "$headless" = auto ]; then
    # A graphical login needs the graphical default target and a display
    # manager; a container or server image often has the first without the other.
    if [ "$(systemctl get-default 2> /dev/null)" = graphical.target ] &&
       systemctl cat display-manager.service > /dev/null 2>&1; then
        headless=no
    else
        headless=yes
    fi
fi

cd "$work/files"
set -- "./$main"
if [ "$headless" = no ]; then set -- "$@" "./$tray"; fi
echo "installing    : $*"
if [ "$tray_upgrade" = true ]; then
    echo "note          : phpvpn-tray is installed and is upgraded together with phpvpn despite --headless"
fi
if [ "$kind" = rpm ]; then
    if [ "$weak_deps" = false ]; then
        dnf install -y --setopt=install_weak_deps=False "$@" || failed "dnf could not install the packages"
    else
        dnf install -y "$@" || failed "dnf could not install the packages"
    fi
else
    export DEBIAN_FRONTEND=noninteractive
    apt-get update || echo "warning: apt-get update failed, the existing package lists are used" >&2
    if [ "$weak_deps" = false ]; then
        apt-get install -y --no-install-recommends "$@" || failed "apt-get could not install the packages"
    else
        apt-get install -y "$@" || failed "apt-get could not install the packages"
    fi
fi
cd /

# ------------------------------------------------------------ 7. service
systemctl enable --now phpvpn.service || failed "phpvpn.service could not be enabled and started (journalctl -u phpvpn)"

waited=0
until [ -S "$SOCKET" ]; do
    if [ "$waited" -ge "$SOCKET_TIMEOUT" ]; then
        failed "the service did not create $SOCKET within $SOCKET_TIMEOUT seconds (journalctl -u phpvpn)"
    fi
    sleep 1
    waited=$((waited + 1))
done

# ------------------------------------------------------------- 9. status
phpvpn status || failed "phpvpn status did not answer (journalctl -u phpvpn)"

echo ""
echo "phpvpn $release_version installed successfully, phpvpn.service is enabled and running."
if [ "$headless" = no ]; then
    echo "The tray starts at the next graphical login; a tray that is already running keeps its version until then."
elif [ -n "$tray" ]; then
    echo "Headless installation: phpvpn-tray was not installed (sudo sh install.sh --with-tray adds it)."
fi
echo "Configure the connection with 'phpvpn set-config <file>' (or the tray Wizard) and connect with 'phpvpn connect'."
