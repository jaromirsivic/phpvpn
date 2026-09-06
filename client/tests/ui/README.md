# Explicit Win32 tray UI tests

Run from the repository root on an interactive Windows AMD64 desktop:

```powershell
./client/tools/run-tray-ui-tests.ps1
./client/tools/run-tray-ui-tests.ps1 -Filter editor_reset
./client/tools/run-tray-ui-tests.ps1 -Interactive
```

The runner configures `work/build/ui-debug` with `PHPVPN_BUILD_UI_TESTS=ON`
and builds `phpvpn-tray-ui-tests`. The option is off by default. These tests
open actual windows and are deliberately excluded from unattended CTest.
A filter matching no tests fails. The executable also accepts `--interactive`
directly, for screenshot inspection without another build.

The executable links the production WizardWindow, WizardReader,
CommandReader, StatusReader, EditorWindow and ContractsWindow. Every scenario
uses a fresh CNG-random private named pipe. Its deterministic stateful backend
returns the real IPC schemas. Window discovery, text reads and button control
are restricted to this executable's process and UI thread. No installed service,
ProgramData file, adapter or route is changed.

Seven scenarios cover the placeholder URL, three explicit Next actions,
stored/default key behavior after IP changes, exact control-character secret
presentation, delayed start acknowledgements and operation results, no automatic
retry, Reset Yes/No, Cancel, X, Escape, the actual 15-second heartbeat and epoch
resume gate, one confirmed Finish opening one empty Contracts window without a
configuration read, and asynchronous Editor Reset failure/generation handling.
Native footer rectangles are checked at 1000x680 and 480x320 Wizard client sizes,
and 1080x560 and 480x320 Editor client sizes. Tab, Shift+Tab and Enter pass through
the production message translation path.

The final scenario holds get-info on the diagnostic executor while a second
production CommandReader sends disconnect. It prints send, backend reception and
valid status confirmation times, requires confirmation below 1000 ms before
get-info completes, verifies an explicit busy rejection, then verifies that the
original accepted get-info result is delivered exactly once. This exercises the
reader and pipe transport path. It does not claim full tray-main, Wintun, network
teardown or installed-service end-to-end coverage.

Interactive mode starts the edit flow with an ordinary test secret and key.
Closing the Wizard, or closing Contracts after Finish, exits the executable.
Screenshot rendering, cross-monitor DPI transitions and visual appearance still
require separate manual QA. Screenshots do not belong in the repository.
