#!/bin/sh
set -eu
root=$(CDPATH= cd -P -- "$(dirname -- "$0")" && pwd)
if [ ! -x "$root/.venv/bin/python" ]; then
    echo 'ERROR: Environment is missing. Run sh init.sh first.' >&2
    exit 1
fi
exec "$root/.venv/bin/python" -I -B "$root/server-control.py" stop
