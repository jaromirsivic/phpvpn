# 23 · Configuration editor

[Open mockup](../23-editor.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/23-editor.png)

## Existing client

The full editor retains access to masterSetup, runtime and communicationContracts. The validator checks known fields and their constraints. Saving and resetting are distinct operations.

## Proposed Android behavior

Preserve the complete document in a scrollable editor. Keep secrets out of incidental status text. Do not assume that Android uses the desktop configuration path.

## Mockup limitations and pitfalls

The sample is not a production configuration. Optional wizard preferences are omitted. Save does not validate or persist edits and opens the saved-while-connected scenario.

## Sources

- `client/apps/tray/editor_window.cpp:77–130,173–253,262–352`
- `client/src/core/config/config_validator.cpp:120–332`

## Related scenarios

[Reset configuration](24-reset.md) · [Changes saved](25-reconnect-save.md) · [JSON error](39-editor-invalid.md) · [Save result unknown](33-save-unknown.md) · [Unsaved changes](32-discard.md)
