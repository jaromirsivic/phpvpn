#!/bin/sh
# Headless installation check of the signed Linux client folder in a clean
# systemd container (plan-v001 5/WP08). Runs on a Linux host with podman.
#
#   sh tests/e2e/linux/package-install-container.sh fedora:<N>|ubuntu:24.04 <folder-release-1> <folder-release-2>
#
# Both folders are outputs of client/tools/linux/make-linux-dist.ps1 (the same
# version, package releases 1 and 2). The script builds a local image with
# systemd and openssl only, starts it rootless with --systemd=always and
# --privileged (see below), and checks: --verify-only, refusal of a tampered file, of a
# tampered manifest and of an unlisted script, installation with weak or
# recommended dependencies off, the active service, phpvpn status as root and
# as an unprivileged user, the upgrade to release 2 with a service restart and
# the configuration kept, uninstall.sh keeping the data and
# uninstall.sh --remove-data removing it. The container is removed afterwards.
set -eu
image=${1:?usage: package-install-container.sh fedora:<N>|ubuntu:24.04 <folder-release-1> <folder-release-2>}
first=$(cd "${2:?folder of release 1}" && pwd)
second=$(cd "${3:?folder of release 2}" && pwd)
engine=${PHPVPN_CONTAINER_ENGINE:-podman}

case $image in
    fedora:[0-9]*)
        base=registry.fedoraproject.org/$image
        prepare='dnf -y install systemd openssl util-linux && dnf clean all'
        package_query='rpm -q --qf %{VERSION}-%{RELEASE} phpvpn'
        package_installed='rpm -q phpvpn > /dev/null 2>&1'
        ;;
    ubuntu:24.04)
        # The container image carries a policy-rc.d that forbids service
        # (re)starts from maintainer scripts; a real Ubuntu system has none.
        base=docker.io/library/$image
        prepare='apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends systemd systemd-sysv openssl ca-certificates && rm -rf /var/lib/apt/lists/* /usr/sbin/policy-rc.d'
        package_query="dpkg-query -W -f='\${Version}' phpvpn"
        package_installed="dpkg-query -W -f='\${Status}' phpvpn 2> /dev/null | grep -q 'install ok installed'"
        ;;
    *) echo "image must be fedora:<N> or ubuntu:24.04" >&2; exit 2 ;;
esac
tag=localhost/phpvpn-install-check:$(printf '%s' "$image" | tr ':.' '--')
name=phpvpn-install-check-$(printf '%s' "$image" | tr ':.' '--')

passes=0
failures=0
pass() { passes=$((passes + 1)); echo "PASS: $*"; }
fail() { failures=$((failures + 1)); echo "FAIL: $*"; }
run() { "$engine" exec "$name" sh -c "$1"; }
run_input() { "$engine" exec -i "$name" sh -c "$1"; }
check() {
    message=$1
    shift
    if run "$1"; then pass "$message"; else fail "$message"; fi
}
refused() {
    message=$1
    shift
    if output=$(run "$1" 2>&1); then
        fail "$message (accepted)"
    elif printf '%s' "$output" | grep -q "$2"; then
        pass "$message: $(printf '%s' "$output" | tail -n 1)"
    else
        fail "$message (unexpected output: $output)"
    fi
}

printf 'FROM %s\nRUN %s\nCMD ["/sbin/init"]\n' "$base" "$prepare" |
    "$engine" build -q -t "$tag" -f - . > /dev/null
"$engine" rm -f "$name" > /dev/null 2>&1 || true
# The unit's sandbox (PrivateTmp, ProtectSystem, ProtectKernelTunables, ...)
# makes systemd build a mount namespace with read-only /proc bind mounts and
# keep CAP_SYS_ADMIN for it. The default container capabilities, masked /proc
# paths and container SELinux label forbid that (217/USER, 226/NAMESPACE), so
# the container runs --privileged. Under rootless podman that stays inside the
# user namespace of the invoking user; NET_ADMIN and /dev/net/tun are included.
"$engine" run -d --name "$name" --systemd=always --privileged --cap-add NET_ADMIN --device /dev/net/tun "$tag" > /dev/null
trap '"$engine" rm -f "$name" > /dev/null 2>&1 || true' EXIT
for _ in $(seq 1 60); do
    state=$(run 'systemctl is-system-running 2> /dev/null' || true)
    case $state in running|degraded) break ;; esac
    sleep 1
done
echo "container $name ($image): systemd $state"

"$engine" cp "$first" "$name:/root/release-1"
"$engine" cp "$second" "$name:/root/release-2"
run 'useradd -m phpvpn-check'

# ------------------------------------------------------------- verification
check "verify-only of the signed folder" 'sh /root/release-1/install.sh --verify-only'
check "verify-only as an unprivileged user" 'cp -r /root/release-1 /tmp/user-copy && chmod -R a+rX /tmp/user-copy && runuser -u phpvpn-check -- sh /tmp/user-copy/install.sh --verify-only'
refused "tampered file is refused" \
    'cp -r /root/release-1 /root/tampered-file && printf x >> /root/tampered-file/README.md && sh /root/tampered-file/install.sh --verify-only' \
    'does not match its recorded'
refused "tampered package is refused" \
    'cp -r /root/release-1 /root/tampered-package && for p in /root/tampered-package/phpvpn*[._]*[md][pe][mb]; do printf x >> "$p"; done && sh /root/tampered-package/install.sh --verify-only' \
    'does not match its recorded'
