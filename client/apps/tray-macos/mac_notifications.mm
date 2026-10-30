#include "mac_notifications.h"

#import <UserNotifications/UserNotifications.h>

#include <string>

#include "core/config/config_document.h"

// Shows the notice as a banner also while the tray is the active application.
@interface PVNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

@implementation PVNotificationDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions options))completionHandler {
    (void)center;
    (void)notification;
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList);
}
@end

namespace phpvpn::tray {
Notifications::Notifications() : alive_(std::make_shared<bool>(true)) {}

Notifications::~Notifications() { detach(); }

void Notifications::detach() {
    *alive_ = false;
    if (delegate_ && [[NSBundle mainBundle] bundleIdentifier]) {
        UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
        if (center.delegate == delegate_) center.delegate = nil;
    }
    delegate_ = nil;
}

bool Notifications::showInformation(std::string_view title, std::string_view body) {
    if (!*alive_ || title.empty() || body.empty() || !isStrictUtf8(title) || !isStrictUtf8(body)) return false;
    auto alive = alive_;
    const std::string titleText(title), bodyText(body);
    // The same notice as an alert, on the main run loop.
    auto fallback = [alive, titleText, bodyText] {
        mac::post([alive, titleText, bodyText] {
            if (*alive) mac::showMessage(bodyText, titleText, mac::DialogIcon::information);
        });
    };
    // UNUserNotificationCenter requires a bundle identifier (an application bundle).
    if (![[NSBundle mainBundle] bundleIdentifier]) {
        fallback();
        return true;
    }
    UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
    if (!delegate_) delegate_ = [[PVNotificationDelegate alloc] init];
    center.delegate = delegate_;
    NSString* heading = mac::ns(titleText);
    NSString* text = mac::ns(bodyText);
    [center requestAuthorizationWithOptions:UNAuthorizationOptionAlert
                          completionHandler:^(BOOL granted, NSError* error) {
                              (void)error;
                              if (!granted) {
                                  fallback();
                                  return;
                              }
                              UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
                              content.title = heading;
                              content.body = text;
                              UNNotificationRequest* request =
                                  [UNNotificationRequest requestWithIdentifier:[[NSUUID UUID] UUIDString]
                                                                       content:content
                                                                       trigger:nil];
                              [center addNotificationRequest:request
                                       withCompletionHandler:^(NSError* addError) {
                                           if (addError) fallback();
                                       }];
                          }];
    return true;
}
}  // namespace phpvpn::tray
