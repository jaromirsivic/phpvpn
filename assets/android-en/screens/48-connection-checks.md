# 48 · Connection checks

[Open mockup](../48-connection-checks.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/48-connection-checks.png)

## Existing client

These are the four checks shown by connection progress. Successful results describe that connection attempt, not a new independent verification request.

## Proposed Android behavior

Provide a read-only detail with a clear route back to status. Opening it should not reconnect or reset the connection.

## Mockup limitations and pitfalls

All four checks are fixed as successful. The connecting state in the scenario selector is an alternative example, not an action triggered by viewing details.

## Sources

- `client/apps/tray/progress_window.cpp:33–104,166–212`

## Related scenarios

[Connection](01-connected.md) · [Connecting](04-connecting.md)
