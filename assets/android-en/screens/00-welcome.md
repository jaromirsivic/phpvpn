# 00 · Welcome

[Open mockup](../00-welcome.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/00-welcome.png)

## Existing client

The existing wizard supplies the server, secret, identity and contract setup steps. This welcome screen is new, not a separate account or profile system.

## Proposed Android behavior

Offer setup and the advanced editor as entry points. Explain that credentials come from the network administrator. Do not imply that PHPVPN provides a hosted VPN subscription.

## Mockup limitations and pitfalls

The setup link opens the prefilled URL sample. No configuration file is inspected on launch.

## Sources

- `client/apps/tray/wizard_window.cpp:489–733`

## Related scenarios

[Server address](12-wizard-url.md) · [Configuration editor](23-editor.md)
