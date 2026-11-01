# Remaining checks requiring interactive intervention

These checks are not replaced by unit tests or by a successful isolated SCM/Wintun test. The main evidence produced is in implementation-plan3-report.md and in the matrix.

RSS at maximum IPC occupancy, genuinely held-back DNS and HTTP 503 under contention were subsequently performed automatically and passed. Details, the server bug that was found and fixed, and the limits of the evidence are given in the [supplementary report](../implementation-plan3-resource-report.md). These three scenarios no longer require user intervention.

## Actual 125 % and 200 % scaling

The prepared test UI target is started with `client/tools/run-tray-ui-tests.ps1 -Interactive`. It uses the production windows and a private test backend, and changes neither the service nor the configuration. The computer-use tool did not work in this session. Screen-master can capture the actual result after the setting has been changed.

Required user intervention: temporarily change the scaling of one monitor in Windows settings to 125 %, then to 200 %, and finally restore the original value. In each setting, go through URL, secret, identity and Contracts, shrink the window and verify all four buttons, scrolling, Tab/Shift+Tab/Enter/Escape and moving between monitors. The change of the setting for the user's entire screen was not performed automatically.

## Sleep/resume and two signed-in sessions

The isolated E2E uses the real service `phpvpn-plan3-e2e-d6c8`, a private pipe and its own adapter GUID. Its existing automatic controller is not a test of sleep/resume or of a second user.

Sleep/resume requires an interactive test window before sleep and at least 60 seconds without a valid heartbeat. After wake-up, the expired session should remain closed, without an automatic repetition of Next or Finish. An active VPN should perform a new serial time synchronization. Before sleep, other work in progress must be saved. Sleep interrupts the connection between this work session and the agent, so it requires a chosen moment and a manual wake-up by the user.

Two sessions require the user's second existing Windows account and simultaneous sign-in. Each session should have its own Wizard session. Closing/expiry of one must not end the other; a global Connect/Disconnect must change the epoch of both. Signing in and entering passwords is done by the user. No new account or password was created for the test.

Concrete walkthrough: in the first session, open the test Wizard, switch user without signing out, in the second session open a second Wizard and verify that the sessions are separate. Then close one session, verify the other, and trigger a global Connect/Disconnect of the isolated test service only. Separately, save the work in the second session and sign it out. The first session should remain usable; the abandoned Wizard session should expire after its lease. Signing out closes the applications of that user. Switching to another session may temporarily interrupt the agent's control of the desktop.

Before these two checks are run, a controller with an explicit mode that waits for a manual step needs to be added. The existing automatic runner cleans up after its test and must not be passed off as a ready sleep/multiuser test.

## Clean installation, upgrade and uninstallation of the package

The package has a verified manifest, files, x64 architecture and Wintun API via `install.ps1 -VerifyOnly`. The real isolated SCM test uses a separate service name. The production installation script works with the fixed name `phpvpn` and the production location.

A real phpvpn service is already running on this host. Running its install/upgrade/uninstall would affect the existing installation, so it was not performed. Decision required: reserve another Windows machine, or explicitly permit a controlled interruption and change of the current installation with a backup and rollback. The availability of a VM is not a prerequisite of the isolated SCM/Wintun test; it is only one of the options for the clean installation scenario.

## Actual power loss

A crash of the process itself has already passed; an actual power loss has not. The required intervention is to choose an expendable test Windows machine with a backup, or an isolated virtual disk with a controlled hard power-off. After checkpoints have been prepared during the write/flush/publication of the config and during an active tunnel, the user triggers a hard power-off and the subsequent start. Then a valid original or new document, journal recovery, ownership of the adapter/routes and the prohibition of automatic Connect are verified. A Windows restart or TerminateProcess does not replace this test. A hard power-off may lose other unsaved work and damage the file system. A hard power-off of a VM is evidence only for the virtual disk and the associated caches, not for a physical power loss of the host. This intervention was not performed on the current workstation.

## Overview of decisions for interactive continuation

| Gate | User step | Impact |
|---|---|---|
| DPI 125/200 % | Temporarily change the scale of one monitor, go through the test windows and restore the original value. | Rearrangement of windows and resizing of the entire desktop. No change to the service. |
| Two users and logoff | Choose a second account, enter the credentials personally, switch sessions and sign one of them out after saving work. | Signing out closes the applications of that session; desktop control may be interrupted. |
| Sleep/resume | Save work, choose the moment of sleep, leave the machine asleep for at least 60 s and wake it manually. | Both the network and the connection with the agent are temporarily interrupted. |
| Install/upgrade/uninstall | Choose a separate machine, or explicitly permit the backup, interruption and change of the existing installation. | The production installer uses the fixed name phpvpn. On the current host it would affect the service in use. |
| Power loss | Choose an expendable machine or an isolated virtual disk and a controlled hard power-off. | Risk of loss of unsaved data and of file system damage; a manual start is required afterwards. |

For sleep/multiuser/power-loss, before the manual step itself, a controller mode that saves the evidence and waits at a specific checkpoint must be added and verified locally. The existing automatic SCM runner has no such mode. No sign-out, sleep, uninstallation or shutdown of the existing service was started in the follow-up block.
