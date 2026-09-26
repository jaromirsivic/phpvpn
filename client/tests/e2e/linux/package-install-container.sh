#!/bin/sh
# Installation check of the signed Linux client folder in a clean systemd
# container without a desktop (plan-v001 5/WP08 and 5/WP11). Runs on a Linux
# host with podman.
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
# uninstall.sh --remove-data removing it. When the folders hold phpvpn-tray, a
# second phase checks the tray package on the same desktop-less container: the
# automatic mode skips it, --with-tray installs it with its files, resolvable
# libraries and GUI dependencies (the base package keeps none), the tray exits
# with a clear message without a display, --headless still upgrades an
# installed tray to the exact release, and uninstall.sh removes both packages.
# The container is removed afterwards.
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
        tray_query='rpm -q --qf %{VERSION}-%{RELEASE} phpvpn-tray'
        tray_installed='rpm -q phpvpn-tray > /dev/null 2>&1'
        tray_files='rpm -ql phpvpn-tray'
        tray_requires='rpm -q --requires phpvpn-tray'
        base_requires='rpm -q --requires phpvpn'
        tray_licence='grep -q "Apache License" /usr/share/licenses/phpvpn-tray/LICENSE-material-symbols.txt && test -f /usr/share/licenses/phpvpn-tray/NOTICE-material-symbols.txt'
        tray_gui_dependencies='libgtk-3.so.0 libayatana-appindicator3.so.1'
        ;;
    ubuntu:24.04)
        # The container image carries a policy-rc.d that forbids service
        # (re)starts from maintainer scripts; a real Ubuntu system has none.
        base=docker.io/library/$image
        prepare='apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends systemd systemd-sysv openssl ca-certificates && rm -rf /var/lib/apt/lists/* /usr/sbin/policy-rc.d'
        package_query="dpkg-query -W -f='\${Version}' phpvpn"
        package_installed="dpkg-query -W -f='\${Status}' phpvpn 2> /dev/null | grep -q 'install ok installed'"
        tray_query="dpkg-query -W -f='\${Version}' phpvpn-tray"
        tray_installed="dpkg-query -W -f='\${Status}' phpvpn-tray 2> /dev/null | grep -q 'install ok installed'"
        tray_files='dpkg -L phpvpn-tray'
        tray_requires="dpkg-query -W -f='\${Depends}\n' phpvpn-tray | tr ',' '\n'"
        base_requires="dpkg-query -W -f='\${Depends}\n' phpvpn | tr ',' '\n'"
        tray_licence='grep -q "^License: Apache-2.0" /usr/share/doc/phpvpn-tray/copyright'
        tray_gui_dependencies='libgtk-3-0 libayatana-appindicator3-1'
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
check "--headless did not install phpvpn-tray" "! { $tray_installed; }"

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

