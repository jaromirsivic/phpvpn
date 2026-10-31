# 20 · Contract details

[Open mockup](../20-contract-detail.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/20-contract-detail.png)

## Existing client

A contract can be enabled or disabled. In the client, a disabled contract has a read-only key, and the password-key action is unavailable. Editing the draft is separate from saving it.

## Proposed Android behavior

Use a detail screen to avoid a horizontally scrolling table. Explain that both peers need matching keys. Applying a detail should return to the shared draft, not silently commit the whole configuration.

## Mockup limitations and pitfalls

The enable switch changes its appearance only. It does not enforce the actual read-only rule. Apply to draft returns to a fixed list without carrying the entered value.

## Sources

- `client/apps/tray/contracts_window.cpp:23–126,241–388`

## Related scenarios

[Key from password](21-password-key.md) · [Invalid key](22-contract-invalid.md) · [Communication contracts](19-contracts.md)
