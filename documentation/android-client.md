# phpvpn client for Android

This guide installs and uses the phpvpn client on Android 14 or newer. The
client is a single application: the user interface, a foreground `VpnService`
and the native phpvpn core run in one process. It creates a VPN interface with
the configured `virtualIp` and exactly one route, the `vpnCidr` network
(default `10.99.99.0/24`). The default route, DNS, the local network and IPv6
are never changed (split tunnel): the client gives the phone access to the
phpvpn virtual network, it does not protect or redirect other traffic.

**Status: development build.** The Android client is not distributed yet. There
is no Google Play listing, no release signature and no package in the phpvpn
distribution. Only debug signed APKs built from the repository exist. The
application identifier `io.github.jaromirsivic.phpvpn` is provisional; if it
changes before the first release, the new build installs as a separate
application, the stored configuration is not carried over and the VPN
permission must be granted again.

## Contents

| File | Purpose |
|---|---|
| `phpvpn-<version>-debug.apk` | Debug signed application for `arm64-v8a` (phones) and `x86_64` (emulator) |
| `phpvpn-<version>-arm64-v8a-debug.apk` | The same for phones only |
| `phpvpn-<version>-x86_64-debug.apk` | The same for the emulator only |
| `phpvpn-<version>-release-unsigned.apk` | Unsigned release build, cannot be installed as is |
| `symbols/<abi>/libphpvpn-android.so` | Native library with symbols, for reading crash reports |
| `SHA256SUMS`, `manifest.json` | Checksums, versions and the results of the build checks |

`client/tools/android/make-android-dev.ps1` builds these files into
`work/android/<version>` and checks them; the `phpvpn-android-debug` artifact
of the `client-android` GitHub Actions workflow holds the same folder. A plain
`gradlew assembleDebug` in `client/android` gives the same debug application
as `client/android/app/build/outputs/apk/debug/app-debug.apk`. Building needs
the prebuilt OpenSSL and curl, see
[client/tools/android/README.md](../client/tools/android/README.md).

## Requirements

- Android 14 (API level 34) or newer. Older versions are not supported.
- A 64-bit device (`arm64-v8a`) or a 64-bit emulator (`x86_64`). 32-bit
  systems are not supported.
- A phpvpn relay server reachable from the phone and a matching configuration:
  server URL, `SERVER_SECRET`, the client's IP and access key, and the
  communication contracts with the other clients.

## Installation

From a computer with the Android platform tools and USB debugging enabled on
the phone:

```sh
adb install -r phpvpn-<version>-debug.apk
```

`-r` replaces an installed build of the same application and keeps its data,
including the configuration.

Directly on the phone: copy the APK to the device, open it in the Files
application and allow the installation of apps from that source when Android
asks. Then open **PHPVPN** from the app list.

## First start and configuration

Without a configuration the app opens **Welcome**. Choose the setup wizard to
enter, step by step, the server URL (with optional HTTP headers), the server
secret, the IP address and access key, and the default contracts. Each step is
verified against the server before the next one; Finish saves the
configuration. Saving does not connect.

Alternatively open the configuration editor. When no file exists yet, the app
shows the full path of the configuration file and asks whether to create the
default configuration now. **Yes** writes the file immediately and opens it in
the editor; **No** creates nothing. The editor shows the real file content, the
same JSON document as on Windows, Linux and macOS.

Settings also open Communication contracts, the editor, About and Exit.

## Connecting and the VPN permission

Tap **Connect** on the Connection screen. The first time, Android shows its
own dialog asking to allow phpvpn to set up a VPN connection. Accept it. If you
cancel it, the app shows **VPN permission denied**; nothing is wrong with the
server and **Request permission again** asks once more. Once the permission is granted the
dialog is skipped.

While connecting, the app shows four checks: server URL, server secret, access
key and communication contracts. You can leave the screen; the connection
continues in the background. **Connected** is shown only after the service has
confirmed the connection.

Only one VPN app can be active at a time. Connecting phpvpn ends the VPN of
another app, and another VPN app taking over ends phpvpn (see
Troubleshooting).

## Notification

While a connection is being set up, is up or is being restored, the client
runs as a foreground service with a quiet, ongoing notification in the
**VPN connection** channel. The title is the state (Connecting, Connected,
Reconnecting, Disconnecting); the text is the server host and your virtual IP,
for example `vpn.example.org · 10.99.99.2`. While connecting, connected or
reconnecting it offers **Disconnect**. Tapping it opens the app like the
launcher icon: a closed app starts on the Connection screen, an open app comes
back where you left it, so an unsaved edit is not lost.

The notification and the service end when the client is confirmed
disconnected, and also after a connection error, because then nothing is
running any more. A disconnection that is not confirmed never removes it.

The first time you tap **Connect**, the app asks for Android's notification
permission. If you refuse, the VPN still works and the app explains once that
the notification will be missing.

## Running in the background and battery

Closing the app window does not stop the VPN. The connection belongs to the
foreground service. Settings, **Exit** offers two choices:

- **Close window, stay connected** closes only the window.
- **Disconnect and close** disconnects, waits up to 30 seconds for the
  confirmed disconnected state and then closes the app. If the disconnection is
  not confirmed, the app shows the Connection screen instead.

The app never kills its own process.

The client polls the relay server at most every 5 seconds when idle and holds
no permanent wakelock. With the screen off, Android may delay network access
(Doze), so packets from other peers can arrive late. Settings, **Battery
optimisation** opens the system page where you can exempt phpvpn from battery
optimisation. Some manufacturers stop background apps more aggressively; the
exemption is the first thing to check if the connection ends while the phone
sleeps.

