# 37 · VPN permission denied

[Open mockup](../37-permission-denied.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/37-permission-denied.png)

## Existing client

The denial belongs to Android permission handling, not the desktop server-verification workflow. It must not be treated as bad credentials.

## Proposed Android behavior

Leave the configuration intact and offer a deliberate request to try permission again, or remain disconnected. Only the system can grant permission.

## Mockup limitations and pitfalls

Request permission again reopens the illustrative HTML dialog. No operating-system permission state exists in the mockup.

## Sources

- Android Developers: VPN, Prepare a service

- [Android VPN documentation](https://developer.android.com/develop/connectivity/vpn)

## Related scenarios

[VPN permission](03-vpn-permission.md) · [Disconnected](02-disconnected.md)
