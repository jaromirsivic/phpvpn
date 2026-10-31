# 40 · Reset wizard

[Open mockup](../40-wizard-reset.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/40-wizard-reset.png)

## Existing client

Wizard reset discards the current setup draft and returns to defaults. It does not commit those defaults before Finish.

## Proposed Android behavior

State that draft progress will be lost and that the saved configuration remains unchanged. Return to the first setup step after a confirmed reset.

## Mockup limitations and pitfalls

Both choices lead to the prefilled URL example. The demonstration does not load the real default placeholder or preserve previous edits.

## Sources

- `client/apps/tray/wizard_window.cpp:489–733`

## Related scenarios

[Server address](12-wizard-url.md)
