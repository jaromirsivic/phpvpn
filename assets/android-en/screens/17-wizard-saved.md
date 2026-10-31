# 17 · Configuration saved

[Open mockup](../17-wizard-saved.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/17-wizard-saved.png)

## Existing client

Successful configuration saving and establishing a VPN connection are separate outcomes. An unconfirmed save belongs to the unknown-result flow, not this success screen.

## Proposed Android behavior

Offer an explicit choice to connect now or later. A subsequent Android connection may still need system consent and server verification.

## Mockup limitations and pitfalls

The saved message is a prepared scenario. No file was written, and the Connect action does not establish a VPN.

## Sources

- `client/apps/tray/wizard_window.cpp:489–733`

## Related scenarios

[VPN permission](03-vpn-permission.md) · [Disconnected](02-disconnected.md) · [Communication contracts](19-contracts.md)
