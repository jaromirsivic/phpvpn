# 41 · More contracts

[Open mockup](../41-contracts-page-2.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/41-contracts-page-2.png)

## Existing client

Contract drafts are retained by target IP across pagination. A save covers the shared draft, not just the visible page.

## Proposed Android behavior

Preserve the changed-row count while switching pages. Do not lose edits when rows leave the viewport. Keep the local IP out of candidate peers.

## Mockup limitations and pitfalls

This is only the second sample page. It returns to the same fixed detail example for any row and does not implement all 252 addresses.

## Sources

- `client/apps/tray/contracts_window.cpp:23–126,241–388`

## Related scenarios

[Communication contracts](19-contracts.md) · [Contract details](20-contract-detail.md) · [Changes saved](25-reconnect-save.md)
