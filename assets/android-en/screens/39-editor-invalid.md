# 39 · JSON error

[Open mockup](../39-editor-invalid.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/39-editor-invalid.png)

## Existing client

Invalid JSON cannot be accepted as a valid configuration. Actual error detail must come from the parser or validator. Do not promise a line location that the backend does not return.

## Proposed Android behavior

Keep the document available for correction and preserve edits. Distinguish syntax errors from valid JSON that violates configuration constraints.

## Mockup limitations and pitfalls

The missing separator and error message are fixed. Editing the excerpt does not revalidate it or enable a real save.

## Sources

- `client/src/core/config/config_validator.cpp:120–332`
- `client/apps/tray/editor_window.cpp:77–130,173–253,262–352`

## Related scenarios

[Configuration editor](23-editor.md)
