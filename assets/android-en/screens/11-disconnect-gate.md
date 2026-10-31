# 11 · Before setup

[Open mockup](../11-disconnect-gate.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/11-disconnect-gate.png)

## Existing client

The wizard disconnectGate prevents setup checks from proceeding until VPN disconnection is known. An already disconnected client can proceed without disconnecting again.

## Proposed Android behavior

Explain the interruption before the action. Proceed to URL setup only after confirmed disconnection. Preserve an exit route that leaves the existing connection in place.

## Mockup limitations and pitfalls

Disconnect and continue opens the URL sample immediately. The intermediate wait is omitted here, but the unknown outcome is available as a scenario.

## Sources

- `client/apps/tray/wizard_window.cpp:489–733`

## Related scenarios

[Server address](12-wizard-url.md) · [Disconnection unconfirmed](38-disconnect-unknown.md)
