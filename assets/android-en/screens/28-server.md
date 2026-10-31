# 28 · Who is online

[Open mockup](../28-server.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/28-server.png)

## Existing client

The server view distinguishes online with a valid contract, online with an invalid contract and offline. Online is evaluated within 60 seconds of the server-generated timestamp. The local client is omitted.

## Proposed Android behavior

Show both presence and contract validity, rather than treating online as permission to communicate. Preserve manual and automatic refresh, and use pagination for longer lists.

## Mockup limitations and pitfalls

Three peers and summary counts are fixed. Auto refresh only toggles visually, and Refresh shows a simulation notice. The raw-data page is a separate sample, not the source of this list.

## Sources

- `client/apps/tray/server_info_window.cpp:31–106,146–236,237–294`
- `client/src/core/presentation/server_info_model.cpp:8–56`

## Related scenarios

[Server data](29-server-raw.md) · [No server records](44-server-empty.md) · [Server refresh failed](45-server-stale.md)
