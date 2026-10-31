# 15 · IP and access key

[Open mockup](../15-wizard-identity.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/15-wizard-identity.png)

## Existing client

The identity step supports a virtual IP, automatic IP suggestion and a custom or default access key. When no suggestion is available, the user can enter an administrator-assigned address.

## Proposed Android behavior

Keep IP and access key distinct from the server secret. Disable the corresponding manual input when its automatic/default option is selected. Do not claim that a suggested address has been verified before the check completes.

## Mockup limitations and pitfalls

The IP and key are samples. Toggling automatic IP only enables or disables the field, and no address availability or key validity is checked.

## Sources

- `client/apps/tray/wizard_window.cpp:489–733`

## Related scenarios

[Complete setup](16-wizard-contracts.md) · [Verification error](18-wizard-error.md)
