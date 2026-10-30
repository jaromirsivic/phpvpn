#include "mac_ui.h"

#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <utility>
#include <vector>

#include "core/base/secure_buffer.h"

using phpvpn::tray::mac::StackItem;

// --- target/action and delegate adapters -------------------------------------------------
@interface PVActionTarget : NSObject
- (instancetype)initWithAction:(std::function<void()>)action;
- (void)invoke:(id)sender;
@end

@implementation PVActionTarget {
    std::function<void()> _action;
}
- (instancetype)initWithAction:(std::function<void()>)action {
    self = [super init];
    if (self) _action = std::move(action);
    return self;
}
- (void)invoke:(id)sender {
    (void)sender;
    // A copy keeps the callback alive even when it replaces the control's action.
    auto action = _action;
    if (action) action();
}
@end

@interface PVTextDelegate : NSObject <NSTextFieldDelegate, NSTextViewDelegate>
- (instancetype)initWithChanged:(std::function<void()>)changed;
@end

@implementation PVTextDelegate {
    std::function<void()> _changed;
}
- (instancetype)initWithChanged:(std::function<void()>)changed {
    self = [super init];
    if (self) _changed = std::move(changed);
    return self;
}
- (void)controlTextDidChange:(NSNotification*)notification {
    (void)notification;
    auto changed = _changed;
    if (changed) changed();
}
- (void)textDidChange:(NSNotification*)notification {
    (void)notification;
    auto changed = _changed;
    if (changed) changed();
}
@end

// Whole-text preflight of single-line edits (the Win32 ContractsEdit and the GTK
// insert-text preflight): a refused proposal leaves the field unchanged.
@interface PVEditFormatter : NSFormatter
- (instancetype)initWithCheck:(std::function<bool(NSString*)>)check;
@end

@implementation PVEditFormatter {
    std::function<bool(NSString*)> _check;
}
- (instancetype)initWithCheck:(std::function<bool(NSString*)>)check {
    self = [super init];
    if (self) _check = std::move(check);
    return self;
}
// Immutable: a copy is the same object (never a bitwise copy of the C++ member).
- (id)copyWithZone:(NSZone*)zone {
    (void)zone;
    return self;
}
- (NSString*)stringForObjectValue:(id)obj {
    return [obj isKindOfClass:[NSString class]] ? (NSString*)obj : @"";
}
- (BOOL)getObjectValue:(out id*)obj forString:(NSString*)string errorDescription:(out NSString**)error {
    (void)error;
    if (obj) *obj = string ? string : @"";
    return YES;
}
- (BOOL)isPartialStringValid:(NSString*)partialString newEditingString:(NSString**)newString
            errorDescription:(NSString**)error {
    (void)newString;
    (void)error;
    if (!_check) return YES;
    return _check(partialString ? partialString : @"") ? YES : NO;
}
@end

namespace {
char kActionKey = 0;
char kTextDelegateKey = 0;
char kSwitchKey = 0;
char kLabelTargetKey = 0;
int gDialogDepth = 0;

CGFloat roundUp(CGFloat value) { return std::ceil(value); }
}  // namespace

