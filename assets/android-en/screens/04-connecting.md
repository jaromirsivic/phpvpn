# 04 · Connecting

[Open mockup](../04-connecting.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/04-connecting.png)

## Existing client

The existing progress UI distinguishes URL, server secret, access key and communication contract checks. A failed or unavailable check must not be displayed as successful.

## Proposed Android behavior

Use clear text as well as status colors. Continue to expose cancellation while connecting. Move to Connected only after confirmation, not merely after the last local animation.

## Mockup limitations and pitfalls

There is no automatic timer. Use the scenario selector to inspect success, failure or cancellation. Cancel opens the disconnecting sample.

## Sources

- `client/apps/tray/progress_window.cpp:33–104,166–212`

## Related scenarios

[Connection](01-connected.md) · [Connection error](06-connection-error.md) · [Disconnecting](07-disconnecting.md)
