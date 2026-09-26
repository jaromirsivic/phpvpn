# phpvpn client for Linux (x64)

This folder installs the phpvpn client on Fedora and Ubuntu. The client
consists of the system service `phpvpn-service`, run by systemd as
`phpvpn.service`, and the command line client `phpvpn`. The service owns the
TUN interface `phpvpn`, its VPN address and one on-link route; the default
route and DNS are never changed (split tunnel).

## Contents

| File | Purpose |
|---|---|
| `phpvpn-<version>-<release>.x86_64.rpm` | Package for Fedora |
| `phpvpn_<version>-<release>_amd64.deb` | Package for Ubuntu |
| `install.sh` | Verifying installer |
| `uninstall.sh` | Uninstaller |
| `release-manifest.json` | List of every file with size and SHA-256 |
| `release-manifest.sig` | Detached RSA PKCS#1 v1.5 SHA-256 signature of the manifest |
| `release-signing.cer` | Public certificate of the signing key |
| `README.md` | This document |

The graphical tray is the separate package `phpvpn-tray`. When the folder holds
it (`phpvpn-tray-*.rpm`, `phpvpn-tray_*.deb`), it also holds the Material
Symbols licence and notice of the tray icons. This release contains no tray
package, so every installation is headless.

## Supported distributions

| Distribution | Package | Built on |
|---|---|---|
| Fedora 44 or newer, x86_64 | RPM | Fedora 44 |
| Ubuntu 24.04 LTS or newer, amd64 | DEB | Ubuntu 24.04 |

Requirements: systemd, the kernel TUN device `/dev/net/tun`, root or sudo, and
the `openssl` command for the signature check (installed by default on both
distributions). Package dependencies (libcurl, OpenSSL, libsystemd) come from
the distribution repositories. SELinux (Fedora) stays enforcing: the service
runs in the default `unconfined_service_t` domain without a custom policy. No
AppArmor profile is installed on Ubuntu.

Other distributions, other architectures, Flatpak and Snap are not supported.

## Installation

Unpack the folder, then run the installer from it. The installer re-runs
itself through sudo when needed.

Headless server or a system without a graphical target:

```sh
sudo sh install.sh --headless
```

Desktop (installs `phpvpn-tray` as well when the folder holds it and the
default systemd target is `graphical.target`):

```sh
sudo sh install.sh
```

Options:

| Option | Effect |
|---|---|
| `--headless` | Never installs `phpvpn-tray`. Chosen automatically without a graphical target or without a tray package. |
| `--no-weak-deps` | Installs no weak (Fedora `install_weak_deps=False`) or recommended (Ubuntu `--no-install-recommends`) dependencies. |
| `--verify-only` | Verifies the folder and stops. Nothing is installed; root is not needed. |

The installer is fail closed. It changes nothing until all checks passed:

1. root (or sudo),
2. the distribution from `/etc/os-release` (Fedora or Ubuntu, minimum version
   from the signed manifest),
3. the manifest signature: the public key of `release-signing.cer` must match
   the SHA-256 of the signing key pinned in `install.sh`, and
   `release-manifest.sig` must verify `release-manifest.json`,
4. the size and SHA-256 of every listed file, checked on a private root owned
   copy that is then the installation source; an unlisted package or script
   in the folder is refused,
5. `dnf install` or `apt-get install` of the local package,
6. `systemctl enable --now phpvpn.service`,
7. the control socket `/run/phpvpn/control-v1.sock` within 30 seconds,
8. `phpvpn status`.

Running the installer again with a newer folder upgrades the package; the
package manager restarts the service and the configuration is kept. A running
tray is not stopped; the new tray version starts at the next login.

The package alone follows the distribution convention: Ubuntu enables and
starts the service on installation, Fedora leaves it disabled (presets).
`install.sh` always enables and starts it, as the Windows installer does.

The signing certificate of this build is the phpvpn development test
certificate. It is not a publicly trusted publisher certificate; the pinned key
protects the integrity of the folder, not the identity of a public publisher.

## Configuration

The configuration is the same JSON document as on Windows. The service stores
it in `/etc/phpvpn/config.json` (directory 0700, file 0600, root only). The
command line client needs no root; it talks to the service over the socket.

```sh
phpvpn set-config ./config.json    # validate and store a document
phpvpn get-config                  # print the stored document
phpvpn connect
phpvpn disconnect
```

On a desktop with `phpvpn-tray`, the tray Wizard creates the configuration
interactively, as on Windows.

## Diagnostics

```sh
phpvpn status                  # service and connection state (JSON)
phpvpn diagnostics             # extended local diagnostics (JSON)
phpvpn info                    # verified server information
systemctl status phpvpn
journalctl -u phpvpn           # service log (journald)
```

The command line client prints `code: description` on standard error and a
`hint:` line when the service is not running or the socket is missing. Exit
codes: 0 success, 1 rejected or unreachable, 2 invalid usage. See
`man phpvpn` and `man phpvpn-service`.

## Tray on Fedora GNOME (AppIndicator)

Vanilla GNOME on Fedora Workstation shows tray icons (StatusNotifierItem) only
with the AppIndicator extension. `phpvpn-tray` recommends the package
`gnome-shell-extension-appindicator` but does not enable it, because desktop
settings belong to the user:

```sh
sudo dnf install gnome-shell-extension-appindicator
gnome-extensions enable appindicatorsupport@rgcjonas.gmail.com
```

Log out and in again afterwards. Without the extension the tray opens its
control window with the state and the same actions as the menu. Ubuntu enables
AppIndicator support by default.

## Uninstallation

```sh
sudo sh uninstall.sh                # keeps /etc/phpvpn and /var/lib/phpvpn
sudo sh uninstall.sh --remove-data  # also removes the configuration and state
```

The uninstaller asks the service to disconnect, stops and disables
`phpvpn.service` and removes `phpvpn-tray` and `phpvpn`. On Ubuntu,
`apt purge phpvpn` also removes the data; the RPM never owns it.

## Differences from Windows

| Area | Windows | Linux | Reason |
|---|---|---|---|
| Service management | SCM, `phpvpn-service --install` | systemd unit from the package | Linux convention |
| Tunnel | Wintun, adapter `PHPVPN`, GUID | `/dev/net/tun`, non-persistent `phpvpn`, marker in `ifalias` | native mechanism, kernel cleanup |
| Reading the configuration from disk | readable by Users | root only (`0600`) | convention for secrets |
| IPC | named pipe, Everyone | Unix socket `0666`, the client checks that the server runs as root | same access model |
| Logs | file with rotation 8 MiB x 5 | journald | Linux convention |
| Tray tooltip | yes | state item in the menu and title | AppIndicator has no tooltip |
| Tray on Fedora GNOME | always | needs the AppIndicator extension, otherwise the control window | GNOME limitation |
| Window position | chosen by the application | chosen by the compositor (Wayland) | Wayland limitation |
| Service activation after installation | installer | `install.sh` enables and starts it; the RPM alone follows presets | distribution convention |
| Installation | signed ZIP with PowerShell | signed manifest, RPM or DEB, `install.sh` | distribution convention |
