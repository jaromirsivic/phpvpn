# 32 · Unsaved changes

[Open mockup](../32-discard.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/32-discard.png)

## Existing client

The existing editor and wizard lifecycle distinguish unsaved drafts from saved configuration. Closing a dirty draft requires a decision rather than silently losing it.

## Proposed Android behavior

Use the same decision pattern for system Back and closing a form. Keep editing must return to the originating form and preserve its draft in a real implementation.

## Mockup limitations and pitfalls

This variant always returns to the URL wizard sample. It is not a working generic back stack, and values are not retained across pages.

## Sources

- `client/apps/tray/editor_window.cpp:77–130,173–253,262–352`
- `client/apps/tray/contracts_window.cpp:23–126,241–388`

## Related scenarios

[Server address](12-wizard-url.md) · [Settings](10-settings.md)
