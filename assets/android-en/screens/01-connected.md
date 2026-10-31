# 01 · Connection

[Open mockup](../01-connected.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/01-connected.png)

## Existing client

Show Connected only for a confirmed connected state. Duration and traffic counters come from the client status. Opening connection details must not start a new connection.

## Proposed Android behavior

Keep Disconnect prominent and route the four bottom destinations consistently. Describe access to the virtual network, not guaranteed protection for all internet traffic.

## Mockup limitations and pitfalls

The duration and counters are fixed examples. Other pages are independent states, so navigating away does not preserve a simulated live connection.

## Sources

- `client/apps/tray/progress_window.cpp:33–104,166–212`
- `client/apps/tray/statistics_model.h:15–26,119–143`

## Related scenarios

[Disconnected](02-disconnected.md) · [Connecting](04-connecting.md) · [Reconnecting](05-reconnecting.md) · [Connection error](06-connection-error.md) · [Disconnecting](07-disconnecting.md) · [Status unavailable](08-unavailable.md) · [Background notification](09-notification.md)
