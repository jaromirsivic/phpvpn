# 38 · Disconnection unconfirmed

[Open mockup](../38-disconnect-unknown.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/38-disconnect-unknown.png)

## Existing client

The progress UI can report Disconnect not confirmed when a request or status read does not establish completion. The connection may still be active.

## Proposed Android behavior

Offer another disconnect attempt or a status check without claiming success. Keep the unknown state visible until authoritative status is available.

## Mockup limitations and pitfalls

Both actions navigate to pending examples. No status query is performed and no timeout is simulated.

## Sources

- `client/apps/tray/progress_window.cpp:33–104,166–212`

## Related scenarios

[Disconnecting](07-disconnecting.md) · [Status unavailable](08-unavailable.md) · [Disconnected](02-disconnected.md)
