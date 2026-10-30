#include "mac_http_headers_window.h"

#include "core/base/secure_buffer.h"
#include "wizard_dialog_text.h"

namespace phpvpn::tray {
namespace {
// tray_UX.md: default client area 760 x 420 DIP, minimum 480 x 260 DIP.
constexpr Size kHeadersClient{760, 420}, kHeadersMinimum{480, 260};
}  // namespace

bool HttpHeadersWindow::open(NSWindow* owner, const std::string& value, std::function<Status(std::string)> accept,
                             std::function<void()> closed) {
    if (window_) {
        mac::presentWindow(window_);
        return true;
    }
    if (!owner) return false;
    auto display = httpHeadersDisplayText(value);
    if (!display) return false;
    accept_ = std::move(accept);
    closed_ = std::move(closed);
    window_ = mac::makeWindow("HTTP headers", kHeadersClient, kHeadersMinimum);
    root_ = mac::makeRoot(window_);
    PVStackView* content = mac::makeStack(true, ui::relatedGap, NSEdgeInsetsMake(ui::cardPadding, ui::cardPadding, 0,
                                                                                 ui::cardPadding));
    const auto text = mac::makeTextScroll(true, true, "HTTP headers");
    field_ = text.text;
    mac::setTextViewString(field_, *display);
    secureWipe(display->data(), display->size());
    [content addView:text.scroll item:mac::withMinHeight(mac::flexibleItem(), 60)];
    [root_ addView:content item:mac::flexibleItem()];
    auto footer = mac::makeFooter();
    [footer.leading addView:mac::makeButton("Reset", mac::ButtonRole::destructive,
                                            [this] { deferred_.post([this] { command(Command::reset); }); })
                       item:mac::withMinWidth(mac::natural(), ui::buttonMinWidth)];
    [footer.trailing addView:mac::makeButton("Cancel", mac::ButtonRole::secondary,
                                             [this] { deferred_.post([this] { command(Command::cancel); }); })
                        item:mac::withMinWidth(mac::natural(), ui::buttonMinWidth)];
    [footer.trailing addView:mac::makeButton("OK", mac::ButtonRole::primary,
                                             [this] { deferred_.post([this] { command(Command::ok); }); })
                        item:mac::withMinWidth(mac::natural(), ui::buttonMinWidth)];
    [root_ addView:footer.bar];
    // Escape cancels the local edit; Enter in the field inserts a new line.
    [window_ setKeyHandler:[this](NSEvent* event) {
        if (!mac::isEscape(event)) return false;
        deferred_.post([this] { close(true); });
        return true;
    }];
    [window_ setCloseHandler:[this] { close(true); }];
    [window_ setResizeHandler:[this] { [root_ relayout]; }];
    [root_ relayout];
    [owner beginSheet:window_ completionHandler:nil];
    [window_ makeFirstResponder:field_];
    return true;
}

void HttpHeadersWindow::close(bool notify) {
    if (!window_) return;
    auto closed = notify ? std::move(closed_) : std::function<void()>{};
    closed_ = {};
    accept_ = {};
    PVWindow* window = window_;
    window_ = nil;
    // The headers may hold a cookie: the text leaves the view and its undo first.
    if (field_) mac::setTextViewString(field_, "");
    mac::closeWindow(window);
    root_ = nil;
    field_ = nil;
    if (closed) closed();
}

void HttpHeadersWindow::command(Command id) {
    if (!window_) return;
    if (id == Command::reset) {
        mac::setTextViewString(field_, defaultHttpHeadersDisplayText());
        [window_ makeFirstResponder:field_];
        return;
    }
    if (id == Command::cancel) {
        close();
        return;
    }
    if (!accept_) return;
    auto text = mac::strictUtf8(field_.string);
    // The error keeps the dialog and its text open.
    const Status result = text ? accept_(std::move(*text)) : fail(ErrorCode::config_encoding_error);
    if (text) secureWipe(text->data(), text->size());
    if (result) {
        close();
        return;
    }
    mac::showError(httpHeadersErrorText(result), "HTTP headers");
    if (window_ && field_) [window_ makeFirstResponder:field_];
}
}  // namespace phpvpn::tray
