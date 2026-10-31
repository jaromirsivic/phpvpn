# 06 · Connection error

[Open mockup](../06-connection-error.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/06-connection-error.png)

## Existing client

Transport failure and input validation are separate. A timeout is not evidence of a wrong secret. HTTP error details are available only if diagnostic data was captured.

## Proposed Android behavior

Explain the failure and provide a retry or a relevant configuration route. Hide the HTTP details action when no such data exists in a real result.

## Mockup limitations and pitfalls

The timeout screen links to an independent sample HTTP 503 response. These are illustrative states, not one captured request. No real diagnostics are collected.

## Sources

- `client/apps/tray/progress_window.cpp:33–104,166–212`
- `client/apps/tray/http_error_detail_window.cpp:19–62`

## Related scenarios

[Connecting](04-connecting.md) · [HTTP response](30-http-response.md) · [Server address](12-wizard-url.md)
