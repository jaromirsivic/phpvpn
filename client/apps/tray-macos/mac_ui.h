// Shared AppKit layer of the macOS tray (Objective-C++, ARC): palette and font roles
// of ui_style.h, the main run loop dispatcher, timers, modal dialogs, control
// factories, a small manual layout engine and the window helpers. Every function
// and class here runs on the main thread only.
//
// Layout: windows are built from PVStackView (a flipped, frame based vertical or
// horizontal stack with measured text wrapping, cards, panels and footers that wrap
// when narrow) instead of Auto Layout constraints, so geometry follows the DIP
// constants of ui_style.h directly (1 DIP = 1 point) and stays deterministic.
#pragma once
#import <AppKit/AppKit.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "ipc_async.h"
#include "layout.h"
#include "ui_style.h"

namespace phpvpn::tray::mac {
// --- text --------------------------------------------------------------------------
// UTF-8 to NSString; invalid UTF-8 yields an empty string (never a partial text).
NSString* ns(std::string_view text);
// NSString to UTF-8; nil yields an empty string.
std::string stdString(NSString* text);
// Lossless UTF-8 or nullopt (an unpaired surrogate cannot be encoded).
std::optional<std::string> strictUtf8(NSString* text);
// The exact UTF-16 code units (unpaired surrogates included) and back.
std::u16string utf16(NSString* text);
NSString* nsUtf16(std::u16string_view text);

// --- palette and fonts (tray_UX.md UX01) ------------------------------------------
NSColor* color(ui::Color token);
// Font role of ui_style.h. The DIP values are logical font heights (cell height),
// so the point size is height x 0.85, the line height ratio of the system font.
NSFont* font(ui::Font role);

// --- main run loop -------------------------------------------------------------------
// Runs task on the main thread from the main run loop in all common modes, so it also
// runs while a modal NSAlert, a sheet or the status menu is open. Never inline.
void post(std::function<void()> task);

// Owner scoped deferred calls (the GTK DeferredCalls): pending calls are dropped when
// the owner is destroyed.
class Deferred {
public:
    Deferred() : alive_(std::make_shared<bool>(true)) {}
    ~Deferred() { *alive_ = false; }
    Deferred(const Deferred&) = delete;
    Deferred& operator=(const Deferred&) = delete;
    void post(std::function<void()> call);

private:
    std::shared_ptr<bool> alive_;
};

// NSTimer in the common run loop modes (fires during modal alerts and menu tracking).
class Timer {
public:
    Timer() = default;
    ~Timer() { stop(); }
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    void start(std::int64_t intervalMs, std::function<void()> tick, bool repeats = true);
    void stop();
    bool active() const;

private:
    NSTimer* timer_ = nil;
};

// --- modal dialogs ---------------------------------------------------------------------
// App modal NSAlert (runModal), the counterpart of the Win32 MessageBox and the GTK
// gtk_dialog_run: IPC deliveries and timers keep running while it is open. The title
// is the alert heading, the text its body.
enum class DialogIcon { information, warning, error };
void showMessage(std::string_view text, std::string_view title, DialogIcon icon = DialogIcon::information);
inline void showError(std::string_view text, std::string_view title = "PHPVPN") {
    showMessage(text, title, DialogIcon::error);
}
// Yes/No question. Destructive questions default to No (Return answers No); only
// the non-destructive reconnect question passes defaultYes.
bool confirm(std::string_view text, std::string_view title, bool defaultYes = false);
// Terminal session end: stops the running alert, which then answers No.
void closeAllDialogs();
bool dialogOpen();

// --- controls ----------------------------------------------------------------------------
// Target/action bound to a std::function; the target lives as long as the control.
void setAction(NSControl* control, std::function<void()> action);
void setMenuAction(NSMenuItem* item, std::function<void()> action);
// Single click on a non-control view (for example the Contracts error message).
void setClickAction(NSView* view, std::function<void()> action);
// Called after every user edit of the field or text view (controlTextDidChange /
// textDidChange); programmatic changes are not reported.
void setTextChanged(NSTextField* field, std::function<void()> changed);
void setTextChanged(NSTextView* view, std::function<void()> changed);
// Preflight of every user edit of a single-line field with the whole proposed text
// (NSFormatter partial validation); a refused edit is rejected as a whole.
void setEditCheck(NSTextField* field, std::function<bool(NSString* proposed)> check);

NSTextField* makeLabel(std::string_view text, ui::Font role, ui::Color tone = ui::Color::text, bool wrap = false);
// Only a real change touches the control (selection and caret survive polling).
void setLabelText(NSTextField* label, std::string_view text);
void setTextColor(NSTextField* label, ui::Color tone);
// Editable single-line field without wrapping; long text scrolls horizontally.
NSTextField* makeEntry(ui::Font role = ui::Font::value, std::string_view accessibleName = {});
// Read-only single-line field: focusable, selectable and copyable, tinted background.
NSTextField* makeReadOnlyField(std::string_view text, ui::Font role, std::string_view accessibleName = {});
void setFieldText(NSTextField* field, std::string_view text);
// Red frame and accessible description of an invalid field (UX02); restores the normal state.
void setFieldInvalid(NSTextField* field, bool invalid, std::string_view description = {});

enum class ButtonRole { primary, secondary, destructive };
NSButton* makeButton(std::string_view title, ButtonRole role, std::function<void()> action);
void setButtonTitle(NSButton* button, std::string_view title);
void setButtonRole(NSButton* button, ButtonRole role);
void setVisible(NSView* view, bool visible);

// Switch with a clickable label: the label is part of the click target, Space toggles
// the focused switch, the accessible name is the label (UX02).
struct SwitchRow {
    NSView* row = nil;
    NSSwitch* toggle = nil;
    NSTextField* label = nil;
};
SwitchRow makeSwitchRow(std::string_view title, std::function<void(bool on)> changed);
bool switchOn(NSSwitch* toggle);
// Programmatic state without the change callback.
void setSwitchOn(NSSwitch* toggle, bool on);

// Multi-line text view in a scroll view with both scrollers and no wrapping; rich
// text, smart quotes, dashes, substitutions and spell checking are off.
struct TextScroll {
    NSScrollView* scroll = nil;
    NSTextView* text = nil;
};
TextScroll makeTextScroll(bool editable, bool mono, std::string_view accessibleName = {});
// Replaces the text and clears the undo history.
void setTextViewString(NSTextView* view, std::string_view text);
std::string textViewString(NSTextView* view);

// --- windows -----------------------------------------------------------------------------
// Size of a new window, clamped to the visible frame of the screen under the mouse.
NSScreen* screenForNewWindow();
void presentWindow(NSWindow* window);
// Detaches the window handlers, orders it out and closes it.
void closeWindow(NSWindow* window);
bool windowVisible(NSWindow* window);
void setCloseEnabled(NSWindow* window, bool enabled);

// --- layout --------------------------------------------------------------------------------
struct StackItem {
    // Vertical stack: take the full inner width. Horizontal stack: take the row height.
    bool fill = true;
    // Takes the remaining space along the stack axis (one per stack).
    bool flexible = false;
    // Packed from the trailing edge (horizontal) or right aligned (vertical, not fill).
    bool trailing = false;
    // Horizontal stack: align to the top instead of the vertical centre.
    bool top = false;
    // Fixed size along an axis (0: measured).
    CGFloat width = 0, height = 0;
    CGFloat minWidth = 0, minHeight = 0;
    // Gap before this item instead of the stack spacing (< 0: stack spacing).
    CGFloat gapBefore = -1;
};
inline StackItem natural() { StackItem item; item.fill = false; return item; }
inline StackItem trailingItem() { StackItem item; item.fill = false; item.trailing = true; return item; }
inline StackItem flexibleItem() { StackItem item; item.flexible = true; return item; }
inline StackItem fixedWidth(CGFloat width) { StackItem item; item.fill = false; item.width = width; return item; }
inline StackItem fixedHeight(CGFloat height) { StackItem item; item.height = height; return item; }
inline StackItem withGap(StackItem item, CGFloat gap) { item.gapBefore = gap; return item; }
inline StackItem withMinWidth(StackItem item, CGFloat width) { item.minWidth = width; return item; }
inline StackItem withMinHeight(StackItem item, CGFloat height) { item.minHeight = height; return item; }

// Natural single-line size and the height at a given width (wrapping labels and stacks).
NSSize naturalSize(NSView* view);
CGFloat heightForWidth(NSView* view, CGFloat width);
}  // namespace phpvpn::tray::mac

