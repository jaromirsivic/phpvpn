# 26 · Traffic statistics

[Open mockup](../26-statistics.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/26-statistics.png)

## Existing client

The screen contains ten read-only fields: four IPv4 packet/byte counters, three HTTP body-size values and three data-poll counts. HTTP body size is not the same measurement as IPv4 traffic.

## Proposed Android behavior

Group counters by layer and show their connection context. Do not infer speed, ping or historical charts from cumulative counters alone.

## Mockup limitations and pitfalls

All values and the connection duration are fixed samples. There is no live polling, reset control or historical data store.

## Sources

- `client/apps/tray/statistics_model.h:15–26,119–143`

## Related scenarios

[Last known statistics](27-statistics-stale.md) · [Loading statistics](43-statistics-empty.md)
