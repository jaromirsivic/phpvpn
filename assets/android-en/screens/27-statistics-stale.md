# 27 · Last known statistics

[Open mockup](../27-statistics-stale.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/27-statistics-stale.png)

## Existing client

When status is unavailable after valid data was received, the statistics model retains the last counters and marks them stale. No prior sample is different from zero traffic.

## Proposed Android behavior

Keep a visible stale-data notice near the values. Do not animate retained values as though they are live. Use the waiting state when no valid sample has ever arrived.

## Mockup limitations and pitfalls

The counters match a prepared previous snapshot. They do not age or refresh automatically.

## Sources

- `client/apps/tray/statistics_model.h:15–26,119–143`

## Related scenarios

[Traffic statistics](26-statistics.md) · [Loading statistics](43-statistics-empty.md) · [Status unavailable](08-unavailable.md)
