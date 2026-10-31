# 19 · Communication contracts

[Open mockup](../19-contracts.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/19-contracts.png)

## Existing client

Contract drafts are tracked by remote IP, including across pages. Keys require 64 lowercase hexadecimal characters. Changes are saved together and apply on the next connection.

## Proposed Android behavior

Replace the wide desktop table with a list and detail screens. Retain unsaved edits while navigating. Do not include the local address among remote targets.

## Mockup limitations and pitfalls

Two changed rows and four visible addresses are fixed examples. Save jumps to a confirmed-save scenario. Only two example pages are linked, not all 252 peers.

## Sources

- `client/apps/tray/contracts_window.cpp:23–126,241–388`

## Related scenarios

[Contract details](20-contract-detail.md) · [Invalid key](22-contract-invalid.md) · [Changes saved](25-reconnect-save.md) · [Save result unknown](33-save-unknown.md) · [More contracts](41-contracts-page-2.md)