namespace phpvpn::tray::mac {
// --- text ------------------------------------------------------------------------------------
NSString* ns(std::string_view text) {
    if (text.empty()) return @"";
    NSString* value = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
    return value ? value : @"";
}

std::string stdString(NSString* text) {
    if (!text) return {};
    const char* bytes = [text UTF8String];
    return bytes ? std::string(bytes) : std::string{};
}

std::optional<std::string> strictUtf8(NSString* text) {
    if (!text) return std::string{};
    NSData* data = [text dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
    if (!data) return std::nullopt;
    return std::string(static_cast<const char*>(data.bytes), static_cast<std::size_t>(data.length));
}

std::u16string utf16(NSString* text) {
    if (!text || text.length == 0) return {};
    std::u16string result(static_cast<std::size_t>(text.length), u'\0');
    [text getCharacters:reinterpret_cast<unichar*>(result.data()) range:NSMakeRange(0, text.length)];
    return result;
}

NSString* nsUtf16(std::u16string_view text) {
    if (text.empty()) return @"";
    return [NSString stringWithCharacters:reinterpret_cast<const unichar*>(text.data()) length:text.size()];
}

// --- palette and fonts ---------------------------------------------------------------------------
NSColor* color(ui::Color token) {
    const auto value = ui::colors[static_cast<std::size_t>(token)];
    return [NSColor colorWithSRGBRed:static_cast<CGFloat>((value >> 16) & 0xFF) / 255.0
                               green:static_cast<CGFloat>((value >> 8) & 0xFF) / 255.0
                                blue:static_cast<CGFloat>(value & 0xFF) / 255.0
                               alpha:1.0];
}

NSFont* font(ui::Font role) {
    const auto& spec = ui::fonts[static_cast<std::size_t>(role)];
    const CGFloat size = std::round(static_cast<CGFloat>(spec.height) * 0.85 * 2.0) / 2.0;
    const NSFontWeight weight = spec.weight >= 700 ? NSFontWeightBold
                              : spec.weight >= 600 ? NSFontWeightSemibold
                                                   : NSFontWeightRegular;
    if (spec.mono) return [NSFont monospacedSystemFontOfSize:size weight:weight];
    return [NSFont systemFontOfSize:size weight:weight];
}

// --- run loop ----------------------------------------------------------------------------------------
void post(std::function<void()> task) {
    auto shared = std::make_shared<std::function<void()>>(std::move(task));
    CFRunLoopRef loop = CFRunLoopGetMain();
    CFRunLoopPerformBlock(loop, kCFRunLoopCommonModes, ^{
        if (*shared) (*shared)();
    });
    CFRunLoopWakeUp(loop);
}

void Deferred::post(std::function<void()> call) {
    auto alive = alive_;
    mac::post([alive, call = std::move(call)] {
        if (*alive && call) call();
    });
}

void Timer::start(std::int64_t intervalMs, std::function<void()> tick, bool repeats) {
    stop();
    auto shared = std::make_shared<std::function<void()>>(std::move(tick));
    const NSTimeInterval seconds = static_cast<NSTimeInterval>(std::max<std::int64_t>(intervalMs, 1)) / 1000.0;
    timer_ = [NSTimer timerWithTimeInterval:seconds repeats:(repeats ? YES : NO) block:^(NSTimer* timer) {
        (void)timer;
        if (*shared) (*shared)();
    }];
    [[NSRunLoop mainRunLoop] addTimer:timer_ forMode:NSRunLoopCommonModes];
}

void Timer::stop() {
    if (timer_) [timer_ invalidate];
    timer_ = nil;
}

bool Timer::active() const { return timer_ != nil && timer_.valid; }

// --- dialogs -----------------------------------------------------------------------------------------
namespace {
NSModalResponse runAlert(NSAlert* alert) {
    [NSApp activate];
    ++gDialogDepth;
    const NSModalResponse response = [alert runModal];
    --gDialogDepth;
    return response;
}
}  // namespace

void showMessage(std::string_view text, std::string_view title, DialogIcon icon) {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = ns(title);
    alert.informativeText = ns(text);
    alert.alertStyle = icon == DialogIcon::error ? NSAlertStyleCritical
                     : icon == DialogIcon::warning ? NSAlertStyleWarning
                                                   : NSAlertStyleInformational;
    [alert addButtonWithTitle:@"OK"];
    (void)runAlert(alert);
}

bool confirm(std::string_view text, std::string_view title, bool defaultYes) {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = ns(title);
    alert.informativeText = ns(text);
    alert.alertStyle = NSAlertStyleWarning;
    // The first button is the default (Return). A destructive question defaults to No.
    NSButton* first = [alert addButtonWithTitle:defaultYes ? @"Yes" : @"No"];
    NSButton* second = [alert addButtonWithTitle:defaultYes ? @"No" : @"Yes"];
    (void)first;
    if (defaultYes) second.keyEquivalent = @"\x1b";
    else second.hasDestructiveAction = YES;
    const NSModalResponse response = runAlert(alert);
    if (defaultYes) return response == NSAlertFirstButtonReturn;
    return response == NSAlertSecondButtonReturn;
}

void closeAllDialogs() {
    if (gDialogDepth <= 0) return;
    // The alert returns NSModalResponseAbort, which every caller treats as No.
    [NSApp stopModalWithCode:NSModalResponseAbort];
    NSEvent* wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0
                                      timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0];
    if (wake) [NSApp postEvent:wake atStart:NO];
}

bool dialogOpen() { return gDialogDepth > 0; }

// --- controls ----------------------------------------------------------------------------------------
void setAction(NSControl* control, std::function<void()> action) {
    if (!control) return;
    PVActionTarget* target = [[PVActionTarget alloc] initWithAction:std::move(action)];
    objc_setAssociatedObject(control, &kActionKey, target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    control.target = target;
    control.action = @selector(invoke:);
}

void setMenuAction(NSMenuItem* item, std::function<void()> action) {
    if (!item) return;
    PVActionTarget* target = [[PVActionTarget alloc] initWithAction:std::move(action)];
    objc_setAssociatedObject(item, &kActionKey, target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    item.target = target;
    item.action = @selector(invoke:);
}

void setClickAction(NSView* view, std::function<void()> action) {
    if (!view) return;
    PVActionTarget* target = [[PVActionTarget alloc] initWithAction:std::move(action)];
    objc_setAssociatedObject(view, &kActionKey, target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    NSClickGestureRecognizer* click = [[NSClickGestureRecognizer alloc] initWithTarget:target action:@selector(invoke:)];
    [view addGestureRecognizer:click];
}

void setTextChanged(NSTextField* field, std::function<void()> changed) {
    if (!field) return;
    PVTextDelegate* delegate = [[PVTextDelegate alloc] initWithChanged:std::move(changed)];
    objc_setAssociatedObject(field, &kTextDelegateKey, delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    field.delegate = delegate;
}

void setTextChanged(NSTextView* view, std::function<void()> changed) {
    if (!view) return;
    PVTextDelegate* delegate = [[PVTextDelegate alloc] initWithChanged:std::move(changed)];
    objc_setAssociatedObject(view, &kTextDelegateKey, delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    view.delegate = delegate;
}

void setEditCheck(NSTextField* field, std::function<bool(NSString*)> check) {
    if (!field) return;
    field.formatter = [[PVEditFormatter alloc] initWithCheck:std::move(check)];
}

NSTextField* makeLabel(std::string_view text, ui::Font role, ui::Color tone, bool wrap) {
    NSTextField* label = wrap ? [NSTextField wrappingLabelWithString:ns(text)] : [NSTextField labelWithString:ns(text)];
    label.font = font(role);
    label.textColor = color(tone);
    label.drawsBackground = NO;
    label.bezeled = NO;
    label.editable = NO;
    label.selectable = NO;
    if (wrap) {
        label.cell.wraps = YES;
        label.cell.scrollable = NO;
        label.lineBreakMode = NSLineBreakByWordWrapping;
        label.usesSingleLineMode = NO;
        label.maximumNumberOfLines = 0;
    } else {
        label.lineBreakMode = NSLineBreakByClipping;
    }
    return label;
}

void setLabelText(NSTextField* label, std::string_view text) {
    if (!label) return;
    NSString* value = ns(text);
    if (![label.stringValue isEqualToString:value]) label.stringValue = value;
}

void setTextColor(NSTextField* label, ui::Color tone) {
    if (label) label.textColor = color(tone);
}

namespace {
void configureSingleLine(NSTextField* field, ui::Font role) {
    field.font = font(role);
    field.textColor = color(ui::Color::text);
    field.bezeled = YES;
    field.bezelStyle = NSTextFieldSquareBezel;
    field.drawsBackground = YES;
    field.usesSingleLineMode = YES;
    field.cell.wraps = NO;
    field.cell.scrollable = YES;
    field.lineBreakMode = NSLineBreakByClipping;
    field.maximumNumberOfLines = 1;
}
}  // namespace

NSTextField* makeEntry(ui::Font role, std::string_view accessibleName) {
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 200, 24)];
    configureSingleLine(field, role);
    field.editable = YES;
    field.selectable = YES;
    field.backgroundColor = color(ui::Color::surface);
    field.allowsEditingTextAttributes = NO;
    field.importsGraphics = NO;
    field.automaticTextCompletionEnabled = NO;
    // An action (if any) runs on Return only, never when the focus leaves the field.
    field.cell.sendsActionOnEndEditing = NO;
    if (!accessibleName.empty()) [field setAccessibilityLabel:ns(accessibleName)];
    return field;
}

NSTextField* makeReadOnlyField(std::string_view text, ui::Font role, std::string_view accessibleName) {
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 200, 24)];
    configureSingleLine(field, role);
    field.editable = NO;
    field.selectable = YES;
    field.backgroundColor = color(ui::Color::disabledBackground);
    field.stringValue = ns(text);
    if (!accessibleName.empty()) [field setAccessibilityLabel:ns(accessibleName)];
    return field;
}

void setFieldText(NSTextField* field, std::string_view text) {
    if (!field) return;
    NSString* value = ns(text);
    if (![field.stringValue isEqualToString:value]) field.stringValue = value;
}

void setFieldInvalid(NSTextField* field, bool invalid, std::string_view description) {
    if (!field) return;
    field.wantsLayer = YES;
    field.layer.borderWidth = invalid ? 2.0 : 0.0;
    field.layer.borderColor = invalid ? color(ui::Color::error).CGColor : nil;
    field.layer.cornerRadius = invalid ? 2.0 : 0.0;
    [field setAccessibilityHelp:invalid ? ns(description) : nil];
}

NSButton* makeButton(std::string_view title, ButtonRole role, std::function<void()> action) {
    // A push button with the rounded bezel; the large control size comes closest to
    // the 36 DIP button height (UX01).
    NSButton* button = [NSButton buttonWithTitle:ns(title) target:nil action:nil];
    button.controlSize = NSControlSizeLarge;
    button.font = [NSFont systemFontOfSize:[NSFont systemFontSizeForControlSize:NSControlSizeLarge]];
    setButtonRole(button, role);
    setAction(button, std::move(action));
    return button;
}

void setButtonTitle(NSButton* button, std::string_view title) {
    if (!button) return;
    NSString* value = ns(title);
    if (![button.title isEqualToString:value]) button.title = value;
}

void setButtonRole(NSButton* button, ButtonRole role) {
    if (!button) return;
    switch (role) {
        case ButtonRole::primary: button.bezelColor = color(ui::Color::primary); break;
        case ButtonRole::destructive: button.bezelColor = color(ui::Color::buttonReset); break;
        case ButtonRole::secondary: button.bezelColor = nil; break;
    }
}

void setVisible(NSView* view, bool visible) {
    if (view && view.hidden == visible) view.hidden = !visible;
}
}  // namespace phpvpn::tray::mac

// Gesture target of a switch label: the label toggles its switch like a click on it.
@interface PVSwitchLabelTarget : NSObject
@property (nonatomic, weak) NSSwitch* toggle;
- (void)clicked:(id)sender;
@end

@implementation PVSwitchLabelTarget
- (void)clicked:(id)sender {
    (void)sender;
    NSSwitch* toggle = self.toggle;
    if (!toggle || !toggle.enabled) return;
    [toggle.window makeFirstResponder:toggle];
    toggle.state = toggle.state == NSControlStateValueOn ? NSControlStateValueOff : NSControlStateValueOn;
    [toggle sendAction:toggle.action to:toggle.target];
}
@end

namespace phpvpn::tray::mac {
SwitchRow makeSwitchRow(std::string_view title, std::function<void(bool)> changed) {
    SwitchRow result;
    PVStackView* row = makeStack(false, ui::relatedGap);
    NSSwitch* toggle = [[NSSwitch alloc] initWithFrame:NSMakeRect(0, 0, 40, 24)];
    [toggle setAccessibilityLabel:ns(title)];
    __weak NSSwitch* weakToggle = toggle;
    setAction(toggle, [weakToggle, changed = std::move(changed)] {
        NSSwitch* strongToggle = weakToggle;
        if (strongToggle && changed) changed(strongToggle.state == NSControlStateValueOn);
    });
    NSTextField* label = makeLabel(title, ui::Font::label, ui::Color::text, true);
    PVSwitchLabelTarget* target = [[PVSwitchLabelTarget alloc] init];
    target.toggle = toggle;
    objc_setAssociatedObject(label, &kLabelTargetKey, target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    NSClickGestureRecognizer* click = [[NSClickGestureRecognizer alloc] initWithTarget:target action:@selector(clicked:)];
    [label addGestureRecognizer:click];
    [row addView:toggle item:natural()];
    StackItem caption = flexibleItem();
    caption.fill = false;
    [row addView:label item:caption];
    objc_setAssociatedObject(row, &kSwitchKey, toggle, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    result.row = row;
    result.toggle = toggle;
    result.label = label;
    return result;
}

bool switchOn(NSSwitch* toggle) { return toggle && toggle.state == NSControlStateValueOn; }

void setSwitchOn(NSSwitch* toggle, bool on) {
    if (!toggle) return;
    const NSControlStateValue state = on ? NSControlStateValueOn : NSControlStateValueOff;
    if (toggle.state != state) toggle.state = state;
}

TextScroll makeTextScroll(bool editable, bool mono, std::string_view accessibleName) {
    TextScroll result;
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300)];
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = YES;
    scroll.autohidesScrollers = NO;
    scroll.borderType = NSBezelBorder;
    const NSSize content = scroll.contentSize;
    PVTextView* text = [[PVTextView alloc] initWithFrame:NSMakeRect(0, 0, content.width, content.height)];
    text.tabMovesFocus = YES;
    text.minSize = NSMakeSize(0, content.height);
    text.maxSize = NSMakeSize(FLT_MAX, FLT_MAX);
    text.verticallyResizable = YES;
    text.horizontallyResizable = YES;
    text.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    text.textContainer.containerSize = NSMakeSize(FLT_MAX, FLT_MAX);
    text.textContainer.widthTracksTextView = NO;
    text.editable = editable ? YES : NO;
    text.selectable = YES;
    text.richText = NO;
    text.importsGraphics = NO;
    text.allowsUndo = editable ? YES : NO;
    text.usesFindBar = YES;
    text.automaticQuoteSubstitutionEnabled = NO;
    text.automaticDashSubstitutionEnabled = NO;
    text.automaticTextReplacementEnabled = NO;
    text.automaticSpellingCorrectionEnabled = NO;
    text.automaticLinkDetectionEnabled = NO;
    text.automaticDataDetectionEnabled = NO;
    text.automaticTextCompletionEnabled = NO;
    text.continuousSpellCheckingEnabled = NO;
    text.grammarCheckingEnabled = NO;
    text.smartInsertDeleteEnabled = NO;
    text.font = mono ? font(ui::Font::mono) : font(ui::Font::label);
    text.textColor = color(ui::Color::text);
    text.backgroundColor = editable ? color(ui::Color::surface) : color(ui::Color::disabledBackground);
    text.drawsBackground = YES;
    text.insertionPointColor = color(ui::Color::text);
    text.textContainerInset = NSMakeSize(4, 4);
    if (!accessibleName.empty()) [text setAccessibilityLabel:ns(accessibleName)];
    scroll.documentView = text;
    result.scroll = scroll;
    result.text = text;
    return result;
}

void setTextViewString(NSTextView* view, std::string_view text) {
    if (!view) return;
    view.string = ns(text);
    view.textColor = color(ui::Color::text);
    [view.undoManager removeAllActions];
    [view setSelectedRange:NSMakeRange(0, 0)];
    [view scrollRangeToVisible:NSMakeRange(0, 0)];
}

std::string textViewString(NSTextView* view) {
    return view ? stdString(view.string) : std::string{};
}

// --- windows ------------------------------------------------------------------------------------------
NSScreen* screenForNewWindow() {
    const NSPoint mouse = [NSEvent mouseLocation];
    for (NSScreen* screen in [NSScreen screens])
        if (NSPointInRect(mouse, screen.frame)) return screen;
    return [NSScreen mainScreen];
}

void presentWindow(NSWindow* window) {
    if (!window) return;
    if (window.miniaturized) [window deminiaturize:nil];
    [NSApp activate];
    [window makeKeyAndOrderFront:nil];
    [window orderFrontRegardless];
}

void closeWindow(NSWindow* window) {
    if (!window) return;
    if ([window isKindOfClass:[PVWindow class]]) [(PVWindow*)window pv_clearHandlers];
    if (window.sheetParent) [window.sheetParent endSheet:window];
    [window orderOut:nil];
    [window close];
}

bool windowVisible(NSWindow* window) { return window && window.visible; }

void setCloseEnabled(NSWindow* window, bool enabled) {
    NSButton* close = [window standardWindowButton:NSWindowCloseButton];
    if (close && close.enabled != enabled) close.enabled = enabled ? YES : NO;
}

// --- layout -----------------------------------------------------------------------------------------
NSSize naturalSize(NSView* view) {
    if (!view) return NSZeroSize;
    // A separator line takes its length from the stack (fill or a fixed size).
    if ([view isKindOfClass:[PVLineView class]]) return NSMakeSize(1, 1);
    if ([view isKindOfClass:[PVStackView class]]) {
        PVStackView* stack = (PVStackView*)view;
        const CGFloat width = [stack naturalWidth];
        return NSMakeSize(width, [stack heightForWidth:width]);
    }
    if ([view isKindOfClass:[NSTextField class]]) {
        NSTextField* field = (NSTextField*)view;
        const NSSize size = [field.cell cellSizeForBounds:NSMakeRect(0, 0, 100000, 100000)];
        return NSMakeSize(roundUp(size.width), roundUp(size.height));
    }
    if ([view isKindOfClass:[NSControl class]]) {
        NSControl* control = (NSControl*)view;
        if (control.cell) {
            const NSSize size = [control.cell cellSize];
            return NSMakeSize(roundUp(size.width), roundUp(size.height));
        }
    }
    const NSSize intrinsic = view.intrinsicContentSize;
    const NSSize frame = view.frame.size;
    return NSMakeSize(intrinsic.width > 0 ? roundUp(intrinsic.width) : frame.width,
                      intrinsic.height > 0 ? roundUp(intrinsic.height) : frame.height);
}

CGFloat heightForWidth(NSView* view, CGFloat width) {
    if (!view) return 0;
    if ([view isKindOfClass:[PVStackView class]]) return [(PVStackView*)view heightForWidth:width];
    if ([view isKindOfClass:[NSTextField class]]) {
        NSTextField* field = (NSTextField*)view;
        if (field.cell.wraps) {
            const NSSize size = [field.cell cellSizeForBounds:NSMakeRect(0, 0, std::max<CGFloat>(width, 1), 100000)];
            return roundUp(size.height);
        }
    }
    return naturalSize(view).height;
}
}  // namespace phpvpn::tray::mac

namespace phpvpn::tray {
Dispatcher mainRunLoopDispatcher() {
    return [](std::function<void()> task) { mac::post(std::move(task)); };
}
}  // namespace phpvpn::tray

// --- PVStackView ------------------------------------------------------------------------------------
@implementation PVStackView {
    NSMutableArray<NSView*>* _views;
    std::vector<StackItem> _items;
    BOOL _layingOut;
    BOOL _laidHorizontally;
}

- (instancetype)initVertical:(BOOL)vertical spacing:(CGFloat)spacing {
    self = [super initWithFrame:NSMakeRect(0, 0, 100, 100)];
    if (self) {
        _vertical = vertical;
        _spacing = spacing;
        _insets = NSEdgeInsetsMake(0, 0, 0, 0);
        _views = [NSMutableArray array];
    }
    return self;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _vertical = YES;
        _insets = NSEdgeInsetsMake(0, 0, 0, 0);
        _views = [NSMutableArray array];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)setFillColor:(NSColor*)fillColor {
    _fillColor = fillColor;
    self.needsDisplay = YES;
}

- (void)setStrokeColor:(NSColor*)strokeColor {
    _strokeColor = strokeColor;
    self.needsDisplay = YES;
}

- (void)setDividerColor:(NSColor*)dividerColor {
    _dividerColor = dividerColor;
    self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    if (_fillColor || _strokeColor) {
        const NSRect rect = NSInsetRect(self.bounds, 0.5, 0.5);
        NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:rect xRadius:_cornerRadius yRadius:_cornerRadius];
        if (_fillColor) {
            [_fillColor setFill];
            [path fill];
        }
        if (_strokeColor) {
            [_strokeColor setStroke];
            path.lineWidth = 1.0;
            [path stroke];
        }
    }
    if (_dividerColor && _laidHorizontally) {
        [_dividerColor setFill];
        bool first = true;
        for (NSView* view in _views) {
            if (view.hidden) continue;
            if (!first) {
                const CGFloat x = std::floor(NSMinX(view.frame) - _spacing / 2);
                NSRectFill(NSMakeRect(x, NSMinY(view.frame), 1, NSHeight(view.frame)));
            }
            first = false;
        }
    }
}

