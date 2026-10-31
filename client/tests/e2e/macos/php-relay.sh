#!/bin/sh
# Private `php -S` relay of the macOS test scripts: sourced by
# run-service-e2e.sh, run-install-test.sh and tools/macos/run-server-contract.sh,
# never executed. POSIX sh with the BSD userland of macOS.
#
# PHP_CLI_SERVER_WORKERS makes the PHP built-in server fork worker processes
# that inherit the listening socket. The server arms a parent-death signal for
# them through prctl (Linux) and procctl (FreeBSD) only, so on macOS a plain
# `kill` of the master leaves the workers serving the port for the next script:
# the service e2e's relay of another MAJOR version on PHPVPN_E2E_PORT+1 survived
# its kill, and the install test, whose relay defaults to that same port, found
# "its" relay answering, connected to the wrong server and failed with
# server_version_incompatible. Hence:
#
#   start_php_relay <port> <document root> <log> [workers]
#       Refuses a port that already answers, starts `${php:-php} -S` with the
#       given number of workers (default 4) in the background, sets
#       php_relay_pid and waits up to 30 s until /phpvpn.php answers 200 from
#       that process. Returns 1 with the reason and the relay log on stderr
#       otherwise, with nothing left running.
#   stop_php_relay <pid>
#       Ends the workers first (once the master is gone they are reparented
#       and no longer found), then the master, waits for all of them and
#       forces stragglers with SIGKILL. An empty pid is a no-op.

php_relay_port_in_use() { curl -s -o /dev/null --max-time 2 "http://127.0.0.1:$1/"; }

start_php_relay() {
    php_relay_port=$1
    php_relay_root=$2
    php_relay_log=$3
    php_relay_workers=${4:-4}
    php_relay_url="http://127.0.0.1:$php_relay_port/phpvpn.php"
    php_relay_pid=
    if php_relay_port_in_use "$php_relay_port"; then
        echo "php relay: 127.0.0.1:$php_relay_port is already in use" >&2
        return 1
    fi
    PHP_CLI_SERVER_WORKERS=$php_relay_workers "${php:-php}" -S "127.0.0.1:$php_relay_port" -t "$php_relay_root" \
        > "$php_relay_log" 2>&1 &
    php_relay_pid=$!
    php_relay_attempt=0
    until [ "$(curl -s -o /dev/null -w '%{http_code}' --max-time 2 "$php_relay_url" || true)" = 200 ]; do
        php_relay_attempt=$((php_relay_attempt + 1))
        if [ "$php_relay_attempt" -ge 60 ] || ! kill -0 "$php_relay_pid" 2> /dev/null; then
            echo "php relay: did not become available at $php_relay_url" >&2
            cat "$php_relay_log" >&2 2> /dev/null || true
            stop_php_relay "$php_relay_pid"
            php_relay_pid=
            return 1
        fi
        sleep 0.5
    done
    # The relay answers, but a master that could not bind the port has ended by
    # now: then the answer came from somebody else.
    if ! kill -0 "$php_relay_pid" 2> /dev/null; then
        echo "php relay: the php -S process on 127.0.0.1:$php_relay_port ended, another process answers there" >&2
        cat "$php_relay_log" >&2 2> /dev/null || true
        wait "$php_relay_pid" 2> /dev/null || true
        php_relay_pid=
        return 1
    fi
    return 0
}

stop_php_relay() {
    php_relay_stop_pid=${1:-}
    [ -n "$php_relay_stop_pid" ] || return 0
    php_relay_worker_pids=$(pgrep -P "$php_relay_stop_pid" 2> /dev/null || true)
    # shellcheck disable=SC2086
    kill $php_relay_worker_pids "$php_relay_stop_pid" 2> /dev/null || true
    wait "$php_relay_stop_pid" 2> /dev/null || true
    php_relay_attempt=0
    while [ "$php_relay_attempt" -lt 50 ]; do
        php_relay_alive=
        for php_relay_worker in $php_relay_worker_pids; do
            if kill -0 "$php_relay_worker" 2> /dev/null; then php_relay_alive=1; fi
        done
        [ -n "$php_relay_alive" ] || return 0
        php_relay_attempt=$((php_relay_attempt + 1))
        sleep 0.1
    done
    # shellcheck disable=SC2086
    kill -KILL $php_relay_worker_pids 2> /dev/null || true
    return 0
}
