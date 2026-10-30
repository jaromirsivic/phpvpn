# phpvpn client for macOS (Apple Silicon)

This folder installs the phpvpn client on macOS 14 Sonoma or newer on Apple
Silicon (arm64). The client consists of the system service `phpvpn-service`,
run by launchd as the daemon `com.pilandrome.phpvpn`, the command line client
`phpvpn` and the menu bar application `phpvpn Tray`. The service owns a `utun`
interface, its VPN address and one route to the VPN network; the default route
and DNS are never changed (split tunnel).

## Contents

| File | Purpose |
|---|---|
| `phpvpn-<version>-arm64.pkg` | Installer package: service, command line client, launchd daemon, tray application and its launch agent |
| `install.sh` | Verifying installer |
| `uninstall.sh` | Uninstaller |
| `release-manifest.json` | List of every file with size and SHA-256 |
| `release-manifest.sig` | Detached RSA PKCS#1 v1.5 SHA-256 signature of the manifest |
| `release-signing.cer` | Public certificate of the signing key |
| `README.md` | This document |

The package installs:

| Path | Content |
|---|---|
| `/Library/PrivilegedHelperTools/phpvpn-service` | Service (launchd daemon, runs as root) |
| `/usr/local/bin/phpvpn` | Command line client |
| `/Library/LaunchDaemons/com.pilandrome.phpvpn.plist` | Registration of the service |
| `/Applications/phpvpn Tray.app` | Menu bar application |
| `/Library/LaunchAgents/com.pilandrome.phpvpn-tray.plist` | Tray start at every login |

## Requirements

- macOS 14 Sonoma or newer.
- A Mac with Apple Silicon (M1 or newer). Intel Macs are not supported.
- An administrator account (root through `sudo`).
- The `openssl` (LibreSSL) and `shasum` commands for the signature check. Both
  are part of macOS; nothing has to be installed.

## Installation

Unpack the folder, open Terminal, change into the folder and run the
installer. It re-runs itself through sudo when needed and asks for the
administrator password.

```sh
cd ~/Downloads/phpvpn-macos      # the unpacked folder
sudo sh install.sh
```

Options:

| Option | Effect |
|---|---|
| `--headless` | Installs the service and the command line client and does not start the tray at login. The application stays in `/Applications` and can be started by hand. |
| `--verify-only` | Verifies the folder and stops. Nothing is installed; root is not needed. |

Running the installer again with a newer folder upgrades the installation; the
configuration is kept. An installation with `--headless` removes the tray
launch agent, a later installation without it restores it.

### Gatekeeper and the code signature

The programs carry an ad hoc code signature (`codesign -s -`) and the `.pkg`
is not signed with an Apple Developer ID, so Apple has not notarized it. This
is a development signature, not a publicly trusted publisher identity, just
as the Windows package uses a development signing certificate.

- `sudo sh install.sh` from Terminal installs the package with
  `installer -pkg ... -target /`; Gatekeeper does not block this path. It is
  the supported installation.
- Double-clicking the `.pkg` in Finder shows a warning that Apple cannot check
  it for malicious software. On macOS 14 right-click the package and choose
  Open; on macOS 15 open System Settings, Privacy & Security, and choose Open
  Anyway. This path skips the manifest verification of `install.sh`.

The authenticity of the folder is established by the signed manifest, not by
an Apple signature: obtain the folder from a trusted publisher channel. The
private signing key is never included.

## What the installer does

The installer is fail closed. It changes nothing until all checks passed:

1. root (or sudo),
2. macOS 14 or newer (`sw_vers`) and an Apple Silicon processor,
3. the manifest signature: the public key of `release-signing.cer` must match
   the SHA-256 of the signing key pinned in `install.sh`, and
   `release-manifest.sig` must verify `release-manifest.json`,
4. the size and SHA-256 of every listed file, checked on a private root owned
   copy that is then the installation source; an unlisted package or script
   in the folder is refused,
5. `installer -pkg phpvpn-<version>-arm64.pkg -target /`. The package scripts
   stop an older service and tray, create the configuration, state and log
   directories, register and start the service with `launchctl bootstrap
   system`, and start the tray for the user logged in at the screen
   (`launchctl bootstrap gui/<uid>`),
6. the control socket `/private/var/run/phpvpn/control-v1.sock` within 30
   seconds,
7. `phpvpn status`.

The installer log of the package is `/var/log/install.log`.

## Configuration

