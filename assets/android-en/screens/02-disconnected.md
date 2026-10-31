# 02 · Disconnected

[Open mockup](../02-disconnected.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/02-disconnected.png)

## Existing client

Connect uses the current configuration. The server and credentials are checked during connection, so a stored configuration is not proof that the server is currently reachable.

## Proposed Android behavior

Before starting a real Android connection, request system VPN consent when required. If consent is already available, skip the illustrative permission screen.

## Mockup limitations and pitfalls

Connect always opens the consent example. This does not mean Android should ask on every connection.

## Sources

- `client/apps/tray/progress_window.cpp:33–104,166–212`

## Related scenarios

[VPN permission](03-vpn-permission.md) · [Connecting](04-connecting.md) · [Welcome](00-welcome.md)
