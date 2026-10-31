# 31 · HTTP request

[Open mockup](../31-http-request.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/31-http-request.png)

## Existing client

The client may reconstruct a request from available application headers. An unknown HTTP version can be displayed as HTTP/?. Reconstruction is not a complete network capture.

## Proposed Android behavior

State the data provenance and keep the request separate from the response. Avoid implying that unavailable system headers were observed.

## Mockup limitations and pitfalls

The body placeholder and headers are samples. Nothing is sent, and tabs only navigate between prepared pages.

## Sources

- `client/apps/tray/http_error_detail_window.cpp:19–62`

## Related scenarios

[HTTP response](30-http-response.md) · [Connection error](06-connection-error.md)
