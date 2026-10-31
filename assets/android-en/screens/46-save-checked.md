# 46 · Save checked

[Open mockup](../46-save-checked.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/46-save-checked.png)

## Existing client

The contracts workflow exposes Save draft again only when its state permits repetition after checking current configuration. A confirmed prior write should lead to success instead of resubmission.

## Proposed Android behavior

Distinguish unchanged, already-saved and conflicting results. Do not quietly overwrite a document changed elsewhere. Let the user inspect the current configuration.

## Mockup limitations and pitfalls

This page illustrates only the draft-not-saved branch. It performs no comparison, revision check or conflict resolution.

## Sources

- `client/apps/tray/contracts_window.cpp:23–126,241–388`

## Related scenarios

[Changes saved](25-reconnect-save.md) · [Configuration editor](23-editor.md) · [Configuration saved](17-wizard-saved.md)
