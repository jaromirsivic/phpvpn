// Entry of the macOS menu bar tray (plan-macos-v001 step 9). The LaunchAgent starts
// it without arguments in every Aqua login session; the installer may pass
// --installation-complete and a user --show-window. One instance per login session:
// a later launch forwards its arguments to the running tray through
// NSDistributedNotificationCenter and exits 0. Reopening the running application
// (Finder, Launchpad, open -a) shows the control window.
#import <AppKit/AppKit.h>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "mac_tray_app.h"
#include "macos_options.h"
#include "macos_text.h"
#include "ui_style.h"

using namespace phpvpn::tray;

@interface PVAppDelegate : NSObject <NSApplicationDelegate>
- (instancetype)initWithOptions:(TrayOptions)options launch:(LaunchRequest)launch;
- (void)showWindowRequested:(NSNotification*)notification;
- (void)installationCompleteRequested:(NSNotification*)notification;
@end

@implementation PVAppDelegate {
    std::unique_ptr<TrayApp> _app;
    TrayOptions _options;
    LaunchRequest _launch;
}

- (instancetype)initWithOptions:(TrayOptions)options launch:(LaunchRequest)launch {
    self = [super init];
    if (self) {
        _options = std::move(options);
        _launch = launch;
    }
    return self;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    _app = std::make_unique<TrayApp>(std::move(_options));
    if (!_app->start()) {
        mac::showError(utf8(ui::startupError), "PHPVPN");
        _app->endSession(false);
        std::exit(1);
    }
    NSDistributedNotificationCenter* center = [NSDistributedNotificationCenter defaultCenter];
    [center addObserver:self
               selector:@selector(showWindowRequested:)
                   name:mac::ns(kShowWindowNotification)
                 object:nil
     suspensionBehavior:NSNotificationSuspensionBehaviorDeliverImmediately];
    [center addObserver:self
               selector:@selector(installationCompleteRequested:)
                   name:mac::ns(kInstallationCompleteNotification)
                 object:nil
     suspensionBehavior:NSNotificationSuspensionBehaviorDeliverImmediately];
    _app->handleLaunch(_launch);
}

- (void)showWindowRequested:(NSNotification*)notification {
    (void)notification;
    if (!_app) return;
    LaunchRequest request;
    request.showWindow = true;
    _app->handleLaunch(request);
}

- (void)installationCompleteRequested:(NSNotification*)notification {
    (void)notification;
    if (!_app) return;
    LaunchRequest request;
    request.installationComplete = true;
    _app->handleLaunch(request);
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    (void)sender;
    // Exit, logout, restart or a system quit: the terminal cleanup never shows UI,
    // never waits and never disconnects the VPN (tray_UX.md UX03).
    if (_app && !_app->ending()) _app->endSession(false);
    return NSTerminateNow;
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)sender hasVisibleWindows:(BOOL)flag {
    (void)sender;
    (void)flag;
    if (_app) _app->showControlWindow();
    return NO;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return NO;
}

- (BOOL)applicationSupportsSecureRestorableState:(NSApplication*)app {
    (void)app;
    return YES;
}
@end

namespace {
// Per-user lock in the user's temporary directory; held open for the process
// lifetime. A second instance of the same user (the same login session) fails it.
bool acquireInstanceLock() {
    NSString* directory = NSTemporaryDirectory();
    if (!directory.length) return true;
    const std::string path = mac::stdString([directory stringByAppendingPathComponent:@"com.pilandrome.phpvpn-tray.lock"]);
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return true;  // no lock available: fall back to the running-application check
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const bool held = errno == EWOULDBLOCK;
        ::close(fd);
        return !held;
    }
    return true;  // the descriptor stays open and keeps the lock
}

bool otherInstanceRunning() {
    const pid_t current = ::getpid();
    NSArray<NSRunningApplication*>* running =
        [NSRunningApplication runningApplicationsWithBundleIdentifier:mac::ns(kTrayBundleIdentifier)];
    for (NSRunningApplication* application in running)
        if (application.processIdentifier != current && !application.terminated) return true;
    return false;
}

void forwardToRunningInstance(const LaunchRequest& launch) {
    NSDistributedNotificationCenter* center = [NSDistributedNotificationCenter defaultCenter];
    if (launch.installationComplete)
        [center postNotificationName:mac::ns(kInstallationCompleteNotification) object:nil userInfo:nil
                  deliverImmediately:YES];
    if (launch.showWindow)
        [center postNotificationName:mac::ns(kShowWindowNotification) object:nil userInfo:nil deliverImmediately:YES];
}
}  // namespace

int main(int argc, char* argv[]) {
    @autoreleasepool {
        const auto launch = parseLaunchArguments(argc, argv);
        // --help needs neither a window server nor the service.
        if (launch.help) {
            std::fputs(std::string(kTrayUsage).c_str(), stdout);
            return 0;
        }
        auto options = trayOptionsFromEnvironment([](const char* name) -> const char* { return std::getenv(name); });
        if (!options) {
            std::fputs("phpvpn-tray: invalid PHPVPN_TRAY_* development setting\n", stderr);
            return 2;
        }
        if (!acquireInstanceLock() || otherInstanceRunning()) {
            forwardToRunningInstance(launch);
            return 0;
        }
        NSApplication* application = [NSApplication sharedApplication];
        // Menu bar extra without a Dock icon (Info.plist LSUIElement as well).
        [application setActivationPolicy:NSApplicationActivationPolicyAccessory];
        PVAppDelegate* delegate = [[PVAppDelegate alloc] initWithOptions:std::move(*options) launch:launch];
        application.delegate = delegate;
        [application run];
    }
    return 0;
}
