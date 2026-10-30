#!/bin/sh
set -eu
umask 077
root=$(CDPATH= cd -P -- "$(dirname -- "$0")" && pwd)
cd "$root"
for name in phpvpn.py requirements-server.txt server-control.py; do
    [ -f "$root/$name" ] || { echo "ERROR: Missing distribution file: $name" >&2; exit 1; }
done
for path in "$root/.tools" "$root/.tools/uv" "$root/.tools/uv/uv" "$root/.bootstrap.lock" "$root/.venv" "$root/.runtime"; do
    [ ! -L "$path" ] || { echo "ERROR: Symlink is not allowed: $path" >&2; exit 1; }
done
attempt=0
until mkdir "$root/.bootstrap.lock" 2>/dev/null; do
    [ -d "$root/.bootstrap.lock" ] || { echo 'ERROR: Cannot create initialization lock.' >&2; exit 1; }
    attempt=$((attempt + 1))
    [ "$attempt" -lt 60 ] || { echo 'ERROR: Initialization lock is busy. Inspect interrupted init before removing .bootstrap.lock.' >&2; exit 1; }
    sleep 1
done
installer="$root/.bootstrap.lock/install-uv.sh"
trap 'rm -f "$installer" "$root/.bootstrap.lock/.curlrc" "$root/.bootstrap.lock/wgetrc"; rmdir "$root/.bootstrap.lock"' 0
trap 'exit 1' HUP INT TERM
# A present state is conservatively rejected before bootstrap. Once uv supplies
# Python, the OS lock (not PID metadata) is the final check before changing venv.
if [ -f "$root/.runtime/state.json" ]; then
    if [ -x "$root/.venv/bin/python" ]; then
        "$root/.venv/bin/python" -I -B -c 'import fcntl,sys; f=open(sys.argv[1], "a+b"); fcntl.flock(f, fcntl.LOCK_EX|fcntl.LOCK_NB)' "$root/.runtime/instance.lock" || {
            echo 'ERROR: Stop the running server before init.' >&2; exit 1;
        }
    else
        echo 'ERROR: Existing runtime with missing venv. Inspect the deployment before init.' >&2
        exit 1
    fi
fi
uv_path=$(command -v uv || true)
if [ -n "$uv_path" ] && "$uv_path" --version; then
    :
elif [ -x "$root/.tools/uv/uv" ] && "$root/.tools/uv/uv" --version; then
    uv_path="$root/.tools/uv/uv"
else
    export UV_INSTALL_DIR="$root/.tools/uv" UV_NO_MODIFY_PATH=1
    # The official installer invokes curl/wget itself. Give those child downloads
    # finite limits without changing the downloaded installer or user profiles.
    printf '%s\n' 'connect-timeout = 15' 'max-time = 90' 'retry = 2' 'retry-max-time = 180' > "$root/.bootstrap.lock/.curlrc"
    printf '%s\n' 'timeout = 30' 'tries = 2' > "$root/.bootstrap.lock/wgetrc"
    export CURL_HOME="$root/.bootstrap.lock" WGETRC="$root/.bootstrap.lock/wgetrc"
    if command -v curl >/dev/null 2>&1; then
        curl --proto '=https' --tlsv1.2 -fLsS --connect-timeout 15 --max-time 90 --retry 2 https://astral.sh/uv/install.sh -o "$installer"
    elif command -v wget >/dev/null 2>&1; then
        wget --timeout=30 --tries=2 -O "$installer" https://astral.sh/uv/install.sh
    else
        echo 'ERROR: Install curl or wget, then run init again.' >&2
        exit 1
    fi
    sh "$installer"
    uv_path="$root/.tools/uv/uv"
    "$uv_path" --version
fi
# Resolve a PATH result before handing it to the controller.
case "$uv_path" in /*) ;; *) uv_path="$(pwd)/$uv_path" ;; esac
unset VIRTUAL_ENV PYTHONHOME PYTHONPATH
export UV_HTTP_TIMEOUT=30 UV_HTTP_RETRIES=2
"$uv_path" --no-config python install --no-bin --no-registry 3.13
python=$("$uv_path" --no-config python find --managed-python 3.13)
"$python" -I -B "$root/server-control.py" init "$uv_path"