- (void)addView:(NSView*)view {
    [self addView:view item:StackItem{}];
}

- (void)addView:(NSView*)view item:(StackItem)item {
    if (!view) return;
    [self insertView:view item:item atIndex:_views.count];
}

- (void)insertView:(NSView*)view item:(StackItem)item atIndex:(NSUInteger)index {
    if (!view) return;
    [self removeView:view];
    const NSUInteger position = std::min<NSUInteger>(index, _views.count);
    [_views insertObject:view atIndex:position];
    _items.insert(_items.begin() + static_cast<std::ptrdiff_t>(position), item);
    if (view.superview != self) [self addSubview:view];
}

- (void)setItem:(StackItem)item forView:(NSView*)view {
    const NSUInteger index = [_views indexOfObjectIdenticalTo:view];
    if (index == NSNotFound) return;
    _items[static_cast<std::size_t>(index)] = item;
}

- (void)removeView:(NSView*)view {
    const NSUInteger index = [_views indexOfObjectIdenticalTo:view];
    if (index == NSNotFound) return;
    [_views removeObjectAtIndex:index];
    _items.erase(_items.begin() + static_cast<std::ptrdiff_t>(index));
    if (view.superview == self) [view removeFromSuperview];
}

- (void)removeAllViews {
    for (NSView* view in [_views copy])
        if (view.superview == self) [view removeFromSuperview];
    [_views removeAllObjects];
    _items.clear();
}

