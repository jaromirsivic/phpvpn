# 14 · Server secret

[Open mockup](../14-wizard-secret.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/14-wizard-secret.png)

## Existing client

The wizard supports a custom server secret or the default secret. Selecting the default disables custom entry. A predictable default is distinct from a private administrator-provided secret.

## Proposed Android behavior

Hide sensitive input initially and let the user reveal it deliberately. Do not repeat the secret in an error message. Advance only after the actual verification succeeds.

## Mockup limitations and pitfalls

The default switch disables the field and Show/Hide works locally. Verify navigates to the next prepared page without performing authentication.

## Sources

- `client/apps/tray/wizard_window.cpp:489–733`

## Related scenarios

[IP and access key](15-wizard-identity.md) · [Verification error](18-wizard-error.md) · [Unsaved changes](32-discard.md)
