# 30 · HTTP response

[Open mockup](../30-http-response.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/30-http-response.png)

## Existing client

HTTP diagnostics may contain an incomplete response or incomplete system headers. The displayed size refers to content actually received, not a presumed full response.

## Proposed Android behavior

Label the response scope and keep it read-only. Keep diagnostics tied to their originating attempt rather than presenting old data as the latest failure.

## Mockup limitations and pitfalls

The HTTP 503 response is illustrative. No packet capture or request is performed, and its relationship to other error examples is not causal.

## Sources

- `client/apps/tray/http_error_detail_window.cpp:19–62`

## Related scenarios

[HTTP request](31-http-request.md) · [Connection error](06-connection-error.md)
