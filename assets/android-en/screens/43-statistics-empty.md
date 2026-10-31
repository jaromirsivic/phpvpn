# 43 · Loading statistics

[Open mockup](../43-statistics-empty.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/43-statistics-empty.png)

## Existing client

Before any valid service-status sample arrives, the statistics model uses missing-value placeholders. These are not zero counters.

## Proposed Android behavior

Show a waiting explanation and replace placeholders only when a valid result arrives. Keep this distinct from stale values retained after a later failure.

## Mockup limitations and pitfalls

The dashes remain static. Use the scenario selector to inspect valid data or unavailable status.

## Sources

- `client/apps/tray/statistics_model.h:15–26,119–143`

## Related scenarios

[Traffic statistics](26-statistics.md) · [Status unavailable](08-unavailable.md)
