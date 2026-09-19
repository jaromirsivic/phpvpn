# Native GUI development

Use an interactive Windows AMD64 desktop with Visual Studio C++ build tools,
CMake >= 3.28 and Ninja already installed. No administrator rights or installed
PHPVPN service are required. From the repository root:

~~~powershell
./client/tools/dev-tray.ps1
./client/tools/dev-tray.ps1 -View Wizard -Scene Identity
./client/tools/dev-tray.ps1 -View Connect -Scene Failed
./client/tools/dev-tray.ps1 -View Disconnect -Scene Disconnecting
./client/tools/dev-tray.ps1 -View Info -Scene Who
./client/tools/dev-tray.ps1 -View Info -Scene Raw
./client/tools/dev-tray.ps1 -Mode Build -Configuration Release
./client/tools/dev-tray.ps1 -Mode Test -Filter plan_v001_
./client/tools/dev-tray.ps1 -Mode Test -Filter wp06_scene_matrix
./client/tools/dev-tray.ps1 -Mode Test -Configuration Release -Filter wp06_
~~~

An absolute script path also works from another working directory and in paths
containing spaces. Preview is the default mode, with Contracts/Default and
Debug. View and Scene are accepted only in Preview. Test requires a nonempty
substring Filter, which is rejected in other modes. Zero selected tests fail.
Build only compiles and checks the executable. -Reconfigure reruns CMake without
deleting its cache. Unknown parameters and invalid combinations fail before build.

| View | Scene | Actual behavior |
|---|---|---|
| Tray | Default | Real tray menu and multiple windows, private mutex and tooltip PHPVPN GUI preview - synthetic data, exit through Exit. |
| Connect | Default, Connecting, Connected, Failed | Default is pending. Connected uses the real three-second success countdown. Failed shows endpoint unavailable/invalid. |
| Disconnect | Disconnecting, Disconnected, Failed | Real Disconnect action and production progress window over an initially connected synthetic snapshot. The scenes show waiting, confirmed success and an unconfirmed Disconnect without contacting the installed service. |
| Editor | Default, Invalid | Valid synthetic document or malformed JSON containing valid UTF-8, with the real Reset and Save controls. |
| Info | Who, Raw | Real Show Info action with one synthetic get-server-info. Who displays the native contracts table. Raw selects the second native tab and shows the same complete, read-only server snapshot. |
| Statistics | Default, Unavailable | Nine exact uint64 counters including values above 2^53. Unavailable first loads a valid snapshot and then loses status, retaining stale values. |
| Wizard | Default, Gate, Url, Secret, Identity, Contracts | Default is Url. Later steps use real Verify actions and synthetic successful replies. Contracts is the Wizard summary. |
| Contracts | Default, Invalid, Saving, SaveUnknown | A /24 document produces 252 rows. Invalid edits an enabled Key. Saving presses real OK and holds the reply until the original 30-second deadline. SaveUnknown loses the acknowledgement after a possible publication and another synthetic writer's change. Check enables an explicitly confirmed Save draft again. |
| Password | Default | Real nonmodal Password SHA child of Contracts. More children and parent OK remain available. The process ends when Contracts closes. |

Preview has no 45-second timeout. Close the relevant window, or choose Exit in
Tray. The runner owns a Process with UseShellExecute=false and CreateNoWindow=true,
drains stdout/stderr concurrently to logs, waits and propagates the child exit code.
This suppresses the console without passing SW_HIDE to the first native ShowWindow.
The actual GUI remains visible. A saving window follows its production
deadline and confirmation rules before it can close.

Debug and actual Release use separate Ninja caches under
work/build/tray-dev-debug and work/build/tray-dev-release. The existing
release preset still means RelWithDebInfo. Only phpvpn-tray-ui-tests is built,
including its existing static dependencies, with at most four parallel build jobs. The exact checked output is
tests/phpvpn-tray-ui-tests.exe, AMD64 PE machine 0x8664. No-op iterations do not
compile or link. Incompatible caches are preserved and rejected. A failed
configuration, build or PE check never launches an old executable. Close an
existing preview before relinking it. The script does not kill processes.

Each invocation writes tool, configure/build and child stdout/stderr logs plus
timings to a unique work/logs/tray-dev/<time-and-id>/ directory. Logs contain no
configuration documents, passwords or keys. No output is copied into dist.

The opt-in host links the same production TrayApplication, windows, controls,
resources and DPI manifest. dev_preview.inc reuses the harness's Wizard backend
and adds memory-only configuration/status scenarios. Every preview has a fresh
UUID pipe and mutex, with no arbitrary endpoint parameter or production fallback.
The backend uses real parsers, document builders and CNG but no ConfigStore
filesystem, ProgramData configuration, HTTP, SCM, Wintun or routes.
Disconnect uses the production `IDM_TOGGLE` path and Info uses production
`IDM_SHOW_INFO`. Their fixture replies are synthetic and remain on the private
UUID pipe. Switching Info tabs or pages does not issue another get-server-info.

Legacy commands remain available:

~~~powershell
./client/tools/run-tray-ui-tests.ps1 -Filter editor_reset
./client/tools/run-tray-ui-tests.ps1 -Interactive
./work/build/tray-dev-debug/tests/phpvpn-tray-ui-tests.exe --interactive
./work/build/tray-dev-debug/tests/phpvpn-tray-ui-tests.exe --preview identity-small
./work/build/tray-dev-debug/tests/phpvpn-tray-ui-tests.exe --dev-preview Password --scene Default
~~~

Legacy --preview accepts secret, identity, identity-small and contracts. Its
contracts scene is the Wizard summary and retains its old 45-second limit.
--interactive has no limit. Confirmed Wizard Finish shows its notification,
closes its session and window, and opens no Contracts editor or VPN connection.

The UI target stays outside CTest. `plan_v001_` runs the focused native dialog
and footer regressions, `live_resize_repaint` compares 112 live edge/corner
frames per selected production window with a clean full repaint, and
`wp06_scene_matrix` opens every declared View/Scene pair and rejects missing or
hidden windows, unexpected requests and incorrect scene setup. wp06_ also checks a 46-second
preview, the actual Connect countdown, unknown Check/repeat, multiple Password
children, the original saving deadline, private endpoint cleanup, all Tray actions
and in-memory Wizard publication. Existing
wp04_, editor_reset and command_reader_private_disconnect filters retain
their production lifecycle and isolated reader checks. All filters are case
sensitive. A full unfiltered native run is explicit and can take several minutes.

For an intermittent Wizard transition, set `PHPVPN_UI_TRACE=1` before a filtered
test run. The opt-in host records a bounded numeric trace of clicks, focus,
capture, sent and queued messages (including timers and WM_QUIT), synthetic
backend commands/replies, modal acknowledgement and preview stages. It prints
the trace on process exit, with a dropped-entry count. No configuration text,
keys or IPC payloads are recorded. Remove the environment variable to disable
tracing. An ordinary timeout also reports its exact URL/secret transition,
original elapsed time, request counts and control presence/visibility/enabled
state. Diagnostics do not change waiting limits, retry actions or convert a
failed attempt into a pass. Fresh processes and separate raw output files are
required when comparing runs. See the [T01 report](../implementation-plan3-fix3-t01.md)
for the retained original FAILs.

Run the hidden-launch regression explicitly with Windows PowerShell 5.1:

~~~powershell
./client/tests/ui/test-hidden-launch.ps1
~~~

It launches the runner hidden from another working directory in Debug and Release,
then repeats both configurations in a fresh source copy with spaces in its path.
The real editor Reset/error/late-generation test must pass with a visible focused
editor and no console. It also checks no-op builds, zero matches and failure before
launch for invalid parameters, compilation, corrupt PE and a locked executable.
Temporary source/EXE mutations stay in that unique copy and are restored. Evidence
and the copy remain under work/logs/hidden-launch. The script is outside CTest.

These are synthetic GUI checks. They verify real production window composition,
native controls, selected input/lifecycle behavior and deterministic repaint over
safe fixture data. They do not prove real service/VPN operation, installation,
distribution, every DPI/monitor combination, screen-reader behavior or full N09b
runtime limits. Full visual/performance acceptance follows
tray_UX.md UX14 and the separate final audit. Screenshots belong only in work
logs, never as new tracked binary assets.

### Extended Server information

The `server_information` filter covers native tabs, 252 clients with 20 inserted
rows per page, full-model count, Refresh success and busy failure, atomic raw/table
replacement, closing a pending request, reopening, small-window footer and live
resize/repaint. `ServerInfo` core tests cover 0/1/20/21/141/252 peers, exact time
boundaries, future timestamps, sorting, local-key changes and 96/120/144/192 DPI
layout. `wp06_scene_matrix` checks the production preview dispatch. These tests
use synthetic private IPC. Physical monitor migration, high contrast and screen
reader checks remain manual and must be recorded separately.

## Polling/UI regression filters

`phpvpn-tray-ui-tests.exe server_information` includes `server_information_auto_refresh_lifecycle`, real five-second timers on both tabs, slow requests, disable/error/reopen, exact maximum raw plaintext and accessibility. `phpvpn-wp03-ui-tests.exe ContractsSavedDisconnect` covers actual default Enter/Yes, No, Escape, failed/unknown Save and connection/window identity changes through the production tray Disconnect. Run UI executables sequentially in an interactive session. Zero selected tests is not success.

Raw text checks cover visible LF and CRLF line breaks as well as exact copied LF
text. Server information has a 320 by 480 DIP minimum client area, preserving
at least one visible table row when the heading and footer wrap.

## Application icons and local Disconnect (2026-09-19)

`phpvpn-wp03-ui-tests WindowIcons` compares rendered small/big HWND icons with
resource 100 for progress, statistics, editor and all common system-dialog paths.
It also drives a real TrayApplication through a private pipe and shell adapter,
checking the immediate disconnect icon, an unchanged connected RPC reply,
teardown completion, duplicate action suppression and invalid responses.
The main UI harness checks the default icon whenever it finds a production
window, including reopened Wizard windows. Unit resource tests cover
96/120/144/192 DPI sizes and failed-load handle preservation. These checks do
not substitute for Explorer/Alt+Tab visual inspection or physical monitor moves.