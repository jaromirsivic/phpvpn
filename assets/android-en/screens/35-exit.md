# 35 · Closing the app

[Open mockup](../35-exit.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/35-exit.png)

## Existing client

Desktop Exit exists, but it is not the same operation as leaving an Android activity. Mobile activity and VPN service lifecycles are distinct.

## Proposed Android behavior

Leaving the screen can retain a connection accessible through a notification. Stopping VPN should wait for confirmed disconnection. Do not pretend that closing a screen force-stops the process.

## Mockup limitations and pitfalls

The two choices open notification or disconnecting samples. They do not background an Android app or stop a running service. Always-on behavior is not modeled.

## Sources

- `client/apps/tray/tray_application.cpp:216–249,361–400,606–642`
- Android Developers: VPN, Service lifecycle

- [Android VPN documentation](https://developer.android.com/develop/connectivity/vpn)

## Related scenarios

[Background notification](09-notification.md) · [Disconnecting](07-disconnecting.md)
