#!/bin/sh
# Builds and runs the real C++ contract tests (libcurl transport, OpenSSL
# crypto) against a private local relay with the wizard-default profile
# (CHANGE_THIS_SERVER_SECRET and the default .2-.15 access keys), the Linux
# counterpart of tools/run-server-contract.ps1.
#
#   sh tools/linux/run-server-contract.sh [php|python]
#
# php (default) serves a fresh copy of phpvpn.php with the PHP built-in server,
# exactly like the Windows fixture. python runs a fresh copy of phpvpn.py with
# memory storage; it needs the locked server/python/requirements-server.txt environment, given
# by PHPVPN_SERVER_PYTHON (default: python3). The relay listens on
# 127.0.0.1:${PHPVPN_CONTRACT_PORT:-18093}, lives in a temporary directory under
# work/ and is stopped and removed afterwards. An occupied port, an unavailable
# relay or a failed test is a failure, never a skip.
#
# PHPVPN_BUILD_DIR overrides the build directory (default
# work/build/linux-contract), PHPVPN_CONTRACT_PRESET the configure preset
# (default linux-debug).
set -eu
relay=${1:-php}
port=${PHPVPN_CONTRACT_PORT:-18093}
preset=${PHPVPN_CONTRACT_PRESET:-linux-debug}
client=$(cd "$(dirname "$0")/../.." && pwd)
repository=$(cd "$client/.." && pwd)
build=${PHPVPN_BUILD_DIR:-$repository/work/build/linux-contract}
url="http://127.0.0.1:$port/phpvpn.php"

case $relay in
    php|python) ;;
    *) echo "run-server-contract.sh: relay must be php or python" >&2; exit 2 ;;
esac
if curl -s -o /dev/null --max-time 2 "http://127.0.0.1:$port/"; then
    echo "run-server-contract.sh: 127.0.0.1:$port is already in use" >&2
    exit 1
fi

mkdir -p "$repository/work"
fixture=$(mktemp -d "$repository/work/contract-relay.XXXXXX")
server_pid=
cleanup() {
    if [ -n "$server_pid" ]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -rf "$fixture"
}
trap cleanup EXIT INT TERM

mkdir "$fixture/buffer" "$fixture/www"
if [ "$relay" = php ]; then
    # The shipped header already is the wizard-default profile; only the
    # buffer moves out of the web root.
    sed "s|^const BUFFER_DIRECTORY = './phpvpn_data';|const BUFFER_DIRECTORY = '$fixture/buffer';|" \
        "$repository/server/php/phpvpn.php" > "$fixture/www/phpvpn.php"
    grep -q "^const BUFFER_DIRECTORY = '$fixture/buffer';" "$fixture/www/phpvpn.php"
    grep -q "^const SERVER_SECRET = 'CHANGE_THIS_SERVER_SECRET';" "$fixture/www/phpvpn.php"
    PHP_CLI_SERVER_WORKERS=4 php -S "127.0.0.1:$port" -t "$fixture/www" > "$fixture/relay.log" 2>&1 &
    server_pid=$!
else
    python=${PHPVPN_SERVER_PYTHON:-python3}
    sed -e "s|^STORAGE_DRIVER = 'disk'|STORAGE_DRIVER = 'memory'|" \
        -e "s|^LISTEN_PORT = 8000|LISTEN_PORT = $port|" \
        "$repository/server/python/phpvpn.py" > "$fixture/www/phpvpn.py"
    grep -q "^LISTEN_PORT = $port" "$fixture/www/phpvpn.py"
    grep -q "^SERVER_SECRET = 'CHANGE_THIS_SERVER_SECRET'" "$fixture/www/phpvpn.py"
    "$python" -B "$fixture/www/phpvpn.py" > "$fixture/relay.log" 2>&1 &
    server_pid=$!
fi

attempt=0
until [ "$(curl -s -o /dev/null -w '%{http_code}' --max-time 2 "$url" || true)" = 200 ]; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 60 ] || ! kill -0 "$server_pid" 2>/dev/null; then
        echo "run-server-contract.sh: the $relay relay did not become available at $url" >&2
        cat "$fixture/relay.log" >&2 || true
        exit 1
    fi
    sleep 0.5
done
echo "relay: $relay at $url"

cd "$client"
cmake --preset "$preset" -B "$build" -DPHPVPN_BUILD_CONTRACT_TESTS=ON
cmake --build "$build" --target phpvpn-contract-tests
PHPVPN_CONTRACT_URL=$url ctest --test-dir "$build" -R '^phpvpn-contract-tests$' -V
