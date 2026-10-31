# 42 · Default editor content

[Open mockup](../42-editor-default.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/42-editor-default.png)

## Existing client

Editor defaults remain a draft until saving. The service supplies the complete default document in the actual client.

## Proposed Android behavior

Make the unsaved state explicit and provide a route to complete network details. Do not present a schematic excerpt as a ready-to-import configuration.

## Mockup limitations and pitfalls

The displayed JSON is intentionally incomplete. Continue and the wizard link do not transfer edits or reproduce the exact default document.

## Sources

- `client/src/core/config/config_defaults.cpp:5–24`
- `client/apps/tray/editor_window.cpp:77–130,173–253,262–352`

## Related scenarios

[Configuration editor](23-editor.md) · [Server address](12-wizard-url.md)