refused "tampered manifest is refused" \
    'cp -r /root/release-1 /root/tampered-manifest && sed -i "s/\"ipcVersion\":1/\"ipcVersion\":2/" /root/tampered-manifest/release-manifest.json && sh /root/tampered-manifest/install.sh --verify-only' \
    'signature did not verify'
refused "unlisted script is refused" \
    'cp -r /root/release-1 /root/tampered-extra && printf "#!/bin/sh\n" > /root/tampered-extra/extra.sh && sh /root/tampered-extra/install.sh --verify-only' \
    'unexpected file extra.sh'
refused "installation without the signing certificate is refused" \
    'cp -r /root/release-1 /root/tampered-cert && rm /root/tampered-cert/release-signing.cer && sh /root/tampered-cert/install.sh --verify-only' \
    'release-signing.cer is missing'

# ------------------------------------------------------------- installation
check "install.sh --headless --no-weak-deps (release 1)" 'sh /root/release-1/install.sh --headless --no-weak-deps'
check "phpvpn.service is active" 'systemctl is-active --quiet phpvpn.service'
check "phpvpn.service is enabled" 'systemctl is-enabled --quiet phpvpn.service'
run 'systemctl status phpvpn.service --no-pager' || true
check "phpvpn status as root reports disconnected" 'phpvpn status | grep -q "\"state\": \"disconnected\""'
check "phpvpn status as an unprivileged user" 'runuser -u phpvpn-check -- phpvpn status > /dev/null'
check "control socket is 0666" 'test "$(stat -c %a /run/phpvpn/control-v1.sock)" = 666'
check "configuration directory is 0700 root" 'test "$(stat -c %a:%U /etc/phpvpn)" = 700:root'
check "installed release 1" "$package_query | grep -q -- '-1\$'"
check "phpvpn-service --verify-tunnel finds the kernel TUN device" '/usr/libexec/phpvpn/phpvpn-service --verify-tunnel'
run 'systemd-analyze security phpvpn.service --no-pager | tail -n 1' || true
check "no graphical library in the installed binaries" '! ldd /usr/bin/phpvpn /usr/libexec/phpvpn/phpvpn-service | grep -Eiq "gtk|gdk|libX|wayland|ayatana"'

run_input 'cat > /tmp/config.json' <<'EOF'
{
  "masterSetup": {
    "serverSecret": "CHANGE_THIS_SERVER_SECRET",
    "serverUrl": "http://127.0.0.1:18093/phpvpn.php",
    "httpHeaders": "User-Agent: phpvpn-install-check",
    "trustAnyTlsCertificate": false,
    "vpnCidr": "10.99.99.0/24",
    "virtualIp": "10.99.99.7",
    "accessKey": "0707070707070707070707070707070707070707070707070707070707070707",
    "autoConnectOnStartup": false
  },
  "communicationContracts": {}
}
EOF
check "phpvpn set-config stores a configuration" 'chmod 0644 /tmp/config.json && runuser -u phpvpn-check -- phpvpn set-config /tmp/config.json | grep -q "\"result\": \"ok\""'
config_before=$(run 'sha256sum /etc/phpvpn/config.json' || true)
invocation_before=$(run 'systemctl show -p InvocationID --value phpvpn.service')

# ----------------------------------------------------------------- upgrade
check "install.sh --headless --no-weak-deps (upgrade to release 2)" 'sh /root/release-2/install.sh --headless --no-weak-deps'
check "installed release 2" "$package_query | grep -q -- '-2\$'"
invocation_after=$(run 'systemctl show -p InvocationID --value phpvpn.service')
if [ -n "$invocation_before" ] && [ "$invocation_before" != "$invocation_after" ]; then
    pass "the upgrade restarted phpvpn.service"
else
    fail "the upgrade did not restart phpvpn.service ($invocation_before / $invocation_after)"
fi
check "phpvpn.service is active after the upgrade" 'systemctl is-active --quiet phpvpn.service'
config_after=$(run 'sha256sum /etc/phpvpn/config.json' || true)
if [ -n "$config_before" ] && [ "$config_before" = "$config_after" ]; then
    pass "the upgrade kept /etc/phpvpn/config.json"
else
    fail "the upgrade changed /etc/phpvpn/config.json"
fi
check "phpvpn status after the upgrade" 'phpvpn status > /dev/null'

# ----------------------------------------------------------- uninstallation
check "uninstall.sh" 'sh /root/release-2/uninstall.sh'
check "the package is removed" "! { $package_installed; }"
check "the service unit is gone" '! systemctl cat phpvpn.service > /dev/null 2>&1'
check "uninstall.sh keeps /etc/phpvpn/config.json" 'test -f /etc/phpvpn/config.json'
check "uninstall.sh keeps /var/lib/phpvpn" 'test -d /var/lib/phpvpn'
check "reinstallation (release 2)" 'sh /root/release-2/install.sh --headless --no-weak-deps'
check "the reinstalled service keeps the configuration" 'phpvpn get-config | grep -q "phpvpn-install-check"'
check "uninstall.sh --remove-data" 'sh /root/release-2/uninstall.sh --remove-data'
check "uninstall.sh --remove-data removes /etc/phpvpn and /var/lib/phpvpn" 'test ! -e /etc/phpvpn && test ! -e /var/lib/phpvpn'
check "no phpvpn interface is left" '! ip link show phpvpn > /dev/null 2>&1 || ! command -v ip > /dev/null'

echo "passes=$passes failures=$failures"
[ "$failures" = 0 ]
