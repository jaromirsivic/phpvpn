# 09 · Background notification

[Open mockup](../09-notification.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/09-notification.png)

## Existing client

The desktop tray provides access to connection controls. The Android notification is a proposed platform replacement, not an existing desktop screen.

## Proposed Android behavior

An active service notification should open the app status and offer an appropriate disconnect action. Remove it when the service becomes inactive. Do not equate closing the activity with stopping VPN.

## Mockup limitations and pitfalls

This is an HTML representation, not a system notification. Layout and available controls can vary with Android. Always-on behavior is outside this concept.

## Sources

- Android Developers: VPN, User experience

- [Android VPN documentation](https://developer.android.com/develop/connectivity/vpn)

## Related scenarios

[Connection](01-connected.md) · [Disconnecting](07-disconnecting.md)
