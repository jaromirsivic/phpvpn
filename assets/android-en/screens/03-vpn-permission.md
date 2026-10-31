# 03 · VPN permission

[Open mockup](../03-vpn-permission.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/03-vpn-permission.png)

## Existing client

The desktop connection flow has no Android permission dialog. This is an additional platform step, not server authentication.

## Proposed Android behavior

Use the system-owned VPN consent flow. Cancellation must not be shown as a server error or as a successful connection. Android controls the actual wording and appearance.

## Mockup limitations and pitfalls

Allow and Cancel only navigate to prepared pages. The HTML dialog cannot grant system permission.

## Sources

- Android Developers: VPN, User experience / Prepare a service

- [Android VPN documentation](https://developer.android.com/develop/connectivity/vpn)

## Related scenarios

[Connecting](04-connecting.md) · [VPN permission denied](37-permission-denied.md)
