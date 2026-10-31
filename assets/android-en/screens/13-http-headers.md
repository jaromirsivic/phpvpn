# 13 · HTTP headers

[Open mockup](../13-http-headers.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/13-http-headers.png)

## Existing client

The headers editor belongs to the wizard draft and offers accept, reset and cancel. Cancel preserves the previously accepted value rather than committing the current edit.

## Proposed Android behavior

Use multiline editing with one header per line. Preserve the full value without silently truncating long headers. Treat possible sensitive header content accordingly.

## Mockup limitations and pitfalls

Reset loads an illustrative header value, not a guaranteed copy of the service defaults. Use in wizard does not transfer text between these independent HTML pages.

## Sources

- `client/apps/tray/http_headers_window.cpp:35–76`

## Related scenarios

[Server address](12-wizard-url.md)
