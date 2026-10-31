# 12 · Server address

[Open mockup](../12-wizard-url.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/12-wizard-url.png)

## Existing client

A server URL supports HTTP or HTTPS and must not contain user information, a query or a fragment. The wizard verifies the endpoint before the next authentication step. Finish performs the eventual save.

## Proposed Android behavior

Keep URL input and optional HTTP headers separate. Distinguish a malformed URL from a transport timeout. Do not mark a valid URL syntactically invalid just because the network is unavailable.

## Mockup limitations and pitfalls

Verify opens a prepared successful result regardless of typed input. The URL is a sample and is never contacted.

## Sources

- `client/apps/tray/wizard_window.cpp:489–733`

## Related scenarios

[Server secret](14-wizard-secret.md) · [HTTP headers](13-http-headers.md) · [Verification error](18-wizard-error.md) · [Configuration needs attention](36-config-unavailable.md) · [Reset wizard](40-wizard-reset.md)
