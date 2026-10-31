# 24 · Reset configuration

[Open mockup](../24-reset.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/24-reset.png)

## Existing client

Reset loads defaults into the editor draft. It must not be presented as an immediate write to the saved configuration. Saving remains a separate action.

## Proposed Android behavior

Explain which unsaved content will be lost and offer a clear way to keep it. A confirmation here is a proposed mobile presentation of that draft replacement.

## Mockup limitations and pitfalls

Reset in editor opens a schematic default excerpt. It does not modify the live configuration or carry the previous draft forward.

## Sources

- `client/apps/tray/editor_window.cpp:77–130,173–253,262–352`

## Related scenarios

[Default editor content](42-editor-default.md) · [Configuration editor](23-editor.md)
