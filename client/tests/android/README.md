# Android tests on the Windows host

Host-side helpers of the Android client tests (plan-android-v001, WP02 to WP11).
Results: [wp11-matrix-report-2026-11-01.md](wp11-matrix-report-2026-11-01.md) and the
sub-reports `wpNN-*-report-2026-10-31.md` next to this file.

All commands run in Git Bash from the repository root (or a worktree root), with
`adb` on `PATH`, `JAVA_HOME=D:/androidstudio/jbr` and `client/android/local.properties`
(`sdk.dir`, `phpvpn.androidDeps`). Set `MSYS_NO_PATHCONV=1` for any manual `adb shell`
with a device path; the scripts set it themselves.

## Emulators

| AVD | Port | Image | Root |
|---|---|---|---|
| `phpvpn-api34` | 5554 | google_apis x86_64, API 34 | `adb root` |
| `phpvpn-api35` | 5558 | google_apis x86_64, API 35 | `adb root` |
| `phpvpn-api36` | 5556 | google_apis_playstore x86_64, API 36 | none |

```sh
emulator -avd phpvpn-api36 -port 5556 -no-window -no-audio -gpu swiftshader_indirect -no-snapshot-save
```

On the API 34 and 35 images run `adb -s <serial> root` first: without it the `appops set … ACTIVATE_VPN allow`
of the scripts does not stick (`appops get` stays `ignore`, Connect ends in `tunnel_permission_missing` and the
system VPN settings list no VPN).

Two test runs never share one emulator (installs and uninstalls collide) and two emulators
never share one relay buffer (both would connect as `10.99.99.2` and take each other's packets).

## Relays

| Relay | URL from the emulator | Used by |
|---|---|---|
| nginx + PHP of the host (`D:\nginx\start.bat`) | `http://10.0.2.2/phpvpn.php` | peer tests with the Windows client on the host (`http://127.0.0.1/phpvpn.php` there) |
| per emulator, profile `wizard-default` | `http://10.0.2.2/wp11/api34/phpvpn.php` (`api35`, `api36`) | instrumented tests, lifecycle script, scenarios |
| `SERVER_VERSION 2.0.0` copy | `http://10.0.2.2/wp11/v2/phpvpn.php` | T-A14 (`RealRelayTest`) |
| `relay_block_proxy.py <port> 80 <block file>` | `http://10.0.2.2:<port>/wp11/apiNN/phpvpn.php` | lifecycle T-A16 b to d, T-A08 without stopping nginx |

```powershell
D:\nginx\start.bat
powershell -NoProfile -File client\tests\android\deploy-wp11-relays.ps1          # -Remove deletes them
```

## Scripts

| Script | Tests | Notes |
|---|---|---|
| `run-instrumented.sh <serial> <app\|bridge> <relay url> <log dir> [class]` | `:bridge:` and `:app:connectedDebugAndroidTest` (T-N03, T-N04, T-A01, T-A01b, T-A13, WP04 IPC, T-U01 to T-U04, T-A02, T-A05, T-A09, T-A11, T-A06, T-A07, T-A14) | Same steps as Gradle (uninstall, install `-t -g`, `am instrument -w -r`, uninstall) without Gradle, so emulators run in parallel; pulls the T-U02 screenshots into `<log dir>/screenshots`; reports `INCOMPLETE` when the instrumentation stream breaks |
| `run-matrix.sh <app\|bridge> <log root> <serial>=<url> ...` | the same on several emulators at once | |
| `../../tools/android/run-native-tests.ps1` | T-N01, T-N02 | `-Tests phpvpn-core-tests,phpvpn-logger-tests,phpvpn-android-platform-tests` inside `powershell -Command "& ..."` (a comma list after `-File` arrives as one string); `-Abi arm64-v8a` runs under the emulator's ARM translation |
| `service-lifecycle-tests.sh` (WP05) | T-A10, T-A12 (broadcast), T-A16, Always-on | block file of `relay_block_proxy.py` as the fourth argument |
| `peer-traffic-tests.sh` | T-A03, T-A04, Wi-Fi ↔ mobile data | second peer = desktop client on the host connected to the same relay; `make_peer_config.py` derives the emulator document from the peer's `config.json` |
| `wp11-scenarios.sh` | T-A08, T-A12 with `adb reboot`, T-A15, T-A20 (emulator), T-A21 | `RELAY_STOP`/`RELAY_START` (default nginx `stop.bat`/`start.bat`), `CONFIG_FILE`, `PEER_IP`, `PEER_PYTHON`, `TA20_IDLE_S`, `TA21_SECONDS` |
| `tcp_echo.py` | helper | TCP echo server/client and UDP sink on the peer |

The debug build's `DebugVpnCommandReceiver` takes the commands of the host scripts: `CONNECT`,
`DISCONNECT`, `STATUS`, `CONFIGURE` (default document with a relay URL), `CONFIGURE_FILE`
(a whole document the script wrote with `run-as` into `files/`) and `STATUS_JSON` (the whole
`get-status` on one log line). Release builds do not contain it.

## Typical run (WP11)

```sh
./client/android/gradlew -p client/android :bridge:testDebugUnitTest :app:testDebugUnitTest \
    :app:assembleDebug :app:assembleDebugAndroidTest :bridge:assembleDebugAndroidTest
client/tests/android/run-matrix.sh bridge work/wp11/logs emulator-5554=http://10.0.2.2/wp11/api34/phpvpn.php \
    emulator-5558=http://10.0.2.2/wp11/api35/phpvpn.php emulator-5556=http://10.0.2.2/wp11/api36/phpvpn.php
client/tests/android/run-matrix.sh app work/wp11/logs emulator-5554=... emulator-5558=... emulator-5556=...

# T-A03/T-A04: the Windows client on the host is connected to http://127.0.0.1/phpvpn.php as 10.99.99.2
PEER_PYTHON=<python with an inbound firewall rule> client/tests/android/peer-traffic-tests.sh emulator-5556 \
    http://10.0.2.2/phpvpn.php C:/ProgramData/phpvpn/config.json 10.99.99.3 \
    client/android/app/build/outputs/apk/debug/app-debug.apk "connect ta03 ta04 netswitch disconnect"

# T-A16 b to d through the block proxy
python -I client/tests/android/relay_block_proxy.py 18435 80 work/wp11/block-api35 &
client/tests/android/service-lifecycle-tests.sh emulator-5558 http://10.0.2.2:18435/wp11/api35/phpvpn.php \
    client/android/app/build/outputs/apk/debug/app-debug.apk work/wp11/block-api35 "ta10 ta16 ta12 alwayson"

# T-A08 with nginx stopped (stops every relay: nothing else may run)
client/tests/android/wp11-scenarios.sh emulator-5556 http://10.0.2.2/wp11/api36/phpvpn.php \
    client/android/app/build/outputs/apk/debug/app-debug.apk "ta08"
```

`work/` keeps logs, traces and the generated peer documents (they contain the test contract
key, never commit them).
