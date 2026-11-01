# Host side environment of the device helpers, sourced by the bash scripts of this folder.
# Nothing here is specific to one computer: adb comes from PATH or from tools.adb of
# phpvpn.config.json (PHPVPN_CONFIG selects another file), the phone from ANDROID_SERIAL or
# android.deviceSerial, the phone's VPN address and the relay host from android.virtualIp and
# android.relayHost. Variables already set in the environment win.
_phpvpn_root=$(cd "$(dirname "${BASH_SOURCE:-$0}")/../../../.." && pwd)
_phpvpn_config() { python -I "$_phpvpn_root/tools/phpvpn_config.py" "$@" 2>/dev/null; }
if ! command -v adb >/dev/null 2>&1; then
  _phpvpn_adb=$(_phpvpn_config tool adb | tr '\\' '/')
  if [ -n "$_phpvpn_adb" ]; then
    _phpvpn_adb=$(dirname "$_phpvpn_adb")
    if command -v cygpath >/dev/null 2>&1; then _phpvpn_adb=$(cygpath -u "$_phpvpn_adb"); fi
    export PATH="$PATH:$_phpvpn_adb"
  fi
fi
if [ -z "${ANDROID_SERIAL:-}" ]; then
  _phpvpn_serial=$(_phpvpn_config get android.deviceSerial)
  if [ -n "$_phpvpn_serial" ]; then export ANDROID_SERIAL="$_phpvpn_serial"; fi
fi
if [ -z "${PHPVPN_DEVICE_IP:-}" ]; then
  PHPVPN_DEVICE_IP=$(_phpvpn_config get android.virtualIp); export PHPVPN_DEVICE_IP
fi
if [ -z "${PHPVPN_RELAY_HOST:-}" ]; then
  PHPVPN_RELAY_HOST=$(_phpvpn_config get android.relayHost); export PHPVPN_RELAY_HOST
fi
export MSYS_NO_PATHCONV=1
unset _phpvpn_root _phpvpn_adb _phpvpn_serial
