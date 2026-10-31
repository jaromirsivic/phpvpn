# 08 · Status unavailable

[Open mockup](../08-unavailable.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/08-unavailable.png)

## Existing client

Unavailable status cannot establish whether the VPN is connected or disconnected. Previously received statistics may be retained with a stale-data notice.

## Proposed Android behavior

State the uncertainty explicitly and distinguish the last known state from current status. A new status read should lead to whichever state is actually confirmed.

## Mockup limitations and pitfalls

Check status again links to a connecting example. That link is not a rule that an unknown state should trigger a fresh connection.

## Sources

- `client/apps/tray/progress_window.cpp:33–104,166–212`
- `client/apps/tray/statistics_model.h:15–26,119–143`

## Related scenarios

[Connection](01-connected.md) · [Disconnected](02-disconnected.md) · [Disconnection unconfirmed](38-disconnect-unknown.md)
