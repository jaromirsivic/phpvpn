# 44 · No server records

[Open mockup](../44-server-empty.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/44-server-empty.png)

## Existing client

A valid server response can yield no remote-client rows, because the local client is omitted. An empty list is distinct from a failed or unverifiable response.

## Proposed Android behavior

Explain the empty result and retain refresh. Do not invent devices or use a connection-error message for a valid empty list.

## Mockup limitations and pitfalls

No response is parsed. Refresh only shows a sample message and does not populate the list.

## Sources

- `client/apps/tray/server_info_window.cpp:31–106,146–236,237–294`

## Related scenarios

[Who is online](28-server.md) · [Server refresh failed](45-server-stale.md)