The configuration is the same JSON document as on Windows and Linux. The
service stores it in `/Library/Application Support/phpvpn/config.json`
(directory 0700, file 0600, root only). The command line client needs no root;
it talks to the service over the socket.

```sh
phpvpn set-config ./config.json    # validate and store a document
phpvpn get-config                  # print the stored document
phpvpn connect
phpvpn disconnect
```

The Configuration Wizard of the tray creates the configuration interactively,
as on Windows.

## The tray

`phpvpn Tray` shows the connection state as an icon in the menu bar
(disconnected, connecting, connected) with the same menu as on Windows:
Connect or Disconnect, Configuration Wizard, Communication contracts, Edit
Config, Show Server Info, Statistics and Exit. It has no Dock icon.

- Autostart: the launch agent `/Library/LaunchAgents/com.pilandrome.phpvpn-tray.plist`
  starts the tray at every login of every user. The installer also starts it
  at once for the user logged in at the screen.
- Manual start, for example after Exit or after a `--headless` installation:
  open `phpvpn Tray` from the Applications folder or Launchpad, or run
  `open "/Applications/phpvpn Tray.app"`.
- One tray runs per login session; a second start only passes its request to
  the running one.
- Exit and logging out close only the tray. The VPN connection belongs to the
  service and stays as it is; `phpvpn status` and `phpvpn disconnect` work
  without the tray.

## Diagnostics and logs

```sh
phpvpn status                  # service and connection state (JSON)
phpvpn diagnostics             # extended local diagnostics (JSON)
phpvpn info                    # verified server information
sudo launchctl print system/com.pilandrome.phpvpn
```

The service writes its log to `/private/var/log/phpvpn/phpvpn-service.log`
(rotated at 8 MiB, five files kept). Output of the process outside that log,
for example a failed start, goes to `/private/var/log/phpvpn/launchd.log`.
Both can be read with `cat`, `tail -f` or the Console application.

The command line client prints `code: description` on standard error and a
`hint:` line when the service is not running or the socket is missing. Exit
codes: 0 success, 1 rejected or unreachable, 2 invalid usage. A stopped
service is started again with:

```sh
sudo launchctl bootstrap system /Library/LaunchDaemons/com.pilandrome.phpvpn.plist
```

## Uninstallation

```sh
sudo sh uninstall.sh                # keeps configuration, state and logs
sudo sh uninstall.sh --remove-data  # also removes them
```

The uninstaller asks the service to disconnect, stops the tray and the
service (`launchctl bootout`), removes the installed files listed by
`pkgutil --files com.pilandrome.phpvpn` and the tray application, forgets the
package receipt and removes `/private/var/run/phpvpn`. Without
`--remove-data` the directories `/Library/Application Support/phpvpn`,
`/private/var/db/phpvpn` and `/private/var/log/phpvpn` stay, so a later
installation connects with the same settings.

## Limitations

- Apple Silicon (arm64) only. Intel Macs are not supported.
- The tunnel is a kernel `utun` interface driven by the root service, without
  a Network Extension. The interface name (`utunN`) is chosen by macOS; it
  does not appear as a VPN in System Settings, Network.
- The client is not distributed through the App Store.
- Development signature: ad hoc code signature, no Developer ID, no
  notarization (see Gatekeeper above).

## Differences from Windows and Linux

| Area | Windows | Linux | macOS |
|---|---|---|---|
| Service management | SCM | systemd unit | launchd daemon `com.pilandrome.phpvpn` |
| Tunnel | Wintun adapter `PHPVPN` | `/dev/net/tun`, interface `phpvpn` | `utun`, interface `utunN` chosen by the kernel |
| Configuration | `%ProgramData%\phpvpn\config.json` | `/etc/phpvpn/config.json` | `/Library/Application Support/phpvpn/config.json` |
| IPC | named pipe | Unix socket `/run/phpvpn` | Unix socket `/private/var/run/phpvpn` |
| Logs | file with rotation 8 MiB x 5 | journald | file with rotation 8 MiB x 5 in `/private/var/log/phpvpn` |
| Tray | notification area | AppIndicator | menu bar |
| Tray autostart | HKLM Run value | `/etc/xdg/autostart` | launch agent in `/Library/LaunchAgents` |
| Installation | signed ZIP with PowerShell | signed manifest, RPM or DEB, `install.sh` | signed manifest, `.pkg`, `install.sh` |
| Code signature | development certificate | none (package manager) | ad hoc, no Developer ID |
