#!/bin/sh
# Installation test of the macOS package on the running system (root only),
# the counterpart of tests/e2e/linux/package-install-container.sh. CI runs it
# on the throwaway macos runner right after tools/macos/package-pkg.sh:
#
#   sudo sh tests/e2e/macos/run-install-test.sh install   <package directory>
#   sudo sh tests/e2e/macos/run-install-test.sh uninstall <package directory>
#
# install: the .pkg of the directory is installed with the system installer
# (the dist install.sh needs the signed distribution folder, which the CI
# artifact is not), then the payload, the launchd daemon, the control socket,
# the CLI as root and as the console user, the service log and the tray launch
# agent are verified, and a Connect through the installed daemon against a
# private php relay reaches the connected state and is disconnected again.
# uninstall: the dist uninstall.sh --remove-data removes everything and the
# system is verified clean. Both modes print PASS/FAIL lines and exit 1 on any
# failure. Logs go to ${PHPVPN_E2E_WORK:-<repository>/work/e2e-macos}/install-<time>.
#
# Needs php and curl on PATH (sudo env "PATH=$PATH" ...) for the Connect check;
# PHPVPN_E2E_PORT (default 18095) selects the relay port.
set -u

mode=${1:-}
package_dir=${2:-}
case $mode in
    install|uninstall) ;;
    *) echo "usage: sudo sh run-install-test.sh install|uninstall <package directory>" >&2; exit 2 ;;
esac
[ -n "$package_dir" ] && [ -d "$package_dir" ] || { echo "run-install-test.sh: package directory missing" >&2; exit 2; }
[ "$(id -u)" = 0 ] || { echo "run-install-test.sh: root is required" >&2; exit 2; }
package_dir=$(cd "$package_dir" && pwd -P)
client=$(cd "$(dirname "$0")/../../.." && pwd -P)
repository=$(cd "$client/.." && pwd -P)
work_root=${PHPVPN_E2E_WORK:-$repository/work/e2e-macos}
work="$work_root/install-$(date +%Y%m%d-%H%M%S)-$$"
mkdir -p "$work"
port=${PHPVPN_E2E_PORT:-18095}
relay_url="http://127.0.0.1:$port/phpvpn.php"

DAEMON_LABEL=com.pilandrome.phpvpn
AGENT_LABEL=com.pilandrome.phpvpn-tray
SERVICE=/Library/PrivilegedHelperTools/phpvpn-service
CLI=/usr/local/bin/phpvpn
DAEMON_PLIST=/Library/LaunchDaemons/$DAEMON_LABEL.plist
AGENT_PLIST=/Library/LaunchAgents/$AGENT_LABEL.plist
TRAY_APP="/Applications/phpvpn Tray.app"
TRAY_BIN="$TRAY_APP/Contents/MacOS/phpvpn-tray"
CONFIG_DIR="/Library/Application Support/phpvpn"
STATE_DIR=/private/var/db/phpvpn
LOG_DIR=/private/var/log/phpvpn
RUNTIME_DIR=/private/var/run/phpvpn
SOCKET=$RUNTIME_DIR/control-v1.sock

passes=0
failures=0
pass() { passes=$((passes + 1)); echo "PASS: $*"; }
fail() { failures=$((failures + 1)); echo "FAIL: $*"; }
check() {
    label=$1
    shift
    if "$@" > /dev/null 2>&1; then pass "$label"; else fail "$label"; fi
}
mode_of() { stat -f '%Lp %u:%g' "$1" 2> /dev/null; }
check_mode() {
    actual=$(mode_of "$2")
    if [ "$actual" = "$1" ]; then pass "$2 is $1"; else fail "$2 is '$actual', expected $1"; fi
}
daemon_loaded() { launchctl print "system/$DAEMON_LABEL" > /dev/null 2>&1; }
console_uid=$(stat -f %u /dev/console 2> /dev/null || echo 0)
console_user=$(id -un "$console_uid" 2> /dev/null || echo root)

finish() {
    echo "RESULT: $passes passed, $failures failed (logs in $work)"
    [ "$failures" -eq 0 ]
}