// Computes the frames for size (height 0 while measuring) and returns the measured
// content height including the insets.
- (CGFloat)layoutForSize:(NSSize)size frames:(std::vector<NSRect>*)frames {
    using namespace phpvpn::tray::mac;
    const std::size_t count = _items.size();
    if (frames) frames->assign(count, NSZeroRect);
    const CGFloat innerWidth = std::max<CGFloat>(0, size.width - _insets.left - _insets.right);
    std::vector<std::size_t> visible;
    for (std::size_t i = 0; i < count; ++i)
        if (!_views[i].hidden) visible.push_back(i);
    if (visible.empty()) return _insets.top + _insets.bottom;

    std::vector<CGFloat> natural(count, 0);
    CGFloat needed = 0;
    for (std::size_t n = 0; n < visible.size(); ++n) {
        const std::size_t i = visible[n];
        const StackItem& item = _items[i];
        natural[i] = item.width > 0 ? item.width : std::max(naturalSize(_views[i]).width, item.minWidth);
        needed += natural[i] + (n ? _spacing : 0);
    }
    const BOOL vertical = _vertical || _forceVertical || (_wrapsWhenNarrow && needed > innerWidth + 0.5);
    if (frames && !_vertical) {
        const BOOL horizontally = vertical ? NO : YES;
        if (horizontally != _laidHorizontally) self.needsDisplay = YES;
        _laidHorizontally = horizontally;
    }

    if (vertical) {
        std::vector<CGFloat> widths(count, 0), heights(count, 0);
        CGFloat total = _insets.top + _insets.bottom;
        std::size_t flexible = count;
        for (std::size_t n = 0; n < visible.size(); ++n) {
            const std::size_t i = visible[n];
            const StackItem& item = _items[i];
            const bool fills = _vertical ? item.fill : (item.flexible || _homogeneous || item.fill);
            const CGFloat width = item.width > 0 ? std::min(item.width, innerWidth)
                                : fills ? innerWidth
                                        : std::min(natural[i], innerWidth);
            widths[i] = width;
            // A flexible item of a vertical stack is measured at its minimum height and
            // takes the remaining height when laid out.
            heights[i] = item.height > 0 ? item.height
                       : (_vertical && item.flexible) ? item.minHeight
                                                      : std::max(heightForWidth(_views[i], width), item.minHeight);
            const CGFloat gap = n == 0 ? 0 : (item.gapBefore >= 0 ? item.gapBefore : _spacing);
            total += heights[i] + gap;
            if (_vertical && item.flexible && flexible == count) flexible = i;
        }
        if (frames) {
            if (flexible < count && size.height > total) heights[flexible] += size.height - total;
            CGFloat y = _insets.top;
            for (std::size_t n = 0; n < visible.size(); ++n) {
                const std::size_t i = visible[n];
                const StackItem& item = _items[i];
                y += n == 0 ? 0 : (item.gapBefore >= 0 ? item.gapBefore : _spacing);
                const CGFloat x = _insets.left + (item.trailing ? innerWidth - widths[i] : 0);
                (*frames)[i] = NSMakeRect(x, y, widths[i], heights[i]);
                y += heights[i];
            }
        }
        return total;
    }

    // Horizontal: leading items from the left, trailing items from the right, the
    // flexible item takes the remaining width.
    std::vector<CGFloat> widths(count, 0), heights(count, 0);
    const CGFloat extra = std::max<CGFloat>(0, innerWidth - needed);
    const std::size_t visibleCount = visible.size();
    const CGFloat homogeneousWidth =
        visibleCount ? std::max<CGFloat>(0, (innerWidth - _spacing * static_cast<CGFloat>(visibleCount - 1)) /
                                                static_cast<CGFloat>(visibleCount))
                     : 0;
    bool flexibleUsed = false;
    CGFloat rowHeight = 0;
    for (std::size_t i : visible) {
        const StackItem& item = _items[i];
        CGFloat width = _homogeneous ? homogeneousWidth : natural[i];
        if (!_homogeneous && item.flexible && !flexibleUsed) {
            width += extra;
            flexibleUsed = true;
        }
        widths[i] = width;
        // Every item counts for the natural row height; fill items are then stretched
        // to the row height.
        heights[i] = item.height > 0 ? item.height : std::max(heightForWidth(_views[i], width), item.minHeight);
        rowHeight = std::max(rowHeight, heights[i]);
    }
    const CGFloat total = _insets.top + rowHeight + _insets.bottom;
    if (frames) {
        const CGFloat available = std::max(rowHeight, size.height - _insets.top - _insets.bottom);
        CGFloat x = _insets.left;
        bool first = true;
        for (std::size_t i : visible) {
            if (_items[i].trailing) continue;
            if (!first) x += _spacing;
            first = false;
            const StackItem& item = _items[i];
            const CGFloat height = item.fill && item.height <= 0 ? available : heights[i];
            const CGFloat y = item.top || (item.fill && item.height <= 0) ? _insets.top
                                                                          : _insets.top + (available - height) / 2;
            (*frames)[i] = NSMakeRect(x, std::round(y), widths[i], height);
            x += widths[i];
        }
        CGFloat right = size.width - _insets.right;
        bool firstTrailing = true;
        for (auto it = visible.rbegin(); it != visible.rend(); ++it) {
            const std::size_t i = *it;
            if (!_items[i].trailing) continue;
            if (!firstTrailing) right -= _spacing;
            firstTrailing = false;
            const StackItem& item = _items[i];
            const CGFloat height = item.fill && item.height <= 0 ? available : heights[i];
            const CGFloat y = item.top || (item.fill && item.height <= 0) ? _insets.top
                                                                          : _insets.top + (available - height) / 2;
            right -= widths[i];
            (*frames)[i] = NSMakeRect(right, std::round(y), widths[i], height);
        }
    }
    return total;
}

