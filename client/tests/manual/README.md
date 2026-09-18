# Windows client release checks

Status: **NOT RUN**. This checklist requires an installed service and interactive
Windows sessions. Unit tests of the parser, presentation model, polling schedules
and layout calculations do not constitute visual or lifecycle verification.
Record OS version, display scaling, build identifier, actual result and tester
for each scenario before release. Never add screenshots or secret configuration
values to the repository.

- [ ] **19, NOT RUN**: inspect all three shield icons at 100%, 125%, 150% and 200%
  on light and dark taskbars. Confirm opaque white background, continuous black
  border, equal geometry and sharp rendering. Verify license notices in dist.
- [ ] **20, 35, NOT RUN**: at 100% DPI measure the default Edit Config client area
  as 1080 x 560. Drag every edge and corner repeatedly, especially widening the
  window beside Cancel where the old vertical strip appeared. Check no strips,
  doubled edges, overlaps or clipped Save/Cancel buttons. Exercise both horizontal
  and vertical scrollbars before and after resize, maximize, restore and DPI changes.
- [ ] **20, NOT RUN**: open Edit Config on a work area smaller than the desired
  window size, including a taskbar on the top or side. The title bar, Save and
  Cancel must remain accessible. Move between monitors at 100/125/150/200% DPI
  and confirm fonts, both scrollbars and layout follow the new scale.
- [ ] **21, NOT RUN**: choose Connect with the pointer on a secondary monitor at
  a different DPI. Progress opens centered inside that monitor's work area.
  Confirm orange shield and Connecting, checkpoints in the prescribed order,
  green outcomes reconstructed despite skipped phases, success only after
  connected and automatic close about three seconds later with no countdown.
- [ ] **22, 30, 31, NOT RUN**: independently trigger unavailable/invalid endpoint,
  server verification failure, refused ACCESS_KEY, tunnel setup failure, timeout
  in each bootstrap phase and other communication failures. Check service-owned
  safe English descriptions, correct red point and orange unperformed points.
  Three green points do not turn a tunnel failure into success. Failure remains
  open through successful automatic recovery until the user closes it.
- [ ] **23, 35, NOT RUN**: close progress while connecting and confirm the service
  still completes the attempt. Capture IPC command names only: close must not
  issue disconnect. Reopen with a later Connect and verify a fresh presentation.
- [ ] **29, 35, NOT RUN**: trace timestamped get-status commands without contents.
  Tray keeps its five-second deadlines. Progress adds one 500ms schedule while
  open, stopping on close. Statistics adds one immediate read on each opening
  and subsequently updates from the five-second snapshots. A slow get-info or
  save operation must not freeze either polling window.
- [ ] **28, 34, NOT RUN**: inspect all nine Statistics counters, full UINT64_MAX
  values, readable labels with original keys, right-aligned monospaced values
  and consistent snapshots across tray sessions and console. Missing/invalid
  replies must not silently replace counters with zero.
- [ ] **17, 23, 27, 28, 35, NOT RUN**: run two simultaneous interactive sessions
  with tray plus console. Compare global snapshots, connect/disconnect from each
  session, and observe the others. Exit, tray crash and logoff must not disconnect
  VPN or stop the service. Closing progress affects only its local window.
- [ ] **27, 32, NOT RUN**: verify disconnect, reconnect, recovery and service
  restart/crash lifecycle resets, including an in-flight response and a delayed
  completion. Confirm old traffic cannot repopulate the new/disconnected state.
- [ ] **35, NOT RUN**: Edit Config may display URL and keys returned by get-config.
  Show Info follows get-server-info. Progress, Statistics, tooltips and errors must not
  display URLs, keys, tags, ciphertexts, packet contents, internal paths or raw
  system/network details. Save only via set-config, Cancel without a write.
- [ ] **20, NOT RUN**: save asynchronously, close the editor before completion,
  and reopen later. An old completion must not close a new editor or discard its
  contents. Repeated clicks during the pending command must not duplicate saves.

## Server information acceptance, NOT RUN

- [ ] Connect A and B with matching keys and check all four checkpoints. Show
  Info on A must exclude A, show B as valid and count it in the heading.
- [ ] Connect C with a mismatching or missing A contract. A must see invalid.
  Publish B-C and C-D contracts and check that A receives neither their hashes
  nor their hidden peer lists in Raw server information.
- [ ] Keep B inactive for more than 60 seconds of server time, then Refresh.
  B must become Offline. Future lastSeen and exactly 60 seconds remain online.
- [ ] Save a matching local A key and Refresh without reconnect. Comparison must
  change using the saved configuration. The active VPN and A's published list
  must remain unchanged until an explicit reconnect.
- [ ] Exercise 0, 20, 21 and 252 peers, keyboard paging, Ctrl+End, both tabs,
  copying raw text, Refresh failure, close/reopen and service restart. A failed
  Refresh must retain both old views. Disconnect must remain available.
- [ ] Verify screen reader text and focus, high contrast, physical monitor/DPI
  migration and 320-DIP width. Record actual Windows settings and results.

## Polling a Server Information

Ověř dva klienty proti aktuálnímu serveru: po skončení pingu celé monotónní 5s minimum, poté exponenciální backoff a po novém paketu opět minimum. Rozliš skutečné VPN pakety od health a prázdných pollů. Ověř Yes/No po uložených contracts, výchozí Enter=Yes a standardní Disconnect bez Connect. V Server Information zkontroluj zelený/žlutý/offline řádek, zelenou celou závorku pro 0/1/2 klienty, modré OK, raw kopírování a výběr, Auto refresh na obou kartách včetně pomalého požadavku, vypnutí a Show Info reaktivace. Opakuj na 100/125/150/200 % DPI, na malé ploše, s klávesnicí, čtečkou a high contrast. Tento checklist není doklad provedení.
