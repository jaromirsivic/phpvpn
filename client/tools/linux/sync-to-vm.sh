#!/bin/sh
# Copies the current source snapshot (tracked plus untracked, non-ignored files,
# including uncommitted edits) to a Linux build host over SSH. Development helper
# only, never part of a distribution.
#
#   sh client/tools/linux/sync-to-vm.sh [ssh-host] [remote-directory]
#
# The host (linuxHost.sshUser@linuxHost.sshHost), the key, the known hosts file
# and the remote directory come from the global configuration phpvpn.config.json,
# read through tools/phpvpn_config.py; the arguments override them. Credentials
# are never passed on the command line; the key is used with BatchMode.
# PHPVPN_SSH_CONFIG additionally selects an SSH config file (ssh -F).
set -eu
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
python=$(command -v python3 || command -v python || true)
[ -n "$python" ] || { echo "sync-to-vm.sh: python3 is required to read phpvpn.config.json" >&2; exit 2; }
config() { "$python" "$root/tools/phpvpn_config.py" "$@" 2> /dev/null | tr '\\' '/' || true; }

host=${1:-}
if [ -z "$host" ]; then
    config_host=$(config get linuxHost.sshHost)
    config_user=$(config get linuxHost.sshUser)
    [ -n "$config_host" ] || { echo "usage: sync-to-vm.sh [ssh-host] [remote-directory], or set linuxHost.sshHost" >&2; exit 2; }
    host=${config_user:+$config_user@}$config_host
fi
remote=${2:-$(config get linuxHost.remoteDirectory)}
remote=${remote:-phpvpn}
identity=$(config path linuxHost.sshIdentityFile)
known_hosts=$(config path linuxHost.sshKnownHostsFile)

set -- -o BatchMode=yes
if [ -n "$identity" ]; then set -- "$@" -i "$identity" -o IdentitiesOnly=yes; fi
if [ -n "$known_hosts" ]; then set -- "$@" -o UserKnownHostsFile="$known_hosts"; fi
if [ -n "${PHPVPN_SSH_CONFIG:-}" ]; then set -- "$@" -F "$PHPVPN_SSH_CONFIG"; fi
cd "$root"
git ls-files -z --cached --others --exclude-standard -- client phpvpn.py phpvpn.php \
    requirements-server.txt requirements-server.in tools/server-python tests-integration assets \
    phpvpn.config.json tools/phpvpn_config.py \
    | xargs -0 -r ls -d 2>/dev/null \
    | tar -cf - -T - \
    | ssh "$@" "$host" "rm -rf '$remote.new' && mkdir -p '$remote.new' && tar -xf - -C '$remote.new' && \
        mkdir -p '$remote' && rm -rf '$remote.src' && mv '$remote.new' '$remote.src' && \
        cd '$remote.src' && tar -cf - . | (cd \"\$HOME/$remote\" && tar -xf -) && rm -rf \"\$HOME/$remote.src\""
echo "synchronized $root -> $host:$remote"