- (CGFloat)heightForWidth:(CGFloat)width {
    return std::ceil([self layoutForSize:NSMakeSize(width, 0) frames:nullptr]);
}

- (CGFloat)naturalWidth {
    using namespace phpvpn::tray::mac;
    CGFloat sum = 0, widest = 0;
    std::size_t visible = 0;
    for (std::size_t i = 0; i < _items.size(); ++i) {
        if (_views[i].hidden) continue;
        const CGFloat width = _items[i].width > 0 ? _items[i].width
                                                  : std::max(naturalSize(_views[i]).width, _items[i].minWidth);
        sum += width + (visible ? _spacing : 0);
        widest = std::max(widest, width);
        ++visible;
    }
    const CGFloat content = (_vertical || _forceVertical) ? widest
                          : _homogeneous ? widest * static_cast<CGFloat>(visible) +
                                               _spacing * static_cast<CGFloat>(visible ? visible - 1 : 0)
                                         : sum;
    return std::ceil(content + _insets.left + _insets.right);
}

- (void)relayout {
    if (_layingOut) return;
    _layingOut = YES;
    std::vector<NSRect> frames;
    [self layoutForSize:self.bounds.size frames:&frames];
    for (std::size_t i = 0; i < _items.size(); ++i) {
        NSView* view = _views[i];
        if (view.hidden) continue;
        const NSRect frame = frames[i];
        const bool sameSize = NSEqualSizes(view.frame.size, frame.size);
        if (!NSEqualRects(view.frame, frame)) view.frame = frame;
        if (sameSize) {
            if ([view isKindOfClass:[PVStackView class]]) [(PVStackView*)view relayout];
            else if ([view isKindOfClass:[PVScrollView class]]) [(PVScrollView*)view relayoutDocument];
        }
    }
    _layingOut = NO;
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    [self relayout];
}
@end

