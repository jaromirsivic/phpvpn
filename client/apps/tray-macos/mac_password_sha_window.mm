#include "mac_password_sha_window.h"

#include "core/base/secure_buffer.h"

namespace phpvpn::tray {
namespace {
void wipe(std::u16string& text) { secureWipe(text.data(), text.size() * sizeof(char16_t)); }
}  // namespace

PasswordShaWindow::~PasswordShaWindow() { dismiss(); }

bool PasswordShaWindow::open(NSWindow* parent) {
    (void)parent;
    if (window_) {
        mac::presentWindow(window_);
        return true;
    }
    // The immutable title of the captured target (UX11), never of a later focus.
    window_ = mac::makeWindow(target_.passwordTitle(), ui::passwordClient, Size{480, 240});
    root_ = mac::makeRoot(window_);
    PVStackView* body = mac::makeStack(true, 0, NSEdgeInsetsMake(ui::cardPadding, ui::cardPadding, ui::fieldGap,
                                                                 ui::cardPadding));
    PVScrollView* scroller = [[PVScrollView alloc] initWithStack:body];
    [root_ addView:scroller item:mac::flexibleItem()];

    [body addView:mac::makeLabel(kPasswordHeading, ui::Font::card)];
    [body addView:mac::makeLabel(kPasswordDescription, ui::Font::label, ui::Color::text, true)
             item:mac::withGap(mac::StackItem{}, ui::fieldGap)];
    [body addView:mac::makeLabel(kPasswordLabel, ui::Font::label) item:mac::withGap(mac::StackItem{}, ui::relatedGap)];
    // The password is visible as in the mockup; the cue text is never hashed.
    input_ = mac::makeEntry(ui::Font::value, kPasswordLabel);
    input_.placeholderString = mac::ns(kPasswordCue);
    mac::setEditCheck(input_, [this](NSString* proposed) { return preflight(proposed); });
    mac::setTextChanged(input_, [this] {
        auto text = mac::utf16(input_.stringValue);
        update(text);
        wipe(text);
    });
    // Enter confirms the current valid hash.
    mac::setAction(input_, [this] { confirm(); });
    [body addView:input_ item:mac::withGap(mac::StackItem{}, ui::fieldGap)];
    [body addView:mac::makeLabel(kPasswordHint, ui::Font::hint, ui::Color::secondaryText, true)
             item:mac::withGap(mac::StackItem{}, ui::fieldGap)];
    [body addView:mac::makeLabel(kPasswordKeyLabel, ui::Font::label) item:mac::withGap(mac::StackItem{}, ui::relatedGap)];
    // Read-only but focusable and selectable; the long hash scrolls horizontally.
    hash_ = mac::makeReadOnlyField("", ui::Font::mono, kPasswordKeyLabel);
    [body addView:hash_ item:mac::withGap(mac::StackItem{}, ui::fieldGap)];
    notice_ = mac::makeLabel("", ui::Font::label, ui::Color::error, true);
    [body addView:notice_ item:mac::withGap(mac::StackItem{}, ui::spacing)];

    [root_ addView:mac::makeSeparator() item:mac::fixedHeight(1)];
    auto footer = mac::makeFooter();
    [footer.leading addView:mac::makeButton("Default", mac::ButtonRole::destructive, [this] { usePairDefault(); })
                       item:mac::withMinWidth(mac::natural(), ui::buttonMinWidth)];
    [footer.leading addView:mac::makeButton("Random", mac::ButtonRole::destructive, [this] { randomPassword(); })
                       item:mac::withMinWidth(mac::natural(), ui::buttonMinWidth)];
    ok_ = mac::makeButton("OK", mac::ButtonRole::primary, [this] { confirm(); });
    [footer.trailing addView:ok_ item:mac::withMinWidth(mac::natural(), ui::buttonMinWidth)];
    [root_ addView:footer.bar];

    [window_ setCloseHandler:[this] { close(); }];
    [window_ setKeyHandler:[this](NSEvent* event) {
        if (mac::isEscape(event)) {
            close();
            return true;
        }
        if ([window_.firstResponder isKindOfClass:[NSButton class]]) return false;
        confirm();
        return true;
    }];
    [window_ setResizeHandler:[this] { [root_ relayout]; }];

    update(u"");
    [root_ relayout];
    // Above the available parent, but never modal: the parent stays usable (UX11).
    mac::presentWindow(window_);
    [window_ makeFirstResponder:input_];
    return true;
}

bool PasswordShaWindow::preflight(NSString* proposed) {
    // Win32 ContractsEdit::accept: the whole proposal is checked before the field
    // changes; an over-limit or unsupported insertion is rejected as a whole and
    // keeps the previous input, selection and valid result.
    auto text = mac::utf16(proposed);
    bool accepted = false;
    if (text.find(u'\0') != std::u16string::npos) accepted = update(std::u16string_view(u"\0", 1));
    else if (text.size() > kPasswordBound) accepted = update(std::u16string(kPasswordBound + 1, u'x'));
    else accepted = update(text);
    wipe(text);
    return accepted;
}

bool PasswordShaWindow::update(std::u16string_view text) {
    const auto error = model_.update(crypto_, text);
    if (hash_) {
        const std::string key(model_.key());
        mac::setFieldText(hash_, key);
    }
    refreshStatus();
    return error != PasswordKeyError::tooLong && error != PasswordKeyError::unsupportedControl;
}

void PasswordShaWindow::setTargetEnabled(bool enabled) {
    if (targetEnabled_ == enabled) return;
    targetEnabled_ = enabled;
    if (window_) refreshStatus();
}

void PasswordShaWindow::refreshStatus() {
    if (!window_) return;
    const auto notice = passwordShaNotice(model_.error(), targetEnabled_);
    mac::setLabelText(notice_, notice);
    mac::setVisible(notice_, !notice.empty());
    ok_.enabled = targetEnabled_ && model_.valid() ? YES : NO;
    [root_ relayout];
}

void PasswordShaWindow::setInput(std::u16string_view text) {
    if (!input_) return;
    input_.stringValue = mac::nsUtf16(text);
    update(text);
}

void PasswordShaWindow::randomPassword() {
    std::u16string password;
    if (!contractsRandomPassword(crypto_, password)) {
        mac::setLabelText(notice_, kPasswordRandomFailed);
        mac::setVisible(notice_, true);
        [root_ relayout];
        return;
    }
    setInput(password);
    wipe(password);
}

void PasswordShaWindow::usePairDefault() {
    // The captured numeric pair, with no Wizard candidate-range restriction.
    auto password = contractsUtf16(target_.contractId());
    setInput(password);
    wipe(password);
}

void PasswordShaWindow::confirm() {
    if (!window_ || !targetEnabled_ || !model_.valid()) return;
    if (apply_ && apply_(target_, model_.key())) close();
}

void PasswordShaWindow::close() {
    if (!window_) return;
    destroy();
    if (closed_) {
        auto closed = closed_;
        closed(target_);
    }
}

void PasswordShaWindow::dismiss() {
    closed_ = {};
    destroy();
}

void PasswordShaWindow::destroy() {
    if (!window_) return;
    PVWindow* window = window_;
    window_ = nil;
    if (input_) input_.stringValue = @"";
    if (hash_) hash_.stringValue = @"";
    model_.clear();
    mac::closeWindow(window);
    root_ = nil;
    input_ = nil;
    hash_ = nil;
    notice_ = nil;
    ok_ = nil;
}
}  // namespace phpvpn::tray