# --------------------------------------------------------------------- tray
if ls "$first"/phpvpn-tray[-_]* > /dev/null 2>&1 && ls "$second"/phpvpn-tray[-_]* > /dev/null 2>&1; then
    refused "--headless together with --with-tray is refused" \
        'sh /root/release-1/install.sh --headless --with-tray' 'usage:'
    check "install.sh without options skips phpvpn-tray without a graphical login" \
        "sh /root/release-1/install.sh --no-weak-deps > /tmp/auto.log 2>&1; status=\$?; cat /tmp/auto.log; test \$status = 0 && grep -q 'phpvpn-tray was not installed' /tmp/auto.log && ! { $tray_installed; }"
    check "install.sh --with-tray --no-weak-deps (release 1)" 'sh /root/release-1/install.sh --with-tray --no-weak-deps'
    check "installed phpvpn-tray release 1" "$tray_query | grep -q -- '-1\$'"
    check "phpvpn-tray binary" 'test -x /usr/bin/phpvpn-tray'
    check "autostart entry /etc/xdg/autostart/phpvpn-tray.desktop" \
        'grep -qx "Exec=phpvpn-tray" /etc/xdg/autostart/phpvpn-tray.desktop'
    check "application launcher runs phpvpn-tray --show-window" \
        'grep -qx "Exec=phpvpn-tray --show-window" /usr/share/applications/phpvpn-tray.desktop'
    check "28 hicolor icons (4 names x 7 sizes)" \
        'test "$(ls /usr/share/icons/hicolor/*/apps/phpvpn.png /usr/share/icons/hicolor/*/apps/phpvpn-*.png | wc -l)" = 28'
    check "phpvpn-tray(1) is part of the package" "$tray_files | grep -q '/man1/phpvpn-tray\.1'"
    check "Material Symbols licence and notice" "$tray_licence"
    check "ldd resolves every library of phpvpn-tray" \
        'ldd /usr/bin/phpvpn-tray > /tmp/ldd.txt && ! grep -q "not found" /tmp/ldd.txt && grep -q libgtk-3 /tmp/ldd.txt && grep -q libayatana-appindicator3 /tmp/ldd.txt'
    for dependency in $tray_gui_dependencies; do
        check "phpvpn-tray depends on $dependency" "$tray_requires | grep -qF '$dependency'"
    done
    check "phpvpn-tray requires the exact phpvpn release" "$tray_requires | grep -Eq '^ *phpvpn(\(x86-64\))? *[(]?= *1\.0\.0-1\)?$'"
    check "the phpvpn package still has no graphical dependency" "! $base_requires | grep -Eiq 'gtk|gdk|ayatana|libX|wayland'"
    check "phpvpn and phpvpn-service still load no graphical library" '! ldd /usr/bin/phpvpn /usr/libexec/phpvpn/phpvpn-service | grep -Eiq "gtk|gdk|libX|wayland|ayatana"'
    for arguments in "--help" "" "--show-window"; do
        if output=$(run "runuser -u phpvpn-check -- env -u DISPLAY -u WAYLAND_DISPLAY phpvpn-tray $arguments 2>&1"); then
            fail "phpvpn-tray $arguments without a display succeeded: $output"
        else
            status=$?
            if [ "$status" = 1 ] && printf '%s' "$output" | grep -q 'cannot open a graphical display'; then
                pass "phpvpn-tray $arguments without a display exits 1: $(printf '%s' "$output" | head -n 1)"
            else
                fail "phpvpn-tray $arguments without a display: status $status, output: $output"
            fi
        fi
    done
    check "install.sh --headless --no-weak-deps upgrades the installed tray too (release 2)" \
        "sh /root/release-2/install.sh --headless --no-weak-deps > /tmp/upgrade.log 2>&1; status=\$?; cat /tmp/upgrade.log; test \$status = 0 && grep -q 'upgraded together with phpvpn' /tmp/upgrade.log"
    check "installed phpvpn release 2" "$package_query | grep -q -- '-2\$'"
    check "installed phpvpn-tray release 2" "$tray_query | grep -q -- '-2\$'"
    check "phpvpn.service is active with the tray installed" 'systemctl is-active --quiet phpvpn.service && phpvpn status > /dev/null'
    check "uninstall.sh --remove-data removes phpvpn and phpvpn-tray" 'sh /root/release-2/uninstall.sh --remove-data'
    check "phpvpn-tray and phpvpn are removed" "! { $tray_installed; } && ! { $package_installed; }"
    check "tray files are removed" \
        'test ! -e /usr/bin/phpvpn-tray && test ! -e /etc/xdg/autostart/phpvpn-tray.desktop && test ! -e /usr/share/applications/phpvpn-tray.desktop && ! ls /usr/share/icons/hicolor/*/apps/phpvpn*.png > /dev/null 2>&1'
else
    echo "NOT RUN: tray phase (the folders hold no phpvpn-tray package)"
fi

echo "passes=$passes failures=$failures"
[ "$failures" = 0 ]
