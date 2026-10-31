# PHPVPN for Android: interface concept

A visual concept based on the current client in this repository, created on October 30, 2026. It does not implement an Android application or VPN service, and does not change production code. This is the complete English version of the approved Czech design, preserved in `../android-cz`.

## Open the design

- [index.html](index.html): a catalog of all **49 screens and states**, with PNG previews.
- [overview.html](overview.html): three key screens side by side.
- [previews/overview.png](previews/overview.png): a presentation image for sharing.
- [01-connected.html](01-connected.html): start exploring the connected experience.
- [00-welcome.html](00-welcome.html): first launch and entry to the setup wizard.
- [RULES.md](RULES.md): an index of rules and pitfalls for all 49 screens. Each description separates existing client behavior, proposed Android behavior and mockup limitations.

Open the HTML files in a browser. No server, internet connection or build is required. When moving the files, keep the `android-en` directory alongside the existing `phpvpn*.svg` files in `assets`. All images use local assets. The English version does not load any files from the Czech version.

Each screen includes a phone mockup and a design note, feature source and scenario selector beside it. On a mobile display, the note moves below the phone. Content inside the phone scrolls when needed, while primary actions stay outside the scrolling area. PNGs in `previews` show the initial phone viewport. The `previews/full` directory contains the complete expanded view of every screen, including content below the fold. The PNG link beside each mockup opens this full version.

## Interaction scope

Links navigate between prepared sample states. Text fields can be edited, switches toggled and secrets revealed. Selecting the default secret, automatic IP or default access key disables the corresponding input. Actions without a real result show a short message explaining the simulation.

The design does not save values, send HTTP requests, compute hashes or establish a VPN. Pages are independent scenarios. Selecting Verify opens a prepared result screen rather than validating the entered value. Alternative outcomes are available in the scenario selector. Sample credentials are not real credentials or a recommended production configuration.

## Navigation

| Main destination | Content |
| --- | --- |
| Connection | VPN status, connect and disconnect, checks, retry, recovery and unknown status |
| Traffic | The ten original statistics, data freshness and initial waiting state |
| Server | Online devices, communication contract validity, raw JSON and refresh |
| Settings | Setup wizard, contracts, JSON editor, reset, app information and background operation |

The wizard and editors are nested screens with Back navigation. Leaving with unsaved changes requires a discard confirmation. In production, returning restores the original form. The `32-discard` sample uses the wizard.

Main path: `00 → 12 → 14 → 15 → 16 → 17 → 03 → 04 → 01`. Reconfiguring an active connection starts with `11`. If VPN permission is already granted, `03` can be skipped. Disconnection follows `01 → 07 → 02`. Pending states do not advance on a timer in the mockup. Select the outcome in the scenario selector.

## Mapping to existing features

| Original feature | Android concept | Source and verified ranges |
| --- | --- | --- |
| Tray menu: Connect/Disconnect, Wizard, Contracts, Edit Config, Server Info, Statistics, About, Exit | Four main destinations and nested screens | `client/apps/tray/tray_application.cpp:216–249` |
| Connection, URL, virtual IP, four checks, errors and disconnection confirmation | 01–08, 38, 48 | `client/apps/tray/progress_window.cpp:33–104,166–212` |
| Reconnecting and uncertain status | 05, 08 | `client/src/core/runtime/status_snapshot.h:77–110`, progress UI |
| Disconnect gate, URL, secret, IP/access key, default contracts, Finish | 11–18 | `client/apps/tray/wizard_window.cpp:489–733` |
| HTTP headers: edit, reset and cancel | 13 | `client/apps/tray/http_headers_window.cpp:35–76` |
| Candidate contracts, enable, key, password-based change, pagination and saving | 19–22, 41 | `client/apps/tray/contracts_window.cpp:23–126,145–211,241–388` |
| Password → SHA-256, Default, Random | 21 | `client/apps/tray/password_sha_window.cpp:12–59,76–102` |
| JSON editor, Reset, Save and draft changes | 23–24, 32, 39, 42 | `client/apps/tray/editor_window.cpp:77–130,173–253,262–352` |
| Reconnect after saving | 25 | `client/apps/tray/tray_application.cpp:361–400` |
| Ten statistics fields, units and data freshness | 26–27, 43 | `client/apps/tray/statistics_model.h:15–26,119–143` |
| Online devices, raw information, refresh, auto refresh and pagination | 28–29, 44–45 | `client/apps/tray/server_info_window.cpp:31–106,146–294` |
| Online with a valid contract, online with an invalid contract, offline | 28 | `client/src/core/presentation/server_info_model.cpp:8–56` |
| Request/Response HTTP details, reconstructed and incomplete data | 30–31 | `client/apps/tray/http_error_detail_window.cpp:19–62` |
| Commit unknown, save verification, retry, invalid configuration, reset and session expiry | 33, 36, 40, 46–47 | Wizard render and contracts refresh above |
| About, Exit | 34–35 | Tray menu and `tray_application.cpp:606–642` |

