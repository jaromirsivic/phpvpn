#!/bin/sh
# Removes the phpvpn client for Linux (plan-v001 section 4.13).
#
#   sudo sh uninstall.sh [--remove-data]
#
#   1. phpvpn disconnect (best effort, the service tears the tunnel down),
#   2. systemctl disable --now phpvpn.service,
#   3. removal of phpvpn-tray and phpvpn with dnf or apt-get,
#   4. with --remove-data only: removal of the configuration /etc/phpvpn and the
#      state /var/lib/phpvpn, the counterpart of the Windows -RemoveData.
#
# Without --remove-data the configuration stays, so a later installation
# connects with the same settings.
set -eu

usage() {
    echo "usage: sudo sh uninstall.sh [--remove-data]"
}
fail() {
    printf 'uninstallation failed: %s\n' "$*" >&2
    exit 1
}

remove_data=false
for argument in "$@"; do
    case $argument in
        --remove-data) remove_data=true ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done

if [ "$(id -u)" != 0 ]; then
    if command -v sudo > /dev/null 2>&1; then
        exec sudo sh "$(cd "$(dirname "$0")" && pwd -P)/uninstall.sh" "$@"
    fi
    fail "run the uninstaller as root: sudo sh uninstall.sh"
fi

[ -r /etc/os-release ] || fail "/etc/os-release is missing, the distribution is unknown"
os_id=$(sed -n 's/^ID=//p' /etc/os-release | head -n 1 | tr -d "\"'")
case $os_id in
    fedora|ubuntu) ;;
    *) fail "the distribution '${os_id:-unknown}' is not supported (supported: Fedora, Ubuntu)" ;;
esac

# ------------------------------------------------------- 1. disconnect
if command -v phpvpn > /dev/null 2>&1; then
    if phpvpn disconnect > /dev/null 2>&1; then
        echo "disconnect    : requested"
    else
        echo "disconnect    : skipped (the service did not answer)"
    fi
fi

# ---------------------------------------------------------- 2. service
if [ -d /run/systemd/system ] && systemctl cat phpvpn.service > /dev/null 2>&1; then
    systemctl disable --now phpvpn.service || echo "warning: phpvpn.service could not be stopped and disabled" >&2
fi

# --------------------------------------------------------- 3. packages
installed=
if [ "$os_id" = fedora ]; then
    for name in phpvpn-tray phpvpn; do
        if rpm -q "$name" > /dev/null 2>&1; then installed="$installed $name"; fi
    done
    if [ -n "$installed" ]; then
        dnf remove -y $installed || fail "dnf could not remove$installed"
    fi
else
    for name in phpvpn-tray phpvpn; do
        if [ "$(dpkg-query -W -f='${Status}' "$name" 2> /dev/null)" = "install ok installed" ]; then
            installed="$installed $name"
        fi
    done
    if [ -n "$installed" ]; then
        export DEBIAN_FRONTEND=noninteractive
        if [ "$remove_data" = true ]; then
            apt-get purge -y $installed || fail "apt-get could not purge$installed"
        else
            apt-get remove -y $installed || fail "apt-get could not remove$installed"
        fi
    fi
fi
if [ -n "$installed" ]; then
    echo "packages      : removed$installed"
else
    echo "packages      : phpvpn is not installed"
fi

# ------------------------------------------------------------- 4. data
if [ "$remove_data" = true ]; then
    rm -rf /etc/phpvpn /var/lib/phpvpn
    echo "data          : /etc/phpvpn and /var/lib/phpvpn removed"
else
    echo "data          : kept in /etc/phpvpn and /var/lib/phpvpn (remove with --remove-data)"
fi
echo "phpvpn uninstalled successfully."
