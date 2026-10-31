# 22 · Invalid key

[Open mockup](../22-contract-invalid.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/22-contract-invalid.png)

## Existing client

An invalid contract key blocks saving. Validation identifies the first invalid peer, and the required format is exactly 64 lowercase hexadecimal characters.

## Proposed Android behavior

Attach the error to the affected input, identify the target peer and preserve other draft changes. Provide the password-key route as an alternative way to obtain a valid key.

## Mockup limitations and pitfalls

The six-character example is static. Typing does not update the error, and Back to editing opens a separate prepared page.

## Sources

- `client/apps/tray/contracts_window.cpp:23–126,241–388`

## Related scenarios

[Contract details](20-contract-detail.md) · [Key from password](21-password-key.md)
