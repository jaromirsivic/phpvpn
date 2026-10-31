# 47 · Wizard session expired

[Open mockup](../47-session-expired.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/47-session-expired.png)

## Existing client

sessionExpired is a separate wizard state. A new session is required before continuing, and obsolete verification results must not authorize a new save.

## Proposed Android behavior

Explain how to restart setup and offer inspection of current configuration, especially after an uncertain save or service restart.

## Mockup limitations and pitfalls

Reopen wizard navigates to the URL sample. It does not establish a service session or restore a previous draft.

## Sources

- `client/apps/tray/wizard_window.cpp:489–733`

## Related scenarios

[Server address](12-wizard-url.md) · [Configuration editor](23-editor.md)
