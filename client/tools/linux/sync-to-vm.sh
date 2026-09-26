#!/bin/sh
# Copies the current source snapshot (tracked plus untracked, non-ignored files,
# including uncommitted edits) to a Linux build host over SSH. Development helper
# only, never part of a distribution. Credentials are never passed on the command
# line; configure key based login in the SSH config given by PHPVPN_SSH_CONFIG.
set -eu
host=${1:?usage: sync-to-vm.sh <ssh-host> [remote-directory]}
remote=${2:-phpvpn}
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
set -- ${PHPVPN_SSH_CONFIG:+-F "$PHPVPN_SSH_CONFIG"}
cd "$root"
git ls-files -z --cached --others --exclude-standard -- client phpvpn.py phpvpn.php \
    requirements-server.txt requirements-server.in tools/server-python tests-integration assets \
    | xargs -0 -r ls -d 2>/dev/null \
    | tar -cf - -T - \
    | ssh "$@" "$host" "rm -rf '$remote.new' && mkdir -p '$remote.new' && tar -xf - -C '$remote.new' && \
        mkdir -p '$remote' && rm -rf '$remote.src' && mv '$remote.new' '$remote.src' && \
        cd '$remote.src' && tar -cf - . | (cd \"\$HOME/$remote\" && tar -xf -) && rm -rf \"\$HOME/$remote.src\""
echo "synchronized $root -> $host:$remote"