// --- PVScrollView ---------------------------------------------------------------------------------------
@implementation PVScrollView {
    BOOL _tiling;
}

- (instancetype)initWithStack:(PVStackView*)stack {
    self = [super initWithFrame:NSMakeRect(0, 0, 200, 200)];
    if (self) {
        _stack = stack;
        self.hasVerticalScroller = YES;
        self.hasHorizontalScroller = NO;
        self.autohidesScrollers = YES;
        self.drawsBackground = NO;
        self.borderType = NSNoBorder;
        self.documentView = stack;
    }
    return self;
}

- (void)tile {
    [super tile];
    [self relayoutDocument];
}

- (void)relayoutDocument {
    if (_tiling || !_stack) return;
    _tiling = YES;
    const NSSize content = self.contentSize;
    const CGFloat height = std::max<CGFloat>([_stack heightForWidth:content.width], content.height);
    const NSSize size = NSMakeSize(content.width, height);
    if (!NSEqualSizes(_stack.frame.size, size)) [_stack setFrameSize:size];
    else [_stack relayout];
    _tiling = NO;
}
@end

// --- PVLineView ------------------------------------------------------------------------------------------
@implementation PVLineView
- (NSSize)intrinsicContentSize {
    return _vertical ? NSMakeSize(1, NSViewNoIntrinsicMetric) : NSMakeSize(NSViewNoIntrinsicMetric, 1);
}
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [(_lineColor ? _lineColor : phpvpn::tray::mac::color(phpvpn::tray::ui::Color::separator)) setFill];
    NSRectFill(self.bounds);
}
@end

