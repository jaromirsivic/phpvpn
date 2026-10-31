# 21 · Key from password

[Open mockup](../21-password-key.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/21-password-key.png)

## Existing client

The password-key tool derives a SHA-256 communication key and provides Default and Random actions. It is scoped to the selected contract, not the server secret or access key.

## Proposed Android behavior

Keep the target IP visible. Show the derived key separately from the password. Applying the key should update only the intended contract draft.

## Mockup limitations and pitfalls

No hashing or random generation is implemented. The shown hash is illustrative and does not correspond to the entered password. Default and Random show explanatory messages.

## Sources

- `client/apps/tray/password_sha_window.cpp:12–59`

## Related scenarios

[Contract details](20-contract-detail.md)
