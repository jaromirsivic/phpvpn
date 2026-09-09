#include "statistics_window.h"
#include "ui_controls.h"
#include "window_utils.h"
#include <windowsx.h>

namespace phpvpn::tray {
namespace {
constexpr wchar_t kClass[] = L"PhpVpnStatistics";
bool setText(HWND window, const std::wstring &value) {
    const int n = GetWindowTextLengthW(window);
    std::wstring old(n + 1, L'\0');
    GetWindowTextW(window, old.data(), n + 1);
    old.resize(n);
    if (old == value)
        return false;
    SetWindowTextW(window, value.c_str());
    return true;
}
} // namespace
StatisticsWindow::~StatisticsWindow() {
    if (window_)
        DestroyWindow(window_);
}
void StatisticsWindow::open(HINSTANCE instance, const TrayStatus &snapshot, bool unavailable) {
    instance_ = instance;
    if (!window_) {
        model_ = StatisticsModel{};
        scroll_ = 0;
        WNDCLASSEXW type{sizeof(type)};
        type.lpfnWndProc = procedure;
        type.hInstance = instance;
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.lpszClassName = kClass;
        RegisterClassExW(&type);
        window_ = CreateWindowExW(WS_EX_COMPOSITED | WS_EX_APPWINDOW | WS_EX_CONTROLPARENT, kClass, L"PHPVPN Statistics",
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 1, 1, nullptr, nullptr, instance, this);
        if (!window_)
            return;
        sizeAndCenter(window_, ui::statisticsClient);
        auto label = [&](const wchar_t *text) {
            return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 1, 1, window_,
                                   nullptr, instance, nullptr);
        };
        heading_ = label(L"Statistics");
        description_ = label(L"Read-only global service counters for the current VPN connection.");
        freshness_ = label(L"");
        const wchar_t *titles[]{L"Data polling", L"Serialized data", L"IPv4 traffic"};
        for (std::size_t i = 0; i < 3; ++i)
            titles_[i] = label(titles[i]);
        for (std::size_t i = 0; i < 9; ++i) {
            const auto &field = statisticsFields[i];
            labels_[i] = label(field.label);
            keys_[i] = CreateWindowExW(0, L"EDIT", widen(kCounterFields[field.counterIndex].key).c_str(),
                                       WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL, 0, 0, 1, 1, window_,
                                       nullptr, instance, nullptr);
            values_[i] = CreateWindowExW(
                0, L"EDIT", L"-", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_READONLY | ES_AUTOHSCROLL | ES_RIGHT, 0, 0, 1,
                1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(500 + i)), instance, nullptr);
            SetWindowSubclass(values_[i], fieldProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
        }
        updateResources();
    }
    model_.update(snapshot, unavailable);
    refresh();
    reactivateWindow(window_);
}
void StatisticsWindow::onSnapshot(const TrayStatus &snapshot, bool unavailable) {
    if (!window_)
        return;
    model_.update(snapshot, unavailable);
    refresh();
}
bool StatisticsWindow::translate(MSG &message) const {
    if (!window_ || (message.hwnd != window_ && !IsChild(window_, message.hwnd)))
        return false;
    if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
        SendMessageW(window_, WM_CLOSE, 0, 0);
        return true;
    }
    return IsDialogMessageW(window_, &message) != FALSE;
}
void StatisticsWindow::updateResources() {
    if (!window_ || !resources_.update(instance_, static_cast<int>(GetDpiForWindow(window_))))
        return;
    auto font = [&](HWND h, ui::Font role) {
        if (h)
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(role)), TRUE);
    };
    font(heading_, ui::Font::heading);
    font(description_, ui::Font::subtitle);
    font(freshness_, ui::Font::hint);
    for (auto h : titles_)
        font(h, ui::Font::card);
    for (auto h : labels_)
        font(h, ui::Font::label);
    for (auto h : keys_)
        font(h, ui::Font::mono);
    for (auto h : values_)
        font(h, ui::Font::counter);
    SendMessageW(window_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(resources_.icon(2)));
}
void StatisticsWindow::refresh() {
    bool changed = setText(freshness_, model_.freshness());
    for (std::size_t i = 0; i < 9; ++i)
        changed = setText(values_[i], model_.value(i)) || changed;
    if (changed || layout_.height == 0)
        layout();
}
void StatisticsWindow::layout() {
    if (!heading_ || layingOut_ || IsIconic(window_))
        return;
    LayoutFrame frame(window_);
    layingOut_ = true;
    RECT client{};
    GetClientRect(window_, &client);
    const int dpi = resources_.dpi();
    HDC dc = GetDC(window_);
    const int width = client.right, margin = scaleForDpi(width < scaleForDpi(760, dpi) ? 16 : 32, dpi);
    StatisticsMetrics metrics;
    metrics.descriptionHeight =
        measureText(dc, resources_.font(ui::Font::subtitle),
                    L"Read-only global service counters for the current VPN connection.", width - 2 * margin, true)
            .height;
    if (*model_.freshness())
        metrics.freshnessHeight =
            measureText(dc, resources_.font(ui::Font::hint), model_.freshness(), width - 2 * margin, true).height;
    for (std::size_t i = 0; i < 9; ++i) {
        metrics.valueWidths[i] = measureText(dc, resources_.font(ui::Font::counter), model_.value(i), 1, false).width;
        metrics.labelWidths[i] =
            measureText(dc, resources_.font(ui::Font::label), statisticsFields[i].label, 1, false).width;
        metrics.keyWidths[i] = measureText(dc, resources_.font(ui::Font::mono),
                                           widen(kCounterFields[statisticsFields[i].counterIndex].key), 1, false)
                                   .width;
    }
    layout_ = computeStatisticsLayout(width, dpi, metrics);
    ReleaseDC(window_, dc);
    scrollMax_ = std::max(0, layout_.height - static_cast<int>(client.bottom));
    scroll_ = std::clamp(scroll_, 0, scrollMax_);
    SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS};
    si.nMax = layout_.height - 1;
    si.nPage = client.bottom;
    si.nPos = scroll_;
    SetScrollInfo(window_, SB_VERT, &si, TRUE);
    RECT updated{};
    GetClientRect(window_, &updated);
    if (updated.right != client.right) {
        layingOut_ = false;
        layout();
        return;
    }
    std::vector<ControlPlacement> children;
    auto add = [&](HWND h, Rect r) {
        r.top -= scroll_;
        r.bottom -= scroll_;
        children.push_back({h, r});
    };
    add(heading_, layout_.heading);
    add(description_, layout_.description);
    add(freshness_, layout_.freshness);
    for (std::size_t i = 0; i < 3; ++i)
        add(titles_[i], layout_.titles[i]);
    for (std::size_t i = 0; i < 9; ++i) {
        add(labels_[i], layout_.rows[i].label);
        add(keys_[i], layout_.rows[i].key);
        add(values_[i], layout_.rows[i].value);
    }
    positionControls(window_, children);
    InvalidateRect(window_, nullptr, FALSE);
    layingOut_ = false;
}
void StatisticsWindow::scroll(int position) {
    LayoutFrame frame(window_);
    position = std::clamp(position, 0, scrollMax_);
    if (position == scroll_)
        return;
    scroll_ = position;
    layout();
}
LRESULT CALLBACK StatisticsWindow::fieldProcedure(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto *self = reinterpret_cast<StatisticsWindow *>(data);
    if (m == WM_SETFOCUS) {
        RECT client{};
        GetClientRect(self->window_, &client);
        for (std::size_t i = 0; i < 9; ++i)
            if (h == self->values_[i])
                self->scroll(revealScrollPosition(self->scroll_, client.bottom, self->layout_.height,
                                                  self->layout_.rows[i].value));
    }
    if (m == WM_NCDESTROY)
        RemoveWindowSubclass(h, fieldProcedure, 1);
    return DefSubclassProc(h, m, w, l);
}
LRESULT CALLBACK StatisticsWindow::procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto *self = reinterpret_cast<StatisticsWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<StatisticsWindow *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_PAINT:
        self->paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        self->layout();
        return 0;
    case WM_DPICHANGED:
        applyDpiRect(window, lParam);
        self->updateResources();
        self->layout();
        return 0;
    case WM_GETMINMAXINFO:
        setMinimumClientTrackSize(window, *reinterpret_cast<MINMAXINFO*>(lParam), {640, 480});
        return 0;
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
        self->updateResources();
        self->layout();
        break;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND h = reinterpret_cast<HWND>(lParam);
        auto background = ui::Color::surface, color = ui::Color::text;
        if (h == self->heading_ || h == self->description_ || h == self->freshness_)
            background = ui::Color::background;
        if (h == self->description_)
            color = ui::Color::secondaryText;
        if (h == self->freshness_)
            color = ui::Color::warning;
        for (auto key : self->keys_)
            if (key == h)
                color = ui::Color::secondaryText;
        SetTextColor(dc, self->resources_.color(color));
        SetBkColor(dc, self->resources_.color(background));
        return reinterpret_cast<LRESULT>(self->resources_.brush(background));
    }
    case WM_VSCROLL: {
        SCROLLINFO s{sizeof(s), SIF_ALL};
        GetScrollInfo(window, SB_VERT, &s);
        int next = self->scroll_;
        switch (LOWORD(wParam)) {
        case SB_LINEUP:
            next -= scaleForDpi(36, self->resources_.dpi());
            break;
        case SB_LINEDOWN:
            next += scaleForDpi(36, self->resources_.dpi());
            break;
        case SB_PAGEUP:
            next -= s.nPage;
            break;
        case SB_PAGEDOWN:
            next += s.nPage;
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            next = s.nTrackPos;
            break;
        case SB_TOP:
            next = 0;
            break;
        case SB_BOTTOM:
            next = s.nMax;
            break;
        }
        self->scroll(next);
        return 0;
    }
    case WM_MOUSEWHEEL:
        self->scroll(self->scroll_ -
                     GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * scaleForDpi(72, self->resources_.dpi()));
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        self->window_ = nullptr;
        return 0;
    case WM_NCDESTROY:
        self->heading_ = self->description_ = self->freshness_ = nullptr;
        self->titles_ = {};
        self->labels_ = {};
        self->keys_ = {};
        self->values_ = {};
        self->layout_ = {};
        self->resources_.clear();
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
void StatisticsWindow::paint() {
    PAINTSTRUCT ps{};
    auto targetDc = BeginPaint(window_, &ps); PaintBuffer buffer(targetDc); auto dc = buffer.dc();
    RECT client{};
    GetClientRect(window_, &client);
    FillRect(dc, &client, resources_.brush(ui::Color::background));
    for (auto r : layout_.cards) {
        r.top -= scroll_;
        r.bottom -= scroll_;
        paintCard(dc, r, resources_);
    }
    const int dpi = resources_.dpi();
    const auto line = [&](Rect r) {
        r.top -= scroll_;
        r.bottom -= scroll_;
        RECT native{r.left, r.top, r.right, r.bottom};
        FillRect(dc, &native, resources_.brush(ui::Color::separator));
    };
    for (std::size_t i : {0u, 1u, 3u, 5u, 7u}) {
        const auto &row = layout_.rows[i];
        const int y = std::max(row.key.bottom, row.value.bottom) + scaleForDpi(8, dpi);
        line({row.label.left, y, row.value.right, y + std::max(1, scaleForDpi(1, dpi))});
    }
    if (client.right >= scaleForDpi(760, dpi)) {
        const int x = (layout_.rows[5].value.right + layout_.rows[7].label.left) / 2;
        line({x, layout_.rows[5].label.top, x + std::max(1, scaleForDpi(1, dpi)), layout_.rows[8].value.bottom});
    }
    buffer.finish(); EndPaint(window_, &ps);
}
} // namespace phpvpn::tray
