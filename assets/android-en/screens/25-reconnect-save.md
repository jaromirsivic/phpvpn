# 25 · Changes saved

[Open mockup](../25-reconnect-save.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/25-reconnect-save.png)

## Existing client

Saved changes do not change the existing connection immediately. The desktop offers reconnect after saving, and a new connection must follow completion of the disconnect operation.

## Proposed Android behavior

Offer reconnect now or use the configuration next time. If VPN is inactive, offer Connect instead. Do not report the new configuration as active before reconnect succeeds.

## Mockup limitations and pitfalls

Reconnect links directly to the connecting sample. The required disconnect phase is not executed. Choosing later returns to a fixed connected sample.

## Sources

- `client/apps/tray/tray_application.cpp:361–400`
- `client/apps/tray/contracts_window.cpp:23–126,241–388`

## Related scenarios

[Connecting](04-connecting.md) · [Connection](01-connected.md) · [Disconnecting](07-disconnecting.md)