@interface PVStackView : NSView
@property (nonatomic) BOOL vertical;
@property (nonatomic) CGFloat spacing;
@property (nonatomic) NSEdgeInsets insets;
// Background, border and corner radius (cards and panels).
@property (nonatomic, strong) NSColor* fillColor;
@property (nonatomic, strong) NSColor* strokeColor;
@property (nonatomic) CGFloat cornerRadius;
// Horizontal stack: lay out vertically when the items do not fit side by side.
@property (nonatomic) BOOL wrapsWhenNarrow;
// Horizontal stack: always lay out vertically (narrow windows).
@property (nonatomic) BOOL forceVertical;
// Horizontal stack: equal item widths.
@property (nonatomic) BOOL homogeneous;
// Horizontal layout: one pixel lines between the items (column separators).
@property (nonatomic, strong) NSColor* dividerColor;
- (instancetype)initVertical:(BOOL)vertical spacing:(CGFloat)spacing;
- (void)addView:(NSView*)view;
- (void)addView:(NSView*)view item:(phpvpn::tray::mac::StackItem)item;
- (void)insertView:(NSView*)view item:(phpvpn::tray::mac::StackItem)item atIndex:(NSUInteger)index;
- (void)setItem:(phpvpn::tray::mac::StackItem)item forView:(NSView*)view;
- (void)removeView:(NSView*)view;
- (void)removeAllViews;
- (CGFloat)heightForWidth:(CGFloat)width;
- (CGFloat)naturalWidth;
// Lays out the items and every nested stack and scroll document again.
- (void)relayout;
@end

