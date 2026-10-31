# 45 · Server refresh failed

[Open mockup](../45-server-stale.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/45-server-stale.png)

## Existing client

A failed refresh retains the previous successful server information. If no previous valid data exists, the client cannot present a cached list as though it had received one.

## Proposed Android behavior

Mark the displayed snapshot as stale and keep its refresh time visible. Avoid implying that a last-known online device is online now.

## Mockup limitations and pitfalls

The timestamps and devices are fixed. Retry opens the prepared successful page instead of issuing a request.

## Sources

- `client/apps/tray/server_info_window.cpp:31–106,146–236,237–294`

## Related scenarios

[Who is online](28-server.md) · [No server records](44-server-empty.md)