// --- PVMarkerView ----------------------------------------------------------------------------------------
@implementation PVMarkerView
- (NSSize)intrinsicContentSize { return NSMakeSize(24, 24); }
- (void)setText:(NSString*)text {
    _text = [text copy];
    self.needsDisplay = YES;
}
- (void)setCircleColor:(NSColor*)circleColor {
    _circleColor = circleColor;
    self.needsDisplay = YES;
}
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    const CGFloat side = std::min(self.bounds.size.width, self.bounds.size.height);
    const NSRect circle = NSMakeRect((self.bounds.size.width - side) / 2, (self.bounds.size.height - side) / 2, side, side);
    [(_circleColor ? _circleColor : phpvpn::tray::mac::color(phpvpn::tray::ui::Color::secondaryText)) setFill];
    [[NSBezierPath bezierPathWithOvalInRect:circle] fill];
    if (!_text.length) return;
    NSDictionary* attributes = @{
        NSFontAttributeName: [NSFont systemFontOfSize:12 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: phpvpn::tray::mac::color(phpvpn::tray::ui::Color::primaryText),
    };
    const NSSize textSize = [_text sizeWithAttributes:attributes];
    const NSPoint origin = NSMakePoint(NSMidX(circle) - textSize.width / 2, NSMidY(circle) - textSize.height / 2);
    [_text drawAtPoint:origin withAttributes:attributes];
}
@end

// --- PVPaintLabel -----------------------------------------------------------------------------------------
@implementation PVPaintLabel {
    std::function<void()> _paint;
}
- (void)setPaintHandler:(std::function<void()>)handler {
    _paint = std::move(handler);
}
- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];
    if (_paint) {
        auto paint = _paint;
        // Notified after the drawing pass; the handler only reads the model clock.
        paint();
    }
}
@end

// --- PVTextView ----------------------------------------------------------------------------------------------
@implementation PVTextView
- (void)insertTab:(id)sender {
    if (_tabMovesFocus) [self.window selectNextKeyView:sender];
    else [super insertTab:sender];
}
- (void)insertBacktab:(id)sender {
    if (_tabMovesFocus) [self.window selectPreviousKeyView:sender];
    else [super insertBacktab:sender];
}
@end

// --- PVWindow -----------------------------------------------------------------------------------------------
@implementation PVWindow {
    std::function<void()> _close;
    std::function<bool(NSEvent*)> _key;
    std::function<void()> _resize;
}

- (void)setCloseHandler:(std::function<void()>)handler { _close = std::move(handler); }
- (void)setKeyHandler:(std::function<bool(NSEvent*)>)handler { _key = std::move(handler); }
- (void)setResizeHandler:(std::function<void()>)handler { _resize = std::move(handler); }
- (void)pv_clearHandlers {
    _close = {};
    _key = {};
    _resize = {};
}

- (BOOL)pv_handleKey:(NSEvent*)event {
    if (!_key || event.type != NSEventTypeKeyDown) return NO;
    const NSEventModifierFlags modifiers =
        event.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagControl | NSEventModifierFlagOption);
    if (modifiers != 0) return NO;
    if (!phpvpn::tray::mac::isEscape(event) && !phpvpn::tray::mac::isReturn(event)) return NO;
    auto key = _key;
    return key(event) ? YES : NO;
}

- (BOOL)performKeyEquivalent:(NSEvent*)event {
    if ([self pv_handleKey:event]) return YES;
    return [super performKeyEquivalent:event];
}

