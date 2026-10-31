# Screen rules and pitfalls

[Visual catalog](index.html) · [Design overview and source map](README.md)

Each of the 49 screen descriptions separates three kinds of information:

- **Existing client:** behavior supported by the referenced client code. Android-only screens explicitly identify that distinction.
- **Proposed Android behavior:** the intended mobile presentation and interaction. This is a design proposal, not a claim that an Android client already implements it.
- **Mockup limitations and pitfalls:** what the static example omits, which transitions are illustrative, and what must not be mistaken for real operation.

Shared navigation, sample-data conventions, platform boundaries and regeneration instructions are in [README.md](README.md). They are not repeated in full on every screen. The source reference on each screen provides the basis for its existing-client rules. These documents do not introduce additional protocol, security or service guarantees.

| Screen | Rules and pitfalls | Mockup |
| --- | --- | --- |
| 00 | [Welcome](screens/00-welcome.md) | [Open screen](00-welcome.html) |
| 01 | [Connection](screens/01-connected.md) | [Open screen](01-connected.html) |
| 02 | [Disconnected](screens/02-disconnected.md) | [Open screen](02-disconnected.html) |
| 03 | [VPN permission](screens/03-vpn-permission.md) | [Open screen](03-vpn-permission.html) |
| 04 | [Connecting](screens/04-connecting.md) | [Open screen](04-connecting.html) |
| 05 | [Reconnecting](screens/05-reconnecting.md) | [Open screen](05-reconnecting.html) |
| 06 | [Connection error](screens/06-connection-error.md) | [Open screen](06-connection-error.html) |
| 07 | [Disconnecting](screens/07-disconnecting.md) | [Open screen](07-disconnecting.html) |
| 08 | [Status unavailable](screens/08-unavailable.md) | [Open screen](08-unavailable.html) |
| 09 | [Background notification](screens/09-notification.md) | [Open screen](09-notification.html) |
| 10 | [Settings](screens/10-settings.md) | [Open screen](10-settings.html) |
| 11 | [Before setup](screens/11-disconnect-gate.md) | [Open screen](11-disconnect-gate.html) |
| 12 | [Server address](screens/12-wizard-url.md) | [Open screen](12-wizard-url.html) |
| 13 | [HTTP headers](screens/13-http-headers.md) | [Open screen](13-http-headers.html) |
| 14 | [Server secret](screens/14-wizard-secret.md) | [Open screen](14-wizard-secret.html) |
| 15 | [IP and access key](screens/15-wizard-identity.md) | [Open screen](15-wizard-identity.html) |
| 16 | [Complete setup](screens/16-wizard-contracts.md) | [Open screen](16-wizard-contracts.html) |
| 17 | [Configuration saved](screens/17-wizard-saved.md) | [Open screen](17-wizard-saved.html) |
| 18 | [Verification error](screens/18-wizard-error.md) | [Open screen](18-wizard-error.html) |
| 19 | [Communication contracts](screens/19-contracts.md) | [Open screen](19-contracts.html) |
| 20 | [Contract details](screens/20-contract-detail.md) | [Open screen](20-contract-detail.html) |
| 21 | [Key from password](screens/21-password-key.md) | [Open screen](21-password-key.html) |
| 22 | [Invalid key](screens/22-contract-invalid.md) | [Open screen](22-contract-invalid.html) |
| 23 | [Configuration editor](screens/23-editor.md) | [Open screen](23-editor.html) |
| 24 | [Reset configuration](screens/24-reset.md) | [Open screen](24-reset.html) |
| 25 | [Changes saved](screens/25-reconnect-save.md) | [Open screen](25-reconnect-save.html) |
| 26 | [Traffic statistics](screens/26-statistics.md) | [Open screen](26-statistics.html) |
| 27 | [Last known statistics](screens/27-statistics-stale.md) | [Open screen](27-statistics-stale.html) |
| 28 | [Who is online](screens/28-server.md) | [Open screen](28-server.html) |
| 29 | [Server data](screens/29-server-raw.md) | [Open screen](29-server-raw.html) |
| 30 | [HTTP response](screens/30-http-response.md) | [Open screen](30-http-response.html) |
| 31 | [HTTP request](screens/31-http-request.md) | [Open screen](31-http-request.html) |
| 32 | [Unsaved changes](screens/32-discard.md) | [Open screen](32-discard.html) |
| 33 | [Save result unknown](screens/33-save-unknown.md) | [Open screen](33-save-unknown.html) |
| 34 | [About](screens/34-about.md) | [Open screen](34-about.html) |
| 35 | [Closing the app](screens/35-exit.md) | [Open screen](35-exit.html) |
| 36 | [Configuration needs attention](screens/36-config-unavailable.md) | [Open screen](36-config-unavailable.html) |
| 37 | [VPN permission denied](screens/37-permission-denied.md) | [Open screen](37-permission-denied.html) |
| 38 | [Disconnection unconfirmed](screens/38-disconnect-unknown.md) | [Open screen](38-disconnect-unknown.html) |
| 39 | [JSON error](screens/39-editor-invalid.md) | [Open screen](39-editor-invalid.html) |
| 40 | [Reset wizard](screens/40-wizard-reset.md) | [Open screen](40-wizard-reset.html) |
| 41 | [More contracts](screens/41-contracts-page-2.md) | [Open screen](41-contracts-page-2.html) |
| 42 | [Default editor content](screens/42-editor-default.md) | [Open screen](42-editor-default.html) |
| 43 | [Loading statistics](screens/43-statistics-empty.md) | [Open screen](43-statistics-empty.html) |
| 44 | [No server records](screens/44-server-empty.md) | [Open screen](44-server-empty.html) |
| 45 | [Server refresh failed](screens/45-server-stale.md) | [Open screen](45-server-stale.html) |
| 46 | [Save checked](screens/46-save-checked.md) | [Open screen](46-save-checked.html) |
| 47 | [Wizard session expired](screens/47-session-expired.md) | [Open screen](47-session-expired.html) |
| 48 | [Connection checks](screens/48-connection-checks.md) | [Open screen](48-connection-checks.html) |