The sample server information JSON uses the schema in `client/tests/linux/tray/server_info_fixture.h:44–86`. It is an independent synthetic diagnostic sample, not the source of the counters on the preceding sample screen.

Client addresses start at the network address + 2. A /24 network has 253 client addresses and 252 possible remote peers. The local `10.80.0.12` address is not offered as its own contract target. Examples start at `10.80.0.2`, not the reserved `.1` address. Verified in `client/src/core/protocol/ipv4.cpp:141–170` and `client/src/core/config/contracts_document.cpp:60–72`.

## Advanced settings

The complete JSON editor preserves access to actual parameters without inventing additional forms:

- `masterSetup`: URL, HTTP headers, secret, network, virtual IP, access key, TLS trust, automatic connection and wizard preferences.
- `runtime`: inbound and outbound queue limits, per-peer reservations, packet age, response and NDJSON line sizes, data record count, timeout and time synchronization interval.
- `communicationContracts`: target IP addresses and their `payloadKey` values.

The schema was verified in `client/src/core/config/config_validator.cpp:120–332`. The editor sample uses only known fields. It is not a production configuration file. Wizard preferences are optional and omitted from the short example.

## Mobile adaptations and boundaries

New design elements include first launch, bottom navigation, contract detail screens, hidden secrets, system VPN permission and a service notification. Screens identify these as adaptations or explain them in their notes. The existing functional model remains one configuration and one virtual network. The concept does not add countries, subscriptions, accounts, multiple profiles, ping, speed charts or traffic history.

VPN permission and notifications follow the [official Android VPN documentation](https://developer.android.com/develop/connectivity/vpn), specifically User experience, Prepare a service and Service lifecycle, checked on October 30, 2026. The system dialog wording is illustrative, not a guaranteed copy across device manufacturers. Leaving the screen is distinct from stopping the VPN service.

Always-on, blocking traffic outside the VPN, per-app selection and other Android networking modes are neither enabled nor promised by this design. Implementation would require separate decisions about supported Android versions, service lifecycle and secret storage. The existing `autoConnectOnStartup` setting must not be silently equated with Android always-on VPN.

The design makes no promise that the client protects all internet traffic. It describes connection to a virtual network.

## Visual rules

A light theme, brand blue, neutral surfaces and textual status labels. Green indicates confirmed success, amber indicates waiting or uncertainty and red indicates an error. Meaning never relies on color alone. The retained SVG branding comes from `assets/phpvpn*.svg`.

The reference phone is 412 CSS pixels wide. Content also adapts to widths of 360 and 320 pixels. Primary actions have a minimum height of 50 pixels. Inputs use native controls, labels and visible keyboard focus styling.

## Editing and regeneration

`generate.cjs` contains the screen definitions, navigation links and source notes. It generates HTML and `manifest.json`. `styles.css` provides shared styling. `interactions.js` contains only local presentation behavior. The English generator contains English source text and can be regenerated independently of the Czech version.

`screen-rules.json` holds the English rules for each screen. `generate-rules.cjs` produces `RULES.md` and the 49 individual Markdown files in `screens`. The main generator runs it automatically, keeping the catalog and screen-specific documentation links intact. Shared behavior remains in this README, while concrete screen rules and pitfalls are documented separately.

```powershell
node assets/android-en/generate.cjs
```

After changing the design, regenerate the PNG previews. They are actual Chromium screenshots, not AI-generated images. Validation results are saved in `qa-results.json`. Rendering used a helper installation of `playwright-core` outside the repository, without adding a dependency or package manifest to the project.

## Verification

Browser checks cover all 49 individual screens, JavaScript errors, the existence of every local link and image, and horizontal overflow at widths of 412, 360 and 320 pixels. Key screens were also inspected visually. Interaction checks cover the main wizard flow, secret reveal, automatic IP, contracts, bottom navigation and scenario switching. These checks validate the design files, not an actual VPN or Android runtime.

The original design was created directly on the active `macos` branch, without a worktree or additional agents. The working tree was clean when that original work began. The localization continues on the active branch and preserves unrelated server and distribution changes already present. The approved Czech directory was renamed from `assets/android` to `assets/android-cz`, and the English version is in `assets/android-en`.
