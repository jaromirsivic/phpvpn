# 29 · Server data

[Open mockup](../29-server-raw.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/29-server-raw.png)

## Existing client

The raw server view is read-only and retains the original field names. The displayed response belongs to a verified server-information result, not an editable configuration document.

## Proposed Android behavior

Provide a readable, scrollable raw view without rewriting protocol keys. Keep its freshness and error handling aligned with the device view in an implementation.

## Mockup limitations and pitfalls

The JSON is an independent test-fixture-style sample. The synthetic serverVersion is not a released version, and this payload is not the data behind the preceding device list.

## Sources

- `client/apps/tray/server_info_window.cpp:31–106,146–236,237–294`
- `client/tests/linux/tray/server_info_fixture.h:44–86`

## Related scenarios

[Who is online](28-server.md) · [Server refresh failed](45-server-stale.md)
