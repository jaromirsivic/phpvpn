# GTK tray development harness (Linux)

Development and verification aids for `client/apps/tray-linux`. Nothing here is
installed, packaged or used by the production tray.

| File | Purpose |
|---|---|
| `mock_service.cpp` | `phpvpn-tray-mock-service` (built with `BUILD_TESTING` and `PHPVPN_BUILD_TRAY`): scripted phpvpn service on a private Unix socket, valid IPC v1 replies, Connect/Disconnect scenarios and errors. Usage in the file header. |
| `tray_atspi.py` | AT-SPI probe: dump windows, press buttons, wait for texts, close windows. |
| `sni_stub.py` | Minimal `org.kde.StatusNotifierWatcher`, item inspection (IconName, Title, dbusmenu layout) and menu activation. |
| `run-tray-checks.sh` | End-to-end checks in a private D-Bus session with a headless mutter: `fallback` (control window, progress window, countdown, errors, Exit) and `sni` (indicator registration and menu). |

## Test-only tray settings

The tray reads two environment variables that exist for this harness only:

- `PHPVPN_TRAY_TEST_SOCKET=<absolute socket path>`: use the mock socket instead of
  `/run/phpvpn/control-v1.sock`.
- `PHPVPN_TRAY_TEST_SERVER_UID=<uid>`: the uid the socket peer must run as (default
  0). Accepted only together with `PHPVPN_TRAY_TEST_SOCKET`.

`PHPVPN_TRAY_ICON_THEME_PATH` points a non-installed run at a directory containing
`hicolor/`; a build tree run finds `<build>/apps/tray-linux/icons` by itself.

## Examples

```sh
# Automated checks (no unlocked desktop needed; mutter, python3-gobject, python3-pyatspi)
sh tests/linux/tray/run-tray-checks.sh ../work/build/linux-debug fallback
sh tests/linux/tray/run-tray-checks.sh ../work/build/linux-debug sni

# Manual run inside the logged in desktop session
B=../work/build/linux-debug/apps/tray-linux
$B/phpvpn-tray-mock-service --socket "$XDG_RUNTIME_DIR/phpvpn-mock/control.sock" &
PHPVPN_TRAY_TEST_SOCKET="$XDG_RUNTIME_DIR/phpvpn-mock/control.sock" \
PHPVPN_TRAY_TEST_SERVER_UID=$(id -u) $B/phpvpn-tray
```

The success countdown of the progress window starts at the first real paint of
"Connected successfully" (tray_UX.md UX05). On a locked or blanked screen the
compositor does not paint, so the window intentionally stays open until it is shown.