- (void)keyDown:(NSEvent*)event {
    if ([self pv_handleKey:event]) return;
    [super keyDown:event];
}

- (void)cancelOperation:(id)sender {
    (void)sender;
    NSEvent* escape = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0 timestamp:0
                                   windowNumber:self.windowNumber context:nil characters:@"\x1b"
                    charactersIgnoringModifiers:@"\x1b" isARepeat:NO keyCode:53];
    if (escape && [self pv_handleKey:escape]) return;
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    if (!_close) return YES;
    auto close = _close;
    // Never inside the close button's own event handling: a question may follow.
    phpvpn::tray::mac::post(close);
    return NO;
}

- (void)windowDidResize:(NSNotification*)notification {
    (void)notification;
    auto resize = _resize;
    if (resize) resize();
}
@end

namespace phpvpn::tray::mac {
bool isEscape(NSEvent* event) {
    if (!event) return false;
    if (event.keyCode == 53) return true;
    NSString* characters = event.charactersIgnoringModifiers;
    return characters.length == 1 && [characters characterAtIndex:0] == 0x1b;
}

bool isReturn(NSEvent* event) {
    if (!event) return false;
    if (event.keyCode == 36 || event.keyCode == 76) return true;
    NSString* characters = event.charactersIgnoringModifiers;
    if (characters.length != 1) return false;
    const unichar character = [characters characterAtIndex:0];
    return character == '\r' || character == 0x03;
}

PVWindow* makeWindow(std::string_view title, Size client, Size minimum, bool resizable) {
    NSWindowStyleMask mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
    if (resizable) mask |= NSWindowStyleMaskResizable;
    NSScreen* screen = screenForNewWindow();
    const NSRect visible = screen ? screen.visibleFrame : NSMakeRect(0, 0, 1440, 900);
    // The default client area, clamped to the work area (title bar allowance).
    const CGFloat width = std::max<CGFloat>(std::min<CGFloat>(client.width, visible.size.width - 16), minimum.width);
    const CGFloat height = std::max<CGFloat>(std::min<CGFloat>(client.height, visible.size.height - 40), minimum.height);
    PVWindow* window = [[PVWindow alloc] initWithContentRect:NSMakeRect(0, 0, width, height) styleMask:mask
                                                     backing:NSBackingStoreBuffered defer:NO];
    window.releasedWhenClosed = NO;
    window.delegate = window;
    window.title = ns(title);
    window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    window.backgroundColor = color(ui::Color::background);
    window.contentMinSize = NSMakeSize(minimum.width, minimum.height);
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    window.autorecalculatesKeyViewLoop = YES;
    const NSRect frame = [window frameRectForContentRect:NSMakeRect(0, 0, width, height)];
    const NSRect placed = NSMakeRect(NSMidX(visible) - frame.size.width / 2,
                                     std::max(NSMinY(visible), NSMidY(visible) - frame.size.height / 2),
                                     frame.size.width, std::min(frame.size.height, visible.size.height));
    [window setFrame:placed display:NO];
    return window;
}

PVStackView* makeStack(bool vertical, CGFloat spacing, NSEdgeInsets insets) {
    PVStackView* stack = [[PVStackView alloc] initVertical:(vertical ? YES : NO) spacing:spacing];
    stack.insets = insets;
    return stack;
}

PVStackView* makeRoot(PVWindow* window) {
    PVStackView* root = makeStack(true, 0);
    root.fillColor = color(ui::Color::background);
    window.contentView = root;
    return root;
}

PVStackView* makeCard(CGFloat padding, CGFloat spacing) {
    PVStackView* card = makeStack(true, spacing, NSEdgeInsetsMake(padding, padding, padding, padding));
    card.fillColor = color(ui::Color::surface);
    card.strokeColor = color(ui::Color::border);
    card.cornerRadius = ui::cardRadius;
    return card;
}

PVStackView* makePanel(PanelTone tone, CGFloat spacing) {
    PVStackView* panel = makeStack(true, spacing, NSEdgeInsetsMake(ui::cardPadding, ui::cardPadding, ui::cardPadding,
                                                                   ui::cardPadding));
    panel.cornerRadius = ui::cardRadius;
    setPanelTone(panel, tone);
    return panel;
}

void setPanelTone(PVStackView* panel, PanelTone tone) {
    if (!panel) return;
    switch (tone) {
        case PanelTone::success:
            panel.fillColor = color(ui::Color::successBackground);
            panel.strokeColor = color(ui::Color::successBorder);
            break;
        case PanelTone::warning:
            panel.fillColor = color(ui::Color::warningBackground);
            panel.strokeColor = color(ui::Color::warning);
            break;
        case PanelTone::error:
            panel.fillColor = color(ui::Color::errorBackground);
            panel.strokeColor = color(ui::Color::errorBorder);
            break;
    }
}

PVLineView* makeSeparator(bool vertical) {
    PVLineView* line = [[PVLineView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
    line.vertical = vertical ? YES : NO;
    line.lineColor = color(ui::Color::separator);
    return line;
}

Footer makeFooter() {
    Footer footer;
    footer.bar = makeStack(false, ui::fieldGap,
                           NSEdgeInsetsMake(ui::compactMargin, ui::compactMargin, ui::compactMargin, ui::compactMargin));
    footer.bar.wrapsWhenNarrow = YES;
    footer.leading = makeStack(false, ui::fieldGap);
    footer.trailing = makeStack(false, ui::fieldGap);
    [footer.bar addView:footer.leading item:natural()];
    [footer.bar addView:footer.trailing item:trailingItem()];
    return footer;
}
}  // namespace phpvpn::tray::mac
