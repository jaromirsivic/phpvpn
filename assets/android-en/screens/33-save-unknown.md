# 33 · Save result unknown

[Open mockup](../33-save-unknown.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/33-save-unknown.png)

## Existing client

An unknown save result is not failure or success. The wizard checks a receipt, while the contracts workflow can inspect current configuration. Retry is exposed only when the relevant controller permits it.

## Proposed Android behavior

Preserve the draft and clearly distinguish inspection from another write. Do not silently overwrite configuration after a service restart or uncertain response.

## Mockup limitations and pitfalls

Verify save opens one prepared contracts outcome. This is not a universal recovery algorithm for the wizard or editor. No receipt or configuration is read.

## Sources

- `client/apps/tray/wizard_window.cpp:489–733`
- `client/apps/tray/contracts_window.cpp:23–126,241–388`

## Related scenarios

[Save checked](46-save-checked.md) · [Configuration saved](17-wizard-saved.md) · [Configuration editor](23-editor.md)
