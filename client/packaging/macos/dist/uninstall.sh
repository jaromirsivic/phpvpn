#!/bin/sh
# Removes the phpvpn client for macOS (plan-macos-v001 step 8), the
# counterpart of the Linux uninstall.sh.
#
#   sudo sh uninstall.sh [--remove-data]
#
#   1. phpvpn disconnect (best effort, the service tears the tunnel down),
#   2. launchctl bootout of the tray agent in every session that runs the
#      tray (at least the console user) and of the daemon
#      system/com.pilandrome.phpvpn,
#   3. removal of the files recorded in the package receipt
#      (pkgutil --files com.pilandrome.phpvpn) and of the tray application
#      folder, then pkgutil --forget com.pilandrome.phpvpn,
#   4. removal of the runtime directory /private/var/run/phpvpn,
#   5. with --remove-data only: removal of the configuration
#      /Library/Application Support/phpvpn, the state /private/var/db/phpvpn and
#      the logs /private/var/log/phpvpn, the counterpart of the Windows -RemoveData.
#
# Without --remove-data the configuration stays, so a later installation
# connects with the same settings.
set -eu

PACKAGE_ID=com.pilandrome.phpvpn
DAEMON_LABEL=com.pilandrome.phpvpn
AGENT_LABEL=com.pilandrome.phpvpn-tray
CLI=/usr/local/bin/phpvpn
TRAY_APP="/Applications/phpvpn Tray.app"
CONFIG_DIR="/Library/Application Support/phpvpn"
STATE_DIR=/private/var/db/phpvpn
LOG_DIR=/private/var/log/phpvpn
RUNTIME_DIR=/private/var/run/phpvpn

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
[ "$(uname -s)" = Darwin ] || fail "this uninstaller is for macOS, the system is $(uname -s)"

# ------------------------------------------------------- 1. disconnect
if [ -x "$CLI" ]; then
    if "$CLI" disconnect > /dev/null 2>&1; then
        echo "disconnect    : requested"
    else
        echo "disconnect    : skipped (the service did not answer)"
    fi
fi

# ------------------------------------------------- 2. tray and service
# The console user, and every user whose session runs the tray (fast user
# switching).
uids=$(stat -f %u /dev/console 2> /dev/null || true)
uids="$uids $(ps -axo uid=,comm= | awk '$NF == "phpvpn-tray" || $NF ~ /\/phpvpn-tray$/ { print $1 }' | sort -u | tr '\n' ' ')"
stopped=
for uid in $uids; do
    case $uid in
        0|*[!0-9]*) continue ;;
    esac
    case " $stopped " in
        *" $uid "*) continue ;;
    esac
    stopped="$stopped $uid"
    if launchctl print "gui/$uid/$AGENT_LABEL" > /dev/null 2>&1; then
        launchctl bootout "gui/$uid/$AGENT_LABEL" > /dev/null 2>&1 ||
            echo "warning: the tray agent of uid $uid could not be stopped" >&2
    fi
done
if launchctl print "system/$DAEMON_LABEL" > /dev/null 2>&1; then
    launchctl bootout "system/$DAEMON_LABEL" ||
        echo "warning: launchctl bootout system/$DAEMON_LABEL failed" >&2
    echo "service       : stopped"
else
    echo "service       : not running"
fi

# ----------------------------------------------------------- 3. files
# Only the documented install layout is removed, whatever the receipt says;
# directories shared with the system (Library, usr/local/bin) stay.
known="Library/PrivilegedHelperTools/phpvpn-service
usr/local/bin/phpvpn
Library/LaunchDaemons/$DAEMON_LABEL.plist
Library/LaunchAgents/$AGENT_LABEL.plist"
removed=0
remove_file() {
    if [ -f "/$1" ] || [ -L "/$1" ]; then
        rm -f "/$1"
        removed=$((removed + 1))
    fi
}
receipt=false
if pkgutil --pkg-info "$PACKAGE_ID" > /dev/null 2>&1; then
    receipt=true
    list=$(mktemp "/private/tmp/phpvpn-uninstall.XXXXXX") || fail "no temporary file"
    trap 'rm -f "$list"' EXIT
    pkgutil --only-files --files "$PACKAGE_ID" > "$list" 2> /dev/null || true
    while IFS= read -r path; do
        case $path in
            "Applications/phpvpn Tray.app/"*) ;; # removed with the folder below
            Library/PrivilegedHelperTools/phpvpn-service|usr/local/bin/phpvpn) remove_file "$path" ;;
            Library/LaunchDaemons/$DAEMON_LABEL.plist|Library/LaunchAgents/$AGENT_LABEL.plist) remove_file "$path" ;;
            '') ;;
            *) echo "warning: the receipt lists /$path outside the phpvpn layout, it is kept" >&2 ;;
        esac
    done < "$list"
fi
# Also without a receipt (an interrupted earlier uninstallation).
while IFS= read -r path; do
    remove_file "$path"
done <<KNOWN
$known
KNOWN
if [ -d "$TRAY_APP" ]; then
    rm -rf "$TRAY_APP"
    removed=$((removed + 1))
fi
if [ "$receipt" = true ]; then
    pkgutil --forget "$PACKAGE_ID" > /dev/null || echo "warning: pkgutil --forget $PACKAGE_ID failed" >&2
    echo "files         : removed, package receipt $PACKAGE_ID forgotten"
elif [ "$removed" -gt 0 ]; then
    echo "files         : removed (no package receipt was present)"
else
    echo "files         : phpvpn is not installed"
fi

# --------------------------------------------------------- 4. runtime
rm -rf "$RUNTIME_DIR"

# ------------------------------------------------------------- 5. data
if [ "$remove_data" = true ]; then
    rm -rf "$CONFIG_DIR" "$STATE_DIR" "$LOG_DIR"
    echo "data          : $CONFIG_DIR, $STATE_DIR and $LOG_DIR removed"
else
    echo "data          : kept in $CONFIG_DIR, $STATE_DIR and $LOG_DIR (remove with --remove-data)"
fi
echo "phpvpn uninstalled successfully."
