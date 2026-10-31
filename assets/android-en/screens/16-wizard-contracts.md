# 16 · Complete setup

[Open mockup](../16-wizard-contracts.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/16-wizard-contracts.png)

## Existing client

Default contracts are added only on Finish when permitted. The count depends on the network, local IP and existing default peers. The local IP is not its own remote peer.

## Proposed Android behavior

Show what already exists and what would be added before saving. Explain the predictable-key risk. Disable this option for unsupported configurations instead of pretending all networks can use it.

## Mockup limitations and pitfalls

The fixed counts illustrate 252 possible peers and two existing contracts. The switch does not recalculate the sample counts or write contracts.

## Sources

- `client/apps/tray/wizard_window.cpp:489–733`

## Related scenarios

[Configuration saved](17-wizard-saved.md) · [Save result unknown](33-save-unknown.md)
