# 10 · Settings

[Open mockup](../10-settings.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/10-settings.png)

## Existing client

The hub maps the existing wizard, contracts, configuration editor, server information, statistics, About and Exit capabilities. Advanced parameters remain available in JSON.

## Proposed Android behavior

Keep frequent destinations in bottom navigation and configuration tasks in Settings. Do not invent profile switching, country selection or account management.

## Mockup limitations and pitfalls

The wizard link demonstrates the active-connection gate. A real disconnected session should go directly to setup. This page does not read live connection status.

## Sources

- `client/apps/tray/tray_application.cpp:216–249,361–400,606–642`

## Related scenarios

[Before setup](11-disconnect-gate.md) · [Communication contracts](19-contracts.md) · [Configuration editor](23-editor.md) · [About](34-about.md) · [Closing the app](35-exit.md)