# ===================================================================== install
if [ "$mode" = install ]; then
    set -- "$package_dir"/phpvpn-*-arm64.pkg
    [ $# -eq 1 ] && [ -f "$1" ] || { echo "run-install-test.sh: expected exactly one phpvpn-*-arm64.pkg in $package_dir" >&2; exit 2; }
    pkg=$1
    echo "package: $pkg"
    echo "console user: $console_user ($console_uid)"

    # ---- preconditions: a clean system
    check "the package is not installed yet" sh -c "! pkgutil --pkg-info $DAEMON_LABEL"
    check "the daemon is not loaded yet" sh -c "! launchctl print system/$DAEMON_LABEL"

    # ---- installer
    if installer -pkg "$pkg" -target / -verboseR > "$work/installer.log" 2>&1; then
        pass "installer -pkg completed"
    else
        fail "installer -pkg failed (see installer.log)"
        cat "$work/installer.log"
    fi
    grep -h "phpvpn postinstall\|phpvpn preinstall" /var/log/install.log > "$work/install-scripts.log" 2> /dev/null || true
    sed 's/^/  install.log: /' "$work/install-scripts.log" | tail -n 30

    # ---- payload
    check_mode "755 0:0" "$SERVICE"
    check_mode "755 0:0" "$CLI"
    check_mode "644 0:0" "$DAEMON_PLIST"
    check_mode "644 0:0" "$AGENT_PLIST"
    check "the tray bundle executable exists" test -x "$TRAY_BIN"
    check_mode "700 0:0" "$CONFIG_DIR"
    check_mode "700 0:0" "$STATE_DIR"
    check_mode "755 0:0" "$LOG_DIR"
    check "pkgutil knows the package" pkgutil --pkg-info "$DAEMON_LABEL"
    check "the service signature verifies" codesign --verify --strict "$SERVICE"
    check "the CLI signature verifies" codesign --verify --strict "$CLI"
    check "the tray bundle signature verifies" codesign --verify --strict "$TRAY_APP"
    check "the daemon plist is valid" plutil -lint "$DAEMON_PLIST"
    check "the agent plist is valid" plutil -lint "$AGENT_PLIST"

    # ---- daemon
    check "launchctl knows the daemon" daemon_loaded
    launchctl print "system/$DAEMON_LABEL" > "$work/launchctl-daemon.txt" 2>&1 || true
    check "the daemon is running" grep -q "state = running" "$work/launchctl-daemon.txt"
    attempt=0
    while [ ! -S "$SOCKET" ] && [ "$attempt" -lt 60 ]; do attempt=$((attempt + 1)); sleep 0.5; done
    check "the control socket $SOCKET exists" test -S "$SOCKET"
    check_mode "666 0:0" "$SOCKET"
    check_mode "755 0:0" "$RUNTIME_DIR"
    check "phpvpn status answers as root" "$CLI" status
    "$CLI" status > "$work/status-root.json" 2>&1 || true
    check "the status is disconnected" grep -q '"state": *"disconnected"' "$work/status-root.json"
    if [ "$console_uid" != 0 ]; then
        check "phpvpn status answers as the console user $console_user" sudo -u "$console_user" "$CLI" status
    fi
    check "the service log exists" test -f "$LOG_DIR/phpvpn-service.log"
    check "the service logged its IPC start" grep -q "the service control IPC is ready" "$LOG_DIR/phpvpn-service.log"
    check "launchd.log exists" test -f "$LOG_DIR/launchd.log"

    # ---- tray launch agent (registered by postinstall in the console session)
    if [ "$console_uid" != 0 ]; then
        launchctl print "gui/$console_uid/$AGENT_LABEL" > "$work/launchctl-agent.txt" 2>&1 && \
            pass "the tray launch agent is registered for $console_user" || \
            fail "the tray launch agent is not registered for $console_user (see launchctl-agent.txt)"
        attempt=0
        while ! pgrep -x phpvpn-tray > /dev/null 2>&1 && [ "$attempt" -lt 20 ]; do attempt=$((attempt + 1)); sleep 0.5; done
        check "the tray process started from the launch agent" pgrep -x phpvpn-tray
    else
        echo "no console user, the tray launch agent is not checked"
    fi

    # ---- connect through the installed daemon against a private relay
    php=${PHPVPN_PHP:-php}
    if command -v "$php" > /dev/null 2>&1 && command -v curl > /dev/null 2>&1; then
        mkdir -p "$work/relay/www" "$work/relay/buffer"
        sed "s|^const BUFFER_DIRECTORY = './phpvpn_data';|const BUFFER_DIRECTORY = '$work/relay/buffer';|" \
            "$repository/server/php/phpvpn.php" > "$work/relay/www/phpvpn.php"
        PHP_CLI_SERVER_WORKERS=8 "$php" -S "127.0.0.1:$port" -t "$work/relay/www" > "$work/relay/relay.log" 2>&1 &
        relay_pid=$!
        attempt=0
        until [ "$(curl -s -o /dev/null -w '%{http_code}' --max-time 2 "$relay_url" || true)" = 200 ]; do
            attempt=$((attempt + 1))
            if [ "$attempt" -ge 60 ] || ! kill -0 "$relay_pid" 2> /dev/null; then break; fi
            sleep 0.5
        done
        check "the php relay answers at $relay_url" sh -c "curl -s -o /dev/null --max-time 2 '$relay_url'"
        cat > "$work/config.json" <<EOF
{
  "masterSetup": {
    "serverSecret": "CHANGE_THIS_SERVER_SECRET",
    "serverUrl": "$relay_url",
    "httpHeaders": "Cache-Control: no-cache\r\nPragma: no-cache\r\nUser-Agent: phpvpn-macos-install-test",
    "trustAnyTlsCertificate": true,
    "useDefaultServerSecretInWizard": true,
    "vpnCidr": "10.99.99.0/24",
    "virtualIp": "10.99.99.2",
    "autoDetectIpAddressInWizard": false,
    "accessKey": "0202020202020202020202020202020202020202020202020202020202020202",
    "useDefaultAccessKeyInWizard": true,
    "autoConnectOnStartup": false
  },
  "communicationContracts": {
    "10.99.99.3": {
      "payloadKey": "918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d"
    }
  }
}
EOF
        check "set-config through the installed daemon" "$CLI" set-config "$work/config.json"
        check_mode "600 0:0" "$CONFIG_DIR/config.json"
        check "connect through the installed daemon" "$CLI" connect
        state=
        attempt=0
        while [ "$attempt" -lt 60 ]; do
            state=$("$CLI" status 2> /dev/null | "$php" -r '
                $d = json_decode(stream_get_contents(STDIN), true);
                echo isset($d["status"]["state"]) ? $d["status"]["state"] . " " . $d["status"]["tunnelReady"] : "unreachable";' 2> /dev/null || echo unreachable)
            case $state in "connected yes") break ;; esac
            attempt=$((attempt + 1))
            sleep 0.5
        done
        if [ "$state" = "connected yes" ]; then
            pass "the installed daemon reached connected with the tunnel ready"
        else
            fail "the installed daemon did not reach connected (state: $state)"
            tail -n 40 "$LOG_DIR/phpvpn-service.log" | sed 's/^/  service log: /'
        fi
        check "a utun interface carries 10.99.99.2" sh -c "ifconfig | grep -q 'inet 10.99.99.2 '"
        check "disconnect through the installed daemon" "$CLI" disconnect
        attempt=0
        while ifconfig | grep -q 'inet 10.99.99.2 ' && [ "$attempt" -lt 60 ]; do attempt=$((attempt + 1)); sleep 0.5; done
        check "the address 10.99.99.2 is gone after disconnect" sh -c "! ifconfig | grep -q 'inet 10.99.99.2 '"
        kill "$relay_pid" 2> /dev/null || true
        wait "$relay_pid" 2> /dev/null || true
    else
        echo "php or curl missing on PATH, the Connect check through the installed daemon is skipped"
    fi
    cp "$LOG_DIR/phpvpn-service.log" "$work/phpvpn-service.log" 2> /dev/null || true
    finish
    exit $?
fi

# =================================================================== uninstall
check "uninstall.sh is in the package directory" test -f "$package_dir/uninstall.sh"
if sh "$package_dir/uninstall.sh" --remove-data > "$work/uninstall.log" 2>&1; then
    pass "uninstall.sh --remove-data completed"
else
    fail "uninstall.sh --remove-data failed (see uninstall.log)"
fi
sed 's/^/  uninstall: /' "$work/uninstall.log" | tail -n 20
check "the daemon is no longer loaded" sh -c "! launchctl print system/$DAEMON_LABEL"
check "the daemon process is gone" sh -c "! pgrep -x phpvpn-service"
check "pkgutil forgot the package" sh -c "! pkgutil --pkg-info $DAEMON_LABEL"
for path in "$SERVICE" "$CLI" "$DAEMON_PLIST" "$AGENT_PLIST" "$TRAY_APP" "$CONFIG_DIR" "$STATE_DIR" "$LOG_DIR" "$RUNTIME_DIR"; do
    check "$path is removed" test ! -e "$path"
done
check "no utun interface carries 10.99.99.2" sh -c "! ifconfig | grep -q 'inet 10.99.99.2 '"
finish
