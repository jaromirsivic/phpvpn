# 07 · Disconnecting

[Open mockup](../07-disconnecting.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/07-disconnecting.png)

## Existing client

Disconnection remains pending until the service confirms the result. A timeout can lead to Disconnect not confirmed rather than a success state.

## Proposed Android behavior

Keep the progress state distinct from Disconnected. Do not dismiss uncertainty just because the screen is closed or the app is backgrounded.

## Mockup limitations and pitfalls

The disabled action indicates a pending example. Select the confirmed or unknown result in the scenario selector. No service is stopped.

## Sources

- `client/apps/tray/progress_window.cpp:33–104,166–212`

## Related scenarios

[Disconnected](02-disconnected.md) · [Disconnection unconfirmed](38-disconnect-unknown.md)