// Vertical scroll container of a PVStackView document that follows the clip width.
@interface PVScrollView : NSScrollView
@property (nonatomic, strong, readonly) PVStackView* stack;
- (instancetype)initWithStack:(PVStackView*)stack;
- (void)relayoutDocument;
@end

// One pixel separator line.
@interface PVLineView : NSView
@property (nonatomic) BOOL vertical;
@property (nonatomic, strong) NSColor* lineColor;
@end

// Round step marker of the Wizard sidebar.
@interface PVMarkerView : NSView
@property (nonatomic, copy) NSString* text;
@property (nonatomic, strong) NSColor* circleColor;
@end

// Label that reports its real paint (the Connect/Disconnect success countdown starts
// at the first drawn result heading, tray_UX.md UX05).
@interface PVPaintLabel : NSTextField
- (void)setPaintHandler:(std::function<void()>)handler;
@end

// Multi-line text view of the tray: Tab and Shift+Tab move the focus to the next or
// previous control instead of inserting a tab (UX06, HTTP headers).
@interface PVTextView : NSTextView
@property (nonatomic) BOOL tabMovesFocus;
@end

// Window with handlers for the title bar close button, unmodified Escape/Return and
// resizing. Its own delegate. Light appearance (UX03: light client content also in
// dark mode).
@interface PVWindow : NSWindow <NSWindowDelegate>
// Close button / Command-W: the handler decides; the window never closes by itself.
- (void)setCloseHandler:(std::function<void()>)handler;
// Escape (cancelOperation:) and Return/Enter without Command, Control or Option.
// Return true when handled.
- (void)setKeyHandler:(std::function<bool(NSEvent* event)>)handler;
- (void)setResizeHandler:(std::function<void()>)handler;
- (void)pv_clearHandlers;
@end

namespace phpvpn::tray::mac {
PVWindow* makeWindow(std::string_view title, Size client, Size minimum, bool resizable = true);
// Content root with the window background: vertical stack without insets.
PVStackView* makeRoot(PVWindow* window);
PVStackView* makeStack(bool vertical, CGFloat spacing, NSEdgeInsets insets = NSEdgeInsetsMake(0, 0, 0, 0));
// White card with border and 8 DIP corner radius (UX01).
PVStackView* makeCard(CGFloat padding = ui::cardPadding, CGFloat spacing = ui::fieldGap);
// Coloured information panel: success, warning or error palette.
enum class PanelTone { success, warning, error };
PVStackView* makePanel(PanelTone tone, CGFloat spacing = ui::fieldGap);
void setPanelTone(PVStackView* panel, PanelTone tone);
PVLineView* makeSeparator(bool vertical = false);
// Footer of the Wizard geometry (UX06): 16 DIP margin, 8 DIP gaps; leading and
// trailing groups that wrap onto two rows when narrow.
struct Footer {
    PVStackView* bar = nil;
    PVStackView* leading = nil;
    PVStackView* trailing = nil;
};
Footer makeFooter();
bool isEscape(NSEvent* event);
bool isReturn(NSEvent* event);
}  // namespace phpvpn::tray::mac
