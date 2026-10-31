# 05 · Reconnecting

[Open mockup](../05-reconnecting.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/05-reconnecting.png)

## Existing client

The runtime distinguishes reconnecting from connected. Recovery does not by itself confirm that the virtual network is available.

## Proposed Android behavior

Explain that recovery is in progress and keep the disconnect action available. Do not promise a kill switch or blocking of non-VPN traffic, neither of which is established by this design.

## Mockup limitations and pitfalls

The sample does not retry a network request. Success and failure are separate scenario pages, and no retry interval is specified.

## Sources

- `client/src/core/runtime/status_snapshot.h:77–110`
- `client/apps/tray/progress_window.cpp:33–104,166–212`

## Related scenarios

[Connection](01-connected.md) · [Connection error](06-connection-error.md) · [Status unavailable](08-unavailable.md)
