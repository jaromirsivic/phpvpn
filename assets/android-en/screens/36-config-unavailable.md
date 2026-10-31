# 36 · Configuration needs attention

[Open mockup](../36-config-unavailable.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/36-config-unavailable.png)

## Existing client

The wizard distinguishes unavailable or unusable configuration from a valid draft. Editing is relevant only when the document can be read as supported text. Reset still waits for final saving.

## Proposed Android behavior

Explain the actual classification rather than showing one generic destructive reset. Offer repair when possible and defaults as a deliberate choice.

## Mockup limitations and pitfalls

This is one generic invalid-document example. The links do not detect missing files, permissions or text encoding, and reset does not read real defaults.

## Sources

- `client/apps/tray/wizard_window.cpp:489–733`
- `client/apps/tray/editor_window.cpp:77–130,173–253,262–352`

## Related scenarios

[Configuration editor](23-editor.md) · [Reset wizard](40-wizard-reset.md)
