#include "mac_http_error_detail_window.h"

#include "wizard_dialog_text.h"

namespace phpvpn::tray {
namespace {
constexpr Size kDetailClient{1000, 680}, kDetailMinimum{480, 400};
}  // namespace

bool HttpErrorDetailWindow::open(NSWindow* owner, const HttpDiagnostic& diagnostic) {
    if (window_) {
        mac::presentWindow(window_);
        return true;
    }
    if (!owner) return false;
    auto pages = httpErrorDetailPages(diagnostic);
    if (!pages) return false;
    window_ = mac::makeWindow("Error detail", kDetailClient, kDetailMinimum);
    PVStackView* root = mac::makeRoot(window_);
    PVStackView* content = mac::makeStack(true, 0, NSEdgeInsetsMake(ui::relatedGap, ui::relatedGap, ui::relatedGap,
                                                                    ui::relatedGap));
    [root addView:content item:mac::flexibleItem()];
    NSTabView* tabs = [[NSTabView alloc] initWithFrame:NSMakeRect(0, 0, 600, 400)];
    tabs.font = mac::font(ui::Font::label);
    [content addView:tabs item:mac::flexibleItem()];
    const char* titles[]{"Request", "Response"};
    for (std::size_t i = 0; i < 2; ++i) {
        PVStackView* page = mac::makeStack(true, ui::fieldGap, NSEdgeInsetsMake(ui::fieldGap, ui::fieldGap, ui::fieldGap,
                                                                                ui::fieldGap));
        page.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        NSTextField* note = mac::makeLabel((*pages)[i].note, ui::Font::label, ui::Color::text, true);
        note.selectable = YES;
        [page addView:note];
        // Read-only but selectable and copyable; the content is never interpreted.
        const auto text = mac::makeTextScroll(false, true, titles[i]);
        mac::setTextViewString(text.text, (*pages)[i].text);
        texts_[i] = text.text;
        [page addView:text.scroll item:mac::withMinHeight(mac::flexibleItem(), 60)];
        NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:mac::ns(titles[i])];
        item.label = mac::ns(titles[i]);
        item.view = page;
        [tabs addTabViewItem:item];
    }
    [window_ setCloseHandler:[this] { close(); }];
    [window_ setKeyHandler:[this](NSEvent* event) {
        if (!mac::isEscape(event)) return false;
        close();
        return true;
    }];
    [window_ setResizeHandler:[root] { [root relayout]; }];
    [root relayout];
    mac::presentWindow(window_);
    [window_ makeFirstResponder:tabs];
    return true;
}

void HttpErrorDetailWindow::close() {
    if (!window_) return;
    PVWindow* window = window_;
    window_ = nil;
    // The raw HTTP text leaves the views before they are released.
    for (auto& text : texts_) {
        if (text) mac::setTextViewString(text, "");
        text = nil;
    }
    mac::closeWindow(window);
}
}  // namespace phpvpn::tray
