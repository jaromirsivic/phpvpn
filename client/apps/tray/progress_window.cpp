#include "progress_window.h"
#include "resource.h"
#include "ui_controls.h"
#include "window_utils.h"
#include <windowsx.h>

namespace phpvpn::tray {
namespace {
constexpr wchar_t kClass[] = L"PhpVpnConnectingProgress";
constexpr UINT kSnapshotMessage = WM_APP + 21, kDetailsMessage = WM_APP + 22;
constexpr UINT_PTR kTimer = 2;
void setText(HWND window, const std::wstring &value) {
    const int length = GetWindowTextLengthW(window);
    std::wstring old(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(window, old.data(), length + 1);
    old.resize(length);
    if (old != value)
        SetWindowTextW(window, value.c_str());
}
const wchar_t *outcome(CheckpointResult check) {
    return check == CheckpointResult::success ? L"Success" : check == CheckpointResult::failed ? L"Failed" : L"Pending";
}
ui::Color checkColor(CheckpointResult check) {
    return check == CheckpointResult::success  ? ui::Color::success
           : check == CheckpointResult::failed ? ui::Color::error
                                               : ui::Color::warning;
}
} // namespace
ProgressWindow::~ProgressWindow() {
    if (window_)
        DestroyWindow(window_);
}
void ProgressWindow::open(HINSTANCE instance, bool disconnect) {
    errorDetail_.close();
    diagnosticEpoch_ = 0;
    diagnosticAttemptId_.clear();
    reader_.stop();
    detailsReader_.stop();
    readPending_ = false;
    disconnect_ = disconnect;
    waitingForReply_ = disconnect;
    model_ = ProgressModel{disconnect};
    details_ = {};
    scroll_ = 0;
    instance_ = instance;
    if (!window_) {
        WNDCLASSEXW type{sizeof(type)};
        type.lpfnWndProc = procedure;
        type.hInstance = instance;
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.lpszClassName = kClass;
        RegisterClassExW(&type);
        window_ = CreateWindowExW(WS_EX_COMPOSITED | WS_EX_APPWINDOW | WS_EX_CONTROLPARENT, kClass, L"PHPVPN connection",
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 1, 1, nullptr, nullptr, instance, this);
        if (!window_)
            return;
        sizeAndCenter(window_, disconnect_ ? ui::disconnectClient : ui::connectClient);
        auto label = [&](const wchar_t *text, int id = 0) {
            return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 1, 1, window_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        };
        eyebrow_ = label(L"", IDC_PROGRESS_CONTEXT);
        detailsHeading_ = label(L"Connection details");
        const wchar_t *names[]{L"Server URL", L"Virtual IP"};
        for (std::size_t i = 0; i < 2; ++i) {
            labels_[i] = label(names[i]);
            fields_[i] = CreateWindowExW(
                0, L"EDIT", L"Unavailable", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_READONLY | ES_AUTOHSCROLL, 0, 0, 1,
                1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(410 + i)), instance, nullptr);
            SetWindowSubclass(fields_[i], fieldProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
        }
        checksHeading_ = label(L"Connection checks");
        for (auto &check : checks_)
            check = label(L"");
        const wchar_t *namesOfChecks[]{L"Server URL", L"Server secret", L"Access key", L"Communication contracts"};
        for (std::size_t i = 0; i < 4; ++i) {
            checkLabels_[i] = label(namesOfChecks[i]);
            checkIcons_[i] = label(L"");
        }
        panelHeading_ = label(L"");
        SetWindowSubclass(panelHeading_, headingProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
        panelAction_ = label(L"");
        countdown_ = label(L"");
        errorDetailButton_ = CreateWindowExW(0, L"BUTTON", L"Error detail", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 1, 1, window_, reinterpret_cast<HMENU>(412), instance, nullptr);
        attachModernButton(errorDetailButton_, resources_, ButtonRole::error);
        SetWindowSubclass(errorDetailButton_, fieldProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    }
    setText(eyebrow_, disconnect_ ? L"Disconnecting client from the server." : L"Connecting client to the server.");
    SetWindowTextW(window_, disconnect_ ? L"PHPVPN disconnection" : L"PHPVPN connection");
    for (auto h : {detailsHeading_, checksHeading_, labels_[0], labels_[1], fields_[0], fields_[1],
                  checks_[0], checks_[1], checks_[2], checks_[3], checkLabels_[0], checkLabels_[1], checkLabels_[2], checkLabels_[3],
                  checkIcons_[0], checkIcons_[1], checkIcons_[2], checkIcons_[3]})
        ShowWindow(h, disconnect_ ? SW_HIDE : SW_SHOW);
    updateResources();
    refresh();
    polling_.start(nowMs());
    SetTimer(window_, kTimer, kProgressIntervalMs, nullptr);
    reactivateWindow(window_);
    // One separate read per local Connect. It cannot change the bootstrap model.
    if (!disconnect_ && !detailsReader_.request(IpcCommand::get_config, {}, window_, kDetailsMessage))
        details_ = {};
    // First status read is due in 500 ms. The command reply seeds the view.
}
void ProgressWindow::onDisconnectReply(const TrayStatus& snapshot) {
    if (!window_ || !disconnect_ || !waitingForReply_) return;
    waitingForReply_ = false;
    if (!snapshot.valid) {
        model_.onCommandFailure();
        polling_.stop();
        refresh();
        return;
    }
    onSnapshot(snapshot);
}
void ProgressWindow::onDisconnectTimeout() {
    if (!window_ || !disconnect_) return;
    waitingForReply_ = false;
    reader_.abandon();
    readPending_ = false;
    polling_.stop();
    model_.onCommandFailure();
    refresh();
}
void ProgressWindow::endSession() {
    // Workers own shared transport state. Revoke delivery before destroying
    // controls. WM_DESTROY's ordinary stop calls now have nothing to wait for.
    reader_.abandon(); detailsReader_.abandon();
    if (window_) DestroyWindow(window_);
}
bool ProgressWindow::translate(MSG &message) {
    if (errorDetail_.translate(message)) return true;
    if (!window_ || (message.hwnd != window_ && !IsChild(window_, message.hwnd)))
        return false;
    if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
        SendMessageW(window_, WM_CLOSE, 0, 0);
        return true;
    }
    return IsDialogMessageW(window_, &message) != FALSE;
}
void ProgressWindow::updateResources() {
    if (!window_ || !resources_.update(instance_, static_cast<int>(GetDpiForWindow(window_))))
        return;
    auto font = [&](HWND h, ui::Font role) {
        if (h)
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(role)), TRUE);
    };
    font(eyebrow_, ui::Font::subtitle);
    for (auto h : labels_)
        font(h, ui::Font::label);
    font(fields_[0], ui::Font::mono);
    font(fields_[1], ui::Font::value);
    for (auto h : checks_)
        font(h, ui::Font::value);
    for (auto h : checkLabels_)
        font(h, ui::Font::value);
    for (auto h : checkIcons_)
        font(h, ui::Font::value);
    for (auto h : {detailsHeading_, checksHeading_, panelHeading_})
        font(h, ui::Font::card);
    font(panelAction_, ui::Font::label);
    font(countdown_, ui::Font::value);
    font(errorDetailButton_, ui::Font::label);
    resources_.applyApplicationIcons(window_);
}
void ProgressWindow::refresh() {
    ShowWindow(errorDetailButton_, model_.httpDiagnostic() ? SW_SHOWNA : SW_HIDE);
    if (!model_.httpDiagnostic()) errorDetail_.close();
    const auto &v = model_.view();
    const bool success = v.stage == ProgressStage::succeeded, failure = v.stage == ProgressStage::failed;
    const auto &error = progressErrorText(v.statusUnavailable ? "status_unavailable" : v.errorCode);
    if (disconnect_) {
        const bool issue = failure || v.statusUnavailable || !v.errorCode.empty();
        setText(panelHeading_, success ? L"Disconnected" : issue ? L"Disconnect not confirmed" : L"Disconnecting...");
        setText(panelAction_, success ? L"You have been disconnected successfully."
            : issue ? L"The service did not confirm Disconnect. Try Disconnect again or check the service status."
                    : L"Please wait while the VPN disconnects.");
        setText(countdown_, success ? L"Closing in " + std::to_wstring(v.closingSeconds) +
            (v.closingSeconds == 1 ? L" second" : L" seconds") : L"");
        ShowWindow(panelHeading_, SW_SHOW);
        ShowWindow(panelAction_, SW_SHOW);
        ShowWindow(countdown_, success ? SW_SHOW : SW_HIDE);
        EnableMenuItem(GetSystemMenu(window_, FALSE), SC_CLOSE,
            MF_BYCOMMAND | (success || issue ? MF_ENABLED : MF_GRAYED));
        resources_.applyApplicationIcons(window_);
        layout();
        return;
    }
    setText(fields_[0], details_.available ? widen(details_.serverUrl) : L"Unavailable");
    setText(fields_[1], details_.available ? widen(details_.virtualIp) : L"Unavailable");
    const CheckpointResult checks[]{v.serverUrl, v.serverSecret, v.accessKey, v.communicationContracts};
    for (std::size_t i = 0; i < 4; ++i) {
        setText(checks_[i], outcome(checks[i]));
        setText(checkIcons_[i], checks[i] == CheckpointResult::success  ? L"\x2713"
                                : checks[i] == CheckpointResult::failed ? L"\x2717"
                                                                        : L"\x25cf");
    }
    setText(panelHeading_, success                          ? L"Connected successfully"
                           : failure || v.statusUnavailable ? widen(error.panel)
                                                            : L"");
    setText(panelAction_, success                          ? L"This window will close automatically."
                          : failure || v.statusUnavailable ? widen(error.action)
                                                           : L"");
    setText(countdown_, success ? L"Closing in " + std::to_wstring(v.closingSeconds) +
                                      (v.closingSeconds == 1 ? L" second" : L" seconds")
                                : L"");
    ShowWindow(panelHeading_, success || failure || v.statusUnavailable ? SW_SHOW : SW_HIDE);
    ShowWindow(panelAction_, success || failure || v.statusUnavailable ? SW_SHOW : SW_HIDE);
    ShowWindow(countdown_, success ? SW_SHOW : SW_HIDE);
    resources_.applyApplicationIcons(window_);
    layout();
}
void ProgressWindow::onSnapshot(const TrayStatus &snapshot) {
    if (!window_)
        return;
    const auto before = model_.view();
    model_.onSnapshot(snapshot, nowMs());
    if (disconnect_ && disconnectSnapshot) disconnectSnapshot(snapshot);
    if (model_.httpDiagnostic()) {
        diagnosticEpoch_ = model_.connectionEpoch();
        diagnosticAttemptId_ = model_.connectAttemptId().value_or("");
    }
    if (model_.view().closeRequested) {
        DestroyWindow(window_);
        return;
    }
    refresh();
}
void ProgressWindow::layout() {
    if (!eyebrow_ || layingOut_ || IsIconic(window_))
        return;
    LayoutFrame frame(window_);
    layingOut_ = true;
    RECT client{};
    GetClientRect(window_, &client);
    const int dpi = resources_.dpi(), width = client.right;
    HDC dc = GetDC(window_);
    auto textHeight = [&](HWND h, ui::Font role, int available, int minimum) {
        const int n = GetWindowTextLengthW(h);
        std::wstring value(n + 1, L'\0');
        GetWindowTextW(h, value.data(), n + 1);
        value.resize(n);
        return std::max(scaleForDpi(minimum, dpi),
                        measureText(dc, resources_.font(role), value, available, true).height);
    };
    const int margin = progressHorizontalMargin(width, dpi);
    const int contentWidth = progressContentRight(width, dpi) - margin;
    const bool success = model_.view().stage == ProgressStage::succeeded;
    const int detailWidth = model_.httpDiagnostic() ? std::max(scaleForDpi(128, dpi),
        measureText(dc, resources_.font(ui::Font::label), L"Error detail", 10000, false).width + scaleForDpi(32, dpi)) : 0;
    const int panelWidth =
        contentWidth - scaleForDpi(48 + (success && width >= scaleForDpi(760, dpi) ? 208 : 0), dpi) -
        (progressErrorDetailBeside(width, dpi, detailWidth) ? detailWidth + scaleForDpi(16, dpi) : 0);
    layout_ = computeProgressLayout(width, dpi,
                                    textHeight(panelAction_, ui::Font::label, panelWidth, 24), success, disconnect_,
                                    textHeight(panelHeading_, ui::Font::card, panelWidth, 28), detailWidth);
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
    std::vector<ControlPlacement> positions;
    auto add = [&](HWND h, Rect r) {
        r.top -= scroll_;
        r.bottom -= scroll_;
        positions.push_back({h, r});
    };
    add(eyebrow_, layout_.eyebrow);
    add(detailsHeading_, layout_.detailsHeading);
    add(checksHeading_, layout_.checksHeading);
    for (std::size_t i = 0; i < 2; ++i) {
        add(labels_[i], layout_.labels[i]);
        add(fields_[i], layout_.values[i]);
    }
    for (std::size_t i = 0; i < 4; ++i) {
        add(checks_[i], layout_.checks[i]);
        add(checkLabels_[i], layout_.checkLabels[i]);
        add(checkIcons_[i], layout_.checkIcons[i]);
    }
    add(panelHeading_, layout_.panelHeading);
    add(panelAction_, layout_.panelAction);
    add(countdown_, layout_.countdown);
    add(errorDetailButton_, layout_.errorDetail);
    positionControls(window_, positions);
    InvalidateRect(window_, nullptr, FALSE);
    layingOut_ = false;
}
void ProgressWindow::scroll(int position) {
    LayoutFrame frame(window_);
    position = std::clamp(position, 0, scrollMax_);
    if (position == scroll_)
        return;
    scroll_ = position;
    layout();
}
void ProgressWindow::reveal(HWND field) {
    for (std::size_t i = 0; i < 2; ++i)
        if (field == fields_[i]) {
            RECT r{};
            GetClientRect(window_, &r);
            scroll(revealScrollPosition(scroll_, r.bottom, layout_.height, layout_.values[i]));
        }
}
LRESULT CALLBACK ProgressWindow::fieldProcedure(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto *self = reinterpret_cast<ProgressWindow *>(data);
    if (m == WM_SETFOCUS)
        self->reveal(h);
    if (m == WM_NCDESTROY)
        RemoveWindowSubclass(h, fieldProcedure, 1);
    return DefSubclassProc(h, m, w, l);
}
LRESULT CALLBACK ProgressWindow::headingProcedure(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto *self = reinterpret_cast<ProgressWindow *>(data);
    const bool painting = m == WM_PAINT && GetUpdateRect(h, nullptr, FALSE);
    const auto result = DefSubclassProc(h, m, w, l);
    if (painting && IsWindowVisible(h) && !IsIconic(self->window_)) {
        RECT heading{}, client{}, visible{};
        GetWindowRect(h, &heading);
        GetClientRect(self->window_, &client);
        MapWindowPoints(self->window_, nullptr, reinterpret_cast<POINT *>(&client), 2);
        if (IntersectRect(&visible, &heading, &client))
            if (h == self->panelHeading_ && self->model_.view().stage == ProgressStage::succeeded)
                self->model_.onSuccessPresented(nowMs());
    }
    if (m == WM_NCDESTROY)
        RemoveWindowSubclass(h, headingProcedure, 1);
    return result;
}
LRESULT CALLBACK ProgressWindow::procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto *self = reinterpret_cast<ProgressWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<ProgressWindow *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_TIMER:
        if (wParam == kTimer) {
            const auto before = self->model_.view();
            self->model_.onTick(nowMs());
            if (self->model_.view().closeRequested) {
                DestroyWindow(window);
                return 0;
            }
            if (before.closingSeconds != self->model_.view().closingSeconds) {
                const auto seconds = self->model_.view().closingSeconds;
                setText(self->countdown_,
                        L"Closing in " + std::to_wstring(seconds) + (seconds == 1 ? L" second" : L" seconds"));
            }
            if (!self->waitingForReply_ && self->polling_.onTimer(nowMs()) && !self->readPending_ &&
                (self->model_.view().stage == ProgressStage::connecting || self->model_.httpDiagnostic())) {
                self->readPending_ = true;
                self->reader_.request(window, kSnapshotMessage);
            }
        }
        return 0;
    case kSnapshotMessage:
        if (auto status = self->reader_.take()) {
            self->readPending_ = false;
            self->onSnapshot(*status);
        }
        return 0;
    case kDetailsMessage:
        if (auto response = self->detailsReader_.take()) {
            self->details_ = consumeProgressConfig(std::move(*response));
            self->refresh();
        }
        return 0;
    case WM_SIZE:
        self->layout();
        return 0;
    case WM_GETMINMAXINFO:
        if (self->disconnect_)
            setMinimumClientTrackSize(window, *reinterpret_cast<MINMAXINFO*>(lParam),
                                      {400, ui::disconnectClient.height});
        return 0;
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
    case WM_DPICHANGED:
        self->updateResources();
        applyDpiRect(window, lParam);
        self->layout();
        return 0;
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
        self->updateResources();
        self->layout();
        break;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND h = reinterpret_cast<HWND>(lParam);
        auto color = ui::Color::text, background = ui::Color::surface;
        if (h == self->eyebrow_)
            background = ui::Color::background;
        if (h == self->eyebrow_ || h == self->fields_[0] || h == self->fields_[1])
            color = ui::Color::secondaryText;
        const auto &v = self->model_.view();
        const CheckpointResult checks[]{v.serverUrl, v.serverSecret, v.accessKey, v.communicationContracts};
        for (std::size_t i = 0; i < 4; ++i)
            if (h == self->checks_[i] || h == self->checkIcons_[i])
                color = checkColor(checks[i]);
        if (h == self->panelHeading_ || h == self->panelAction_ || h == self->countdown_) {
            const bool success = v.stage == ProgressStage::succeeded;
            color = success ? ui::Color::success : ui::Color::error;
            background = success ? ui::Color::successBackground : ui::Color::errorBackground;
            if (self->disconnect_ && v.stage == ProgressStage::connecting &&
                !v.statusUnavailable && v.errorCode.empty()) {
                color = ui::Color::warning;
                background = ui::Color::warningBackground;
            }
        }
        SetTextColor(dc, self->resources_.color(color));
        SetBkColor(dc, self->resources_.color(background));
        return reinterpret_cast<LRESULT>(self->resources_.brush(background));
    }
    case WM_PAINT:
        self->paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_COMMAND:
        if (LOWORD(wParam) == 412 && self->model_.httpDiagnostic()) {
            self->errorDetail_.open(self->instance_, window, *self->model_.httpDiagnostic());
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            SendMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (self->disconnectPending() && !self->model_.view().statusUnavailable &&
            self->model_.view().errorCode.empty()) return 0;
        self->model_.onClose();
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (!self->disconnect_ && self->diagnosticEpoch_)
            self->dismissReader_.request(IpcCommand::dismiss_connect_diagnostic,
                IpcProtocol::dismissDiagnosticPayload(self->diagnosticEpoch_, self->diagnosticAttemptId_), nullptr, 0);
        self->diagnosticEpoch_ = 0;
        self->diagnosticAttemptId_.clear();
        self->model_.onClose();
        self->errorDetail_.close();
        KillTimer(window, kTimer);
        self->polling_.stop();
        self->reader_.stop();
        self->detailsReader_.stop();
        self->readPending_ = false;
        // Children still exist during WM_DESTROY. Their fonts outlive them.
        self->window_ = nullptr;
        return 0;
    case WM_NCDESTROY:
        self->resources_.clear();
        self->eyebrow_ = nullptr;
        self->errorDetailButton_ = nullptr;
        self->detailsHeading_ = self->checksHeading_ = self->panelHeading_ = self->panelAction_ = self->countdown_ =
            nullptr;
        self->fields_ = {};
        self->labels_ = {};
        self->checks_ = {};
        self->checkLabels_ = {};
        self->checkIcons_ = {};
        self->details_ = {};
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
void ProgressWindow::paint() {
    PAINTSTRUCT ps{};
    auto targetDc = BeginPaint(window_, &ps); PaintBuffer buffer(targetDc); auto dc = buffer.dc();
    RECT client{};
    GetClientRect(window_, &client);
    FillRect(dc, &client, resources_.brush(ui::Color::background));
    auto shifted = [&](Rect r) {
        r.top -= scroll_;
        r.bottom -= scroll_;
        return r;
    };
    if (!disconnect_) paintCard(dc, shifted(layout_.card), resources_);
    const auto line = shifted(layout_.separator);
    RECT r{line.left, line.top, line.right, line.bottom};
    FillRect(dc, &r, resources_.brush(ui::Color::separator));
    const auto &v = model_.view();
    const bool waiting = disconnect_ && v.stage == ProgressStage::connecting && !v.statusUnavailable && v.errorCode.empty();
    if (disconnect_ || v.stage != ProgressStage::connecting || v.statusUnavailable)
        paintCard(dc, shifted(layout_.panel), resources_,
                  v.stage == ProgressStage::succeeded ? ui::Color::successBackground : waiting ? ui::Color::warningBackground : ui::Color::errorBackground,
                  v.stage == ProgressStage::succeeded ? ui::Color::successBorder : waiting ? ui::Color::warning : ui::Color::errorBorder);
    buffer.finish(); EndPaint(window_, &ps);
}
} // namespace phpvpn::tray
