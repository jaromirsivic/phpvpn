#include "system_dialog.h"
#include "server_info_window.h"
#include "ipc_timeouts.h"
#include "core/presentation/table_pagination.h"
#include "resource.h"
#include "server_info_layout.h"
#include "ui_controls.h"
#include "window_utils.h"
#include <algorithm>
namespace phpvpn::tray {
namespace {
constexpr UINT kResult = WM_APP + 271;
// CommCtrl.h hides these Windows 10 1809 messages at our generic Win10 target.
constexpr UINT kEditSetExtendedStyle = ECM_FIRST + 10;
constexpr LPARAM kEditAllowAllLineEndings = 0x0003;
constexpr int kRefresh = 7101, kTable = 7102, kAutoRefresh = 7103, kFirstPage = 7200;
constexpr wchar_t kClass[] = L"PHPVPN.ServerInformation";
std::wstring localTime(std::int64_t ms) {
    ULARGE_INTEGER ticks{};
    ticks.QuadPart = static_cast<ULONGLONG>(ms + 11644473600000LL) * 10000;
    FILETIME file{ticks.LowPart, ticks.HighPart};
    SYSTEMTIME utc{}, local{};
    if (!FileTimeToSystemTime(&file, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        return L"";
    wchar_t date[128]{}, time[128]{};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, date, 128, nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local, nullptr, time, 128);
    return std::wstring(date) + L" " + time;
}
} // namespace
void ServerInfoWindow::open(HINSTANCE instance) {
    if (window_) {
        stopAutoRefresh();
        if (model_)
            reactivateWindow(window_);
        return;
    }
    instance_ = instance;
    ++generation_;
    model_.reset();
    page_ = 0;
    comInitialised_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&accessibility_));
    WNDCLASSEXW type{sizeof(type)};
    type.lpfnWndProc = procedure;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.lpszClassName = kClass;
    RegisterClassExW(&type);
    window_ = CreateWindowExW(WS_EX_COMPOSITED | WS_EX_APPWINDOW | WS_EX_CONTROLPARENT, kClass, L"Server information",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 1, 1, nullptr, nullptr, instance, this);
    if (!window_) {
        endSession();
        return;
    }
    sizeAndCenter(window_, ui::editorClient);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&controls);
    auto create = [&](const wchar_t *cls, const wchar_t *text, DWORD style, int id) {
        return CreateWindowExW(0, cls, text, WS_CHILD | WS_CLIPSIBLINGS | style, 0, 0, 1, 1, window_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    };
    heading_ = create(L"STATIC", L"Server information", WS_VISIBLE | SS_NOPREFIX, 0);
    count_ = create(L"STATIC", L"", WS_VISIBLE | SS_NOPREFIX, 7104);
    tabs_ = create(WC_TABCONTROLW, L"", WS_VISIBLE | WS_TABSTOP | TCS_OWNERDRAWFIXED | TCS_FOCUSONBUTTONDOWN,
                   IDC_INFO_TABS);
    for (auto text : {L"Who is online", L"Raw server information"}) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t *>(text);
        TabCtrl_InsertItem(tabs_, TabCtrl_GetItemCount(tabs_), &item);
    }
    // The report header is a child window. Without clipping it, WS_EX_COMPOSITED
    // repeatedly repaints the list and header, even when the list has no rows.
    table_ =
        create(WC_LISTVIEWW, L"Who is online",
               WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS |
                   LVS_SHAREIMAGELISTS, kTable);
    SetWindowSubclass(table_, tableProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(ListView_GetHeader(table_), headerProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    ListView_SetExtendedListViewStyle(table_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    int index = 0;
    for (auto text : {L"Contract", L"Last seen", L"Status"}) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<wchar_t *>(text);
        column.cx = 250;
        ListView_InsertColumn(table_, index++, &column);
    }
    raw_ = create(L"EDIT", L"",
                  WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                  IDC_TEXT_AREA);
    SendMessageW(raw_, EM_SETLIMITTEXT, kIpcMaxMessageBytes, 0);
    // Render server LF and CRLF directly without rewriting authenticated text.
    SendMessageW(raw_, kEditSetExtendedStyle, kEditAllowAllLineEndings, kEditAllowAllLineEndings);
    refresh_ = create(L"BUTTON", L"Refresh", WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, kRefresh);
    autoRefresh_ = create(L"BUTTON", L"Auto refresh", WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, kAutoRefresh);
    ok_ = create(L"BUTTON", L"OK", WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, IDOK);
    notice_ = create(L"STATIC", L"", WS_VISIBLE | SS_NOPREFIX, 0);
    fonts();
    attachModernButton(refresh_, resources_, ButtonRole::primary);
    attachModernButton(ok_, resources_, ButtonRole::primary);
    attachToggleSwitch(autoRefresh_, resources_);
    layout();
    request();
}
void ServerInfoWindow::endSession() {
    stopAutoRefresh();
    ++generation_;
    reader_.abandon();
    pending_ = false;
    destroyPager();
    if (window_)
        DestroyWindow(window_);
    window_ = nullptr;
    heading_ = tabs_ = table_ = raw_ = refresh_ = ok_ = notice_ = nullptr;
    autoRefresh_ = count_ = nullptr;
    pager_.clear();
    model_.reset();
    if (rowHeightImages_) {
        ImageList_Destroy(rowHeightImages_);
        rowHeightImages_ = nullptr;
    }
    if (accessibility_) {
        accessibility_->Release();
        accessibility_ = nullptr;
    }
    if (comInitialised_) {
        CoUninitialize();
        comInitialised_ = false;
    }
}
void ServerInfoWindow::stopAutoRefresh() {
    if (window_ && autoTimer_) KillTimer(window_, autoTimer_);
    autoTimer_ = 0;
    autoDeadline_ = 0;
    if (autoRefresh_) SendMessageW(autoRefresh_, BM_SETCHECK, BST_UNCHECKED, 0);
}
void ServerInfoWindow::scheduleAutoRefresh() {
    if (window_ && autoTimer_) KillTimer(window_, autoTimer_);
    autoTimer_ = 0;
    if (!window_ || pending_ || SendMessageW(autoRefresh_, BM_GETCHECK, 0, 0) != BST_CHECKED) return;
    autoDeadline_ = GetTickCount64() + kServerInfoAutoRefreshMs;
    autoTimer_ = SetTimer(window_, ++nextTimer_, kServerInfoAutoRefreshMs, nullptr);
}
void ServerInfoWindow::request() {
    if (pending_ || !window_)
        return;
    pending_ = true;
    if (autoTimer_) KillTimer(window_, autoTimer_);
    autoTimer_ = 0;
    requestGeneration_ = generation_;
    EnableWindow(refresh_, FALSE);
    SetWindowTextW(notice_, L"Loading...");
    layout();
    if (!reader_.request(IpcCommand::get_server_info, {}, window_, kResult)) {
        pending_ = false;
        EnableWindow(refresh_, TRUE);
        SetWindowTextW(notice_, L"Server information is busy. Try Refresh again.");
        if (!model_) {
            showSystemMessage(window_, L"Server information is busy. Try opening it again.", L"PHPVPN", SystemDialogIcon::error);
            endSession();
        }
        scheduleAutoRefresh();
    }
}
void ServerInfoWindow::finish() {
    auto response = reader_.take();
    if (!response || !window_ || requestGeneration_ != generation_)
        return;
    pending_ = false;
    scheduleAutoRefresh();
    EnableWindow(refresh_, TRUE);
    auto snapshot = parseServerInfoResult(*response);
    if (!snapshot) {
        if (model_) {
            SetWindowTextW(notice_, L"Refresh failed. Previous information is retained.");
            layout();
        } else {
            showSystemMessage(
                window_,
                L"Server information could not be verified. Check the service, configuration and server compatibility.",
                L"PHPVPN", SystemDialogIcon::error);
            endSession();
        }
        return;
    }
    auto model = buildServerInfoModel(std::move(snapshot).value());
    if (!model) {
        if (model_) {
            SetWindowTextW(notice_, L"Refresh failed. Previous information is retained.");
            layout();
        } else {
            showSystemMessage(window_, L"Server information could not be verified.", L"PHPVPN", SystemDialogIcon::error);
            endSession();
        }
        return;
    }
    std::optional<Ipv4Address> selected;
    const int row = ListView_GetNextItem(table_, -1, LVNI_SELECTED);
    if (model_ && row >= 0 && static_cast<std::size_t>(page_ * kTablePageSize + row) < model_->rows.size())
        selected = model_->rows[page_ * kTablePageSize + row].peer;
    const bool first = !model_;
    const auto focus = GetFocus();
    const bool rawChanged = !model_ || model_->snapshot.infoPlaintext != model->snapshot.infoPlaintext;
    DWORD selectionStart = 0, selectionEnd = 0;
    SendMessageW(raw_, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart), reinterpret_cast<LPARAM>(&selectionEnd));
    const auto firstLine = SendMessageW(raw_, EM_GETFIRSTVISIBLELINE, 0, 0);
    LayoutFrame frame(window_);
    model_ = std::move(model).value();
    page_ = std::clamp(page_, 0, tablePageCount(model_->rows.size()) - 1);
    SetWindowTextW(notice_, L"");
    if (rawChanged) {
        const auto raw = widen(model_->snapshot.infoPlaintext);
        SetWindowTextW(raw_, raw.c_str());
        SendMessageW(raw_, EM_SETSEL, (std::min)(static_cast<std::size_t>(selectionStart), raw.size()),
                     (std::min)(static_cast<std::size_t>(selectionEnd), raw.size()));
        SendMessageW(raw_, EM_LINESCROLL, 0, firstLine);
    }
    display();
    if (selected)
        for (std::size_t i = 0; i < model_->rows.size(); ++i)
            if (model_->rows[i].peer == *selected) {
                page_ = static_cast<int>(i) / kTablePageSize;
                display();
                ListView_SetItemState(table_, static_cast<int>(i) % kTablePageSize, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
                break;
            }
    RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    if (first) {
        reactivateWindow(window_);
        SetFocus(tabs_);
    } else
        SetFocus(IsWindow(focus) ? focus : table_);
}
void ServerInfoWindow::display() {
    if (!model_)
        return;
    const auto countText = widen(serverInfoCountText(*model_));
    const auto title = L"Server information " + countText;
    SetWindowTextW(count_, countText.c_str());
    SetWindowTextW(window_, title.c_str());
    ListView_DeleteAllItems(table_);
    const auto start = static_cast<std::size_t>(page_ * kTablePageSize);
    const auto end = (std::min)(model_->rows.size(), start + kTablePageSize);
    for (auto i = start; i < end; ++i) {
        const auto &row = model_->rows[i];
        auto contract = widen(row.contract);
        auto time = localTime(row.lastSeenMs);
        auto state = widen(serverClientStateText(row.state));
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i - start);
        item.pszText = contract.data();
        ListView_InsertItem(table_, &item);
        ListView_SetItemText(table_, item.iItem, 1, time.data());
        ListView_SetItemText(table_, item.iItem, 2, state.data());
    }
    destroyPager();
    const int count = tablePageCount(model_->rows.size());
    if (count > 1) {
        auto add = [&](std::wstring text, int page, bool enabled = true, std::wstring name = L"") {
            auto h = CreateWindowExW(0, L"BUTTON", text.c_str(), WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1,
                                     window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFirstPage + pager_.size())),
                                     instance_, nullptr);
            pager_.push_back({h, page});
            attachModernButton(h, resources_, page == page_ ? ButtonRole::primary : ButtonRole::neutral);
            EnableWindow(h, enabled);
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::label)), TRUE);
            SetWindowPos(h, pager_.size() > 1 ? pager_[pager_.size() - 2].first : raw_, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (name.empty() && page >= 0)
                name = L"Page " + std::to_wstring(page + 1) + L" of " + std::to_wstring(count) +
                       (page == page_ ? L", current page" : L"");
            if (accessibility_)
                accessibility_->SetHwndPropStr(h, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, PROPID_ACC_NAME,
                                               name.c_str());
        };
        add(L"«", 0, page_ > 0, L"First page");
        add(L"‹", (std::max)(0, page_ - 1), page_ > 0, L"Previous page");
        int previous = -1;
        for (int page : tablePageNumbers(count, page_)) {
            if (page > previous + 1)
                add(L"…", -1, false);
            add(std::to_wstring(page + 1), page);
            previous = page;
        }
        add(L"›", (std::min)(count - 1, page_ + 1), page_ < count - 1, L"Next page");
        add(L"»", count - 1, page_ < count - 1, L"Last page");
    }
    selectTab();
    layout();
}
void ServerInfoWindow::page(int value) {
    if (!model_)
        return;
    page_ = std::clamp(value, 0, tablePageCount(model_->rows.size()) - 1);
    display();
    SetFocus(table_);
}
void ServerInfoWindow::destroyPager() {
    const MSAAPROPID properties[]{PROPID_ACC_NAME};
    for (auto &button : pager_) {
        if (accessibility_)
            accessibility_->ClearHwndProps(button.first, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, properties, 1);
        DestroyWindow(button.first);
    }
    pager_.clear();
}
void ServerInfoWindow::selectTab() {
    const bool who = TabCtrl_GetCurSel(tabs_) == 0;
    ShowWindow(table_, who ? SW_SHOW : SW_HIDE);
    ShowWindow(raw_, who ? SW_HIDE : SW_SHOW);
    for (auto &button : pager_)
        ShowWindow(button.first, who ? SW_SHOW : SW_HIDE);
    InvalidateRect(window_, nullptr, FALSE);
}
void ServerInfoWindow::fonts() {
    resources_.update(instance_, static_cast<int>(GetDpiForWindow(window_)), false);
    resources_.applyApplicationIcons(window_);
    resources_.applyApplicationIcons(window_);
    for (auto h : {tabs_, table_, refresh_, autoRefresh_, ok_, notice_})
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::value)), TRUE);
    SendMessageW(heading_, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::heading)), TRUE);
    SendMessageW(count_, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::heading)), TRUE);
    SendMessageW(raw_, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::mono)), TRUE);
    SendMessageW(table_, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::label)), TRUE);
    const auto images = ImageList_Create(1, scaleForDpi(40, resources_.dpi()), ILC_COLOR32, 1, 1);
    ListView_SetImageList(table_, images, LVSIL_SMALL);
    if (rowHeightImages_)
        ImageList_Destroy(rowHeightImages_);
    rowHeightImages_ = images;
    ListView_SetBkColor(table_, resources_.color(ui::Color::surface));
    ListView_SetTextColor(table_, resources_.color(ui::Color::text));
}
void ServerInfoWindow::layout() {
    if (!heading_ || !window_ || IsIconic(window_))
        return;
    RECT r{};
    GetClientRect(window_, &r);
    const int dpi = resources_.dpi();
    LayoutFrame frame(window_);
    auto p = [&](int n) { return scaleForDpi(n, dpi); };
    const int m = p(16), w = (std::max)(1, static_cast<int>(r.right) - 2 * m), bottom = static_cast<int>(r.bottom);
    auto dc = GetDC(window_);
    const auto old = SelectObject(dc, resources_.font(ui::Font::heading));
    wchar_t title[256]{};
    GetWindowTextW(heading_, title, 256);
    RECT measured{0, 0, w, 0};
    DrawTextW(dc, title, -1, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    const int baseWidth = measured.right, baseHeight = measured.bottom;
    wchar_t count[256]{};
    GetWindowTextW(count_, count, 256);
    RECT countRect{0, 0, w, 0};
    DrawTextW(dc, count, -1, &countRect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    const bool wrapCount = baseWidth + p(8) + countRect.right > w;
    measured.bottom = wrapCount ? baseHeight + countRect.bottom : (std::max<LONG>)(baseHeight, countRect.bottom);
    SelectObject(dc, old);
    ReleaseDC(window_, dc);
    wchar_t notice[256]{};
    GetWindowTextW(notice_, notice, 256);
    dc = GetDC(window_);
    const auto noticeFont = SelectObject(dc, resources_.font(ui::Font::value));
    RECT noticeRect{0, 0, w, 0};
    DrawTextW(dc, notice, -1, &noticeRect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, noticeFont);
    ReleaseDC(window_, dc);
    const auto geometry =
        computeServerInfoLayout({static_cast<int>(r.right), bottom}, dpi, measured.bottom,
                                notice[0] ? noticeRect.bottom + p(8) : 0, static_cast<int>(pager_.size()));
    const auto move = [](HWND h, Rect rect) {
        SetWindowPos(h, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
    };
    move(heading_, {m, m, m + baseWidth, m + baseHeight});
    const int countX = wrapCount ? m : m + baseWidth + p(8);
    const int countY = wrapCount ? m + baseHeight : m;
    move(count_, {countX, countY, countX + countRect.right, countY + countRect.bottom});
    move(tabs_, geometry.tabs);
    move(raw_, geometry.content);
    tableBounds_ = geometry.content;
    auto inner = tableBounds_;
    ++inner.left;
    inner.top += p(8);
    --inner.right;
    inner.bottom -= p(8);
    inner.right = (std::max)(inner.left + 1, inner.right);
    inner.bottom = (std::max)(inner.top + 1, inner.bottom);
    move(table_, inner);
    ListView_SetColumnWidth(table_, 0, p(280));
    ListView_SetColumnWidth(table_, 1, p(210));
    ListView_SetColumnWidth(table_, 2, (std::max)(p(420), w - p(494)));
    int x = geometry.pager.left;
    for (auto &h : pager_) {
        move(h.first, {x, geometry.pager.top, x + geometry.pagerButtonWidth, geometry.pager.bottom});
        x += geometry.pagerButtonWidth;
    }
    move(refresh_, geometry.refresh);
    move(autoRefresh_, geometry.autoRefresh);
    move(ok_, geometry.ok);
    move(notice_, geometry.notice);
    InvalidateRect(window_, nullptr, FALSE);
}
LRESULT CALLBACK ServerInfoWindow::headerProcedure(HWND h, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto *self = reinterpret_cast<ServerInfoWindow *>(data);
    const auto result = DefSubclassProc(h, message, w, l);
    if (message == HDM_LAYOUT && result && l) {
        auto *layout = reinterpret_cast<HDLAYOUT *>(l);
        layout->pwpos->cy = scaleForDpi(40, self->resources_.dpi());
        layout->prc->top = layout->pwpos->y + layout->pwpos->cy;
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(h, headerProcedure, 1);
    return result;
}
LRESULT CALLBACK ServerInfoWindow::tableProcedure(HWND h, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto *self = reinterpret_cast<ServerInfoWindow *>(data);
    if (message == WM_NOTIFY && l) {
        const auto *header = reinterpret_cast<NMHDR *>(l);
        if (header->hwndFrom == ListView_GetHeader(h) && header->code == NM_CUSTOMDRAW) {
            auto *draw = reinterpret_cast<NMCUSTOMDRAW *>(l);
            if (draw->dwDrawStage == CDDS_PREPAINT)
                return CDRF_NOTIFYITEMDRAW;
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
                FillRect(draw->hdc, &draw->rc, self->resources_.brush(ui::Color::contractsHeader));
                wchar_t text[64]{};
                HDITEMW item{};
                item.mask = HDI_TEXT;
                item.pszText = text;
                item.cchTextMax = 64;
                Header_GetItem(header->hwndFrom, static_cast<int>(draw->dwItemSpec), &item);
                auto rect = draw->rc;
                rect.left += scaleForDpi(12, self->resources_.dpi());
                const auto font = SelectObject(draw->hdc, self->resources_.font(ui::Font::contractsHeader));
                SetBkMode(draw->hdc, TRANSPARENT);
                SetTextColor(draw->hdc, self->resources_.color(ui::Color::primaryText));
                DrawTextW(draw->hdc, text, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                SelectObject(draw->hdc, font);
                return CDRF_SKIPDEFAULT;
            }
        }
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(h, tableProcedure, 1);
    return DefSubclassProc(h, message, w, l);
}
bool ServerInfoWindow::translate(MSG &message) {
    if (!window_ || (message.hwnd != window_ && !IsChild(window_, message.hwnd)))
        return false;
    if (message.message == WM_KEYDOWN) {
        if (message.wParam == VK_ESCAPE) {
            endSession();
            return true;
        }
        if (message.wParam == VK_RETURN && message.hwnd != raw_) {
            wchar_t cls[32]{};
            GetClassNameW(message.hwnd, cls, 32);
            if (std::wstring_view(cls) == L"Button")
                SendMessageW(message.hwnd, BM_CLICK, 0, 0);
            else
                endSession();
            return true;
        }
        if (message.hwnd == table_ && message.wParam == VK_END && (GetKeyState(VK_CONTROL) & 0x8000) && model_) {
            page(tablePageCount(model_->rows.size()) - 1);
            const int last = ListView_GetItemCount(table_) - 1;
            ListView_SetItemState(table_, last, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(table_, last, FALSE);
            return true;
        }
    }
    return IsDialogMessageW(window_, &message) != FALSE;
}
LRESULT CALLBACK ServerInfoWindow::procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto *self = reinterpret_cast<ServerInfoWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<ServerInfoWindow *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(window, message, w, l);
    switch (message) {
    case WM_TIMER:
        if (w == self->autoTimer_ && self->autoTimer_ && GetTickCount64() >= self->autoDeadline_) {
            KillTimer(window, self->autoTimer_);
            self->autoTimer_ = 0;
            if (!self->pending_ && SendMessageW(self->autoRefresh_, BM_GETCHECK, 0, 0) == BST_CHECKED)
                self->request();
        }
        return 0;
    case WM_SHOWWINDOW:
        if (!w) {
            self->stopAutoRefresh();
            ++self->generation_;
            self->reader_.abandon();
            self->pending_ = false;
            if (self->refresh_) EnableWindow(self->refresh_, TRUE);
        }
        break;
    case WM_DRAWITEM:
        if (l && drawModernTab(*reinterpret_cast<DRAWITEMSTRUCT *>(l), self->tabs_, self->resources_))
            return TRUE;
        break;
    case WM_GETMINMAXINFO:
        setMinimumClientTrackSize(window, *reinterpret_cast<MINMAXINFO *>(l), {320, 480});
        return 0;
    case kResult:
        self->finish();
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) == kAutoRefresh && HIWORD(w) == BN_CLICKED) {
            self->scheduleAutoRefresh();
            return 0;
        }
        if (LOWORD(w) == IDOK) {
            self->endSession();
            return 0;
        }
        if (LOWORD(w) == kRefresh) {
            self->request();
            return 0;
        }
        if (LOWORD(w) >= kFirstPage && LOWORD(w) < kFirstPage + self->pager_.size()) {
            self->page(self->pager_[LOWORD(w) - kFirstPage].second);
            return 0;
        }
        break;
    case WM_NOTIFY: {
        const auto *hdr = reinterpret_cast<NMHDR *>(l);
        if (hdr->hwndFrom == self->tabs_ && hdr->code == TCN_SELCHANGE) {
            self->selectTab();
            return 0;
        }
        if (hdr->hwndFrom == self->table_ && hdr->code == NM_CUSTOMDRAW) {
            auto *draw = reinterpret_cast<NMLVCUSTOMDRAW *>(l);
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
                return CDRF_NOTIFYITEMDRAW;
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && self->model_) {
                const auto index = static_cast<std::size_t>(self->page_ * kTablePageSize) + draw->nmcd.dwItemSpec;
                if (index < self->model_->rows.size()) {
                    const auto state = self->model_->rows[index].state;
                    draw->clrTextBk =
                        self->resources_.color(state == ServerClientState::valid     ? ui::Color::serverInfoValid
                                               : state == ServerClientState::invalid ? ui::Color::contractsChanged
                                                                                     : ui::Color::disabledBackground);
                    draw->clrText = self->resources_.color(ui::Color::text);
                }
                return CDRF_NEWFONT;
            }
        }
        break;
    }
    case WM_SIZE:
        self->layout();
        return 0;
    case WM_DPICHANGED:
        applyDpiRect(window, l);
        self->fonts();
        self->layout();
        return 0;
    case WM_SETTINGCHANGE:
        self->fonts();
        self->layout();
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        FillRect(dc, &client, self->resources_.brush(ui::Color::background));
        if (self->table_ && IsWindowVisible(self->table_))
            paintContractsTable(dc, self->tableBounds_, scaleForDpi(48, self->resources_.dpi()), self->resources_);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND: {
        RECT r{};
        GetClientRect(window, &r);
        FillRect(reinterpret_cast<HDC>(w), &r, self->resources_.brush(ui::Color::background));
        return 1;
    }
    case WM_CTLCOLORSTATIC:
        SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
        SetTextColor(reinterpret_cast<HDC>(w), self->resources_.color(reinterpret_cast<HWND>(l) == self->count_ ? ui::Color::success : ui::Color::text));
        return reinterpret_cast<LRESULT>(self->resources_.brush(ui::Color::background));
    case WM_CLOSE:
        self->endSession();
        return 0;
    case WM_NCDESTROY:
        self->stopAutoRefresh();
        ++self->generation_;
        self->pending_ = false;
        self->reader_.abandon();
        self->window_ = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, w, l);
}
} // namespace phpvpn::tray