## Always-on VPN and start after boot

Settings and About show whether Android's **Always-on VPN** is set for phpvpn
and whether **Block connections without VPN** (lockdown) is on. With Always-on
the system starts the client and it connects when the configuration is valid.
Lockdown is not supported: the app warns when it is enabled, because while the
VPN is not connected Android may block the network access of other apps. How
lockdown interacts with the client's own traffic has not been tested yet.

The configuration option `autoConnectOnStartup` means "connect after the
device has started" on Android. It connects only if the VPN permission is
still granted; otherwise it does nothing and writes a log entry. It is not
the same as Always-on.

## What the app does and does not do

It does:

- create a VPN interface with your virtual IP and one route for the phpvpn
  network, and exchange IPv4 packets with the other peers through the relay,
- keep its own traffic to the relay server outside the tunnel,
- show the connection state, four checks, ten statistics, who is online, the
  raw server data, HTTP error details, the contracts and the configuration
  editor.

It does not:

- route internet traffic, DNS, the local network or IPv6 through the tunnel
  (no full tunnel),
- block traffic outside the VPN (no kill switch, no lockdown support),
- offer per-app selection, several profiles, accounts or server directories.

## Configuration and logs

The configuration is stored in the app's private storage that is excluded from
backups:

```
<private no-backup directory>/phpvpn/config/config.json
```

On most devices the private no-backup directory is
`/data/user/0/io.github.jaromirsivic.phpvpn/no_backup`; the app shows the exact
path in the editor and in **Configuration needs attention**. Other apps cannot
read it. The app disables cloud backup and device-to-device transfer of its
data, and screens that show secrets cannot be captured in screenshots.

The client log is `phpvpn/logs/phpvpn-service.log` in the same directory
(rotated at 8 MiB, five files kept). The same entries go to logcat with the tag
`phpvpn`:

```sh
adb logcat -s phpvpn
```

Logs never contain keys, secrets or packet content.

## Troubleshooting

| Screen or message | Meaning and what to do |
|---|---|
| **Status unavailable** | The app did not get the connection state within its time limit. Nothing was confirmed or changed. **Check status again** reads it again. |
| **VPN permission denied** | The system VPN dialog was cancelled, or the permission was revoked in the system settings (error `tunnel_permission_missing`). Tap **Request permission again** and accept the dialog. |
| **Another VPN app took over** | Another VPN app was started and Android ended phpvpn. This is a normal disconnection, phpvpn does not reconnect by itself. Tap **Connect** to take the VPN back. Removing phpvpn with **Forget VPN** in Android's VPN settings shows the same message, because Android reports both cases the same way. |
| **The connection ended** | Android stopped the app while the VPN was active. If Android restarts the VPN service, the client reconnects by itself only if you had connected, the VPN permission is still granted and the configuration is valid, at most three times within 10 minutes. After such a stop with the VPN up, Android 14 and 15 often do not restart the service at all; the next time the app starts (for example when you open it), it shows this message instead and does not reconnect. Connect again, and consider the battery optimisation exemption. |
| **Disconnection unconfirmed** | The client did not confirm the disconnection in time. **Check status** shows the real state, **Try disconnecting again** sends a new request. |
| **Configuration needs attention** | The configuration is missing, invalid or unreadable. Use the setup wizard, or the editor when the document is readable. |
| `config_directory_security_invalid` | The private configuration directory failed its safety checks. Restart the app; it repairs the directory at every start. |
| `netconfig_preflight_conflict` | The current network (for example a Wi-Fi) already uses addresses of the phpvpn network. Use another network or another `vpnCidr`. |
| `server_version_incompatible` | The server has another major version than the client. Install a client and server of the same major version. |

## Uninstallation

Uninstall phpvpn from the app list or with:

```sh
adb uninstall io.github.jaromirsivic.phpvpn
```

Android removes the app together with its private storage, including the
configuration and the logs.

## Limitations

- Development build only: debug signature, provisional application identifier,
  no distribution.
- Android 14 or newer, 64-bit only.
- Light theme only. English and Czech texts.
- Results so far come from emulators (API 34, 35 and 36). Tests on a physical
  device, battery and Doze measurements and long running peer traffic are
  still open.

## Differences from Windows, Linux and macOS

| Area | Windows | Linux | macOS | Android |
|---|---|---|---|---|
| Components | Service, CLI, tray | Service, CLI, tray | Service, CLI, tray | One app with a foreground VPN service |
| Tunnel | Wintun adapter `PHPVPN` | `/dev/net/tun`, interface `phpvpn` | `utun`, interface `utunN` | `VpnService`, interface `tunN` chosen by Android |
| Permission | Administrator installation | root | root | System VPN permission dialog |
| Configuration | `%ProgramData%\phpvpn\config.json` | `/etc/phpvpn/config.json` | `/Library/Application Support/phpvpn/config.json` | `phpvpn/config/config.json` in the private no-backup directory |
| IPC | named pipe | Unix socket | Unix socket | none, inside the app process |
| Logs | file with rotation | journald | file with rotation | file with rotation and logcat |
| User interface | notification area | AppIndicator | menu bar | full-screen app with four destinations |
| Start without user | service start | service start | service start | Always-on, or `autoConnectOnStartup` after boot |
| Installation | signed ZIP with PowerShell | signed manifest, RPM or DEB | signed manifest, `.pkg` | debug APK with `adb` or a file |

The technical specification of the Android client is the Android part of
[client.md](../client.md#android); the visual design is
[assets/android-en](../assets/android-en/README.md).
