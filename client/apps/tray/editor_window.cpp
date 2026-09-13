#include "editor_window.h"
#include "system_dialog.h"
#include "core/ipc/ipc_protocol.h"
#include "core/ipc/wizard_ipc.h"
#include "core/crypto/crypto_provider.h"
#include "platform/windows/ipc_pipe.h"
#include "resource.h"
#include "window_utils.h"
#include "ui_controls.h"
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <atomic>

namespace phpvpn::tray {
namespace {
constexpr wchar_t kClass[] = L"PhpVpnTrayEditor";
constexpr int kReset = 304;
constexpr UINT_PTR kResetTimer = 1;
std::string prettyEditorJson(const std::string& text) {
    if (!Json::parse(text)) return text;
    // Reflow only whitespace after validation. Retain exact number tokens
    // (including decimals beyond double precision), escapes and member order.
    std::string result;
    result.reserve(text.size());
    int depth = 0;
    bool quoted = false, escaped = false;
    char previous = 0;
    const auto newline = [&] {
        result.push_back('\n');
        result.append(static_cast<std::size_t>(depth) * 2, ' ');
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            result.push_back(c);
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '}' || c == ']') {
            --depth;
            if (previous != '{' && previous != '[') newline();
        }
        result.push_back(c);
        if (c == '"') quoted = true;
        else if (c == '{' || c == '[') {
            ++depth;
            const auto next = text.find_first_not_of(" \t\r\n", i + 1);
            if (next != std::string::npos && text[next] != '}' && text[next] != ']') newline();
        } else if (c == ',') newline();
        else if (c == ':') result.push_back(' ');
        previous = c;
    }
    return result;
}
}
struct EditorWindow::ResetTask {
    std::uint64_t generation = 0;
    std::atomic<bool> done{false};
    std::string document;
    std::wstring error;
    ~ResetTask() { secureWipe(document.data(), document.size()); }
};
EditorWindow::~EditorWindow() {
    if (window_) DestroyWindow(window_);
    // No worker ever owns HWND or this. Its only I/O has a five-second deadline.
    if (resetThread_.joinable()) resetThread_.join();
}
EditorLifecycleSnapshot EditorWindow::lifecycleSnapshot() const {
    return {window_ != nullptr, window_ && save_ && SendMessageW(field_, EM_GETMODIFY, 0, 0) != 0,
        window_ && saving_, window_ && unknown_, generation_, draftGeneration_, operationGeneration_, saveOperation_.deadlineMs};
}
std::optional<SaveOperation> EditorWindow::beginSave(std::uint64_t deadlineMs) {
    if (!window_ || !save_ || saving_ || unknown_ || endingSession_ ||
        (resetTask_ && resetTask_->generation == generation_) || deadlineMs <= GetTickCount64()) return {};
    if (!SetTimer(window_, kResetTimer, 50, nullptr)) return {};
    saveOperation_ = {generation_, draftGeneration_, ++operationGeneration_, deadlineMs};
    saving_ = true;
    updateEnabled();
    return saveOperation_;
}
bool EditorWindow::finishSave(const SaveResult& result) {
    if (!window_ || endingSession_ || !saving_ || result.operation != saveOperation_ ||
        result.operation.windowGeneration != generation_ || result.operation.draftGeneration != draftGeneration_) return false;
    saving_ = false;
    unknown_ = result.outcome == SaveOutcome::unknown;
    if (!resetTask_) KillTimer(window_, kResetTimer);
    updateEnabled();
    const auto generation = generation_;
    if (result.outcome == SaveOutcome::confirmed) {
        showSystemMessage(window_, L"The configuration was stored. It takes effect on the next connection attempt.",
            L"Edit Config", SystemDialogIcon::information);
        // A modal message pumps session-end and other generation changes.
        if (window_ && generation == generation_ && !endingSession_) DestroyWindow(window_);
    } else {
        const auto text = unknown_ ? L"The file may already have changed. The save result is unknown. Close this editor and open Edit Config to inspect the current configuration. The save will not be retried automatically." :
            result.outcome == SaveOutcome::notSent ? L"The save was not sent. Your text has been preserved." :
            L"The service rejected the configuration before publication. Your text has been preserved.";
        showSystemMessage(window_, text, L"Edit Config", unknown_ ? SystemDialogIcon::warning : SystemDialogIcon::error);
        if (window_ && generation == generation_ && !endingSession_) SetFocus(field_);
    }
    return true;
}
bool EditorWindow::approvedDiscard(const EditorLifecycleSnapshot& approved) {
    if (!window_ || saving_ || endingSession_ || lifecycleSnapshot() != approved) return false;
    DestroyWindow(window_);
    return true;
}
void EditorWindow::requestClose() {
    if (saving_ || endingSession_) return;
    const auto approved = lifecycleSnapshot();
    if ((approved.dirty || approved.unknown) && !confirmSystemAction(window_, approved.unknown ?
        L"The file may already have changed. Close without checking the save result?" : L"Discard unsaved changes?",
        L"Edit Config")) return;
    approvedDiscard(approved);
}
void EditorWindow::clearDraft() {
    changingContent_ = true;
    if (field_) { SetWindowTextW(field_, L""); SendMessageW(field_, EM_EMPTYUNDOBUFFER, 0, 0); }
    document_.load({});
    changingContent_ = false;
}
void EditorWindow::endSession() {
    if (endingSession_) return;
    endingSession_ = true;
    // Copy before invocation: destroying the window clears the stored callbacks.
    const auto abandon = abandonSave_;
    if (abandon) abandon();
    ++generation_; ++draftGeneration_; ++operationGeneration_;
    saving_ = unknown_ = false;
    if (window_) { KillTimer(window_, kResetTimer); DestroyWindow(window_); }
    // Reset owns only task + endpoint values, never this/HWND or an app callback.
    if (resetThread_.joinable()) resetThread_.detach();
    resetTask_.reset();
}
void EditorWindow::updateEnabled() {
    const bool resetting = resetTask_ && resetTask_->generation == generation_;
    EnableWindow(field_, !saving_ && !resetting);
    SendMessageW(field_, EM_SETREADONLY, !save_ || unknown_, 0);
    EnableWindow(GetDlgItem(window_, IDC_SAVE), !saving_ && !unknown_ && !resetting);
    EnableWindow(GetDlgItem(window_, kReset), !saving_ && !unknown_ && !resetTask_);
    EnableWindow(GetDlgItem(window_, IDC_CANCEL), !saving_);
}
void EditorWindow::setContent(const std::string& content) {
    changingContent_ = true;
    ++draftGeneration_;
    document_.load(content);
    auto presentation = document_.presentation();
    if (save_ && !document_.literal()) {
        presentation = prettyEditorJson(content);
    }
    std::string crlf;
    crlf.reserve(presentation.size());
    char previous = 0;
    for (const char character : presentation) {
        if (character == '\n' && previous != '\r') crlf.push_back('\r');
        crlf.push_back(character);
        previous = character;
    }
    SetWindowTextW(field_, widen(crlf).c_str());
    SendMessageW(field_, EM_SETMODIFY, FALSE, 0);
    SetWindowTextW(window_, (title_ + (document_.literal() ? L" - JSON string literal (raw NUL shown as \\u0000)" : L"")).c_str());
    changingContent_ = false;
}
void EditorWindow::reset() {
    if (!save_ || saving_ || unknown_ || endingSession_ || resetTask_) return;
    const auto expected = lifecycleSnapshot();
    if (!confirmSystemAction(window_,
        L"Restore the default configuration in this editor? The file will change only after you choose Save.",
        L"Edit Config")) return;
    if (endingSession_ || lifecycleSnapshot() != expected) return;
    auto task = std::make_shared<ResetTask>();
    task->generation = generation_;
    resetTask_ = task;
    updateEnabled();
    if (!SetTimer(window_, kResetTimer, 50, nullptr)) {
        resetTask_.reset();
        updateEnabled();
        showSystemMessage(window_, L"The default configuration could not be loaded. Your text has been preserved.",
                          L"Edit Config", SystemDialogIcon::error);
        return;
    }
    try {
        resetThread_ = std::thread([task, pipeName = pipeName_] {
            auto load = [&]() -> Status {
                auto crypto = createPlatformCryptoProvider();
                if (!crypto) return crypto.error();
                auto id = crypto.value()->randomUuidV4();
                if (!id) return id.error();
                WizardRequest request;
                request.command = WizardCommand::get_default_config;
                request.requestId = id.value();
                auto encoded = WizardIpc::buildRequest(request);
                if (!encoded) return encoded.error();
                auto bytes = sendIpcRequestTo(pipeName, encoded.value(), 5000);
                if (!bytes) return bytes.error();
                auto reply = WizardIpc::parseReply(request.command, request.requestId, bytes.value());
                if (!reply) return reply.error();
                if (reply->error) {
                    task->error = widen(reply->error->description + " " + reply->error->suggestedAction);
                    return Error(ErrorCode::response_invalid);
                }
                const auto* document = reply->result.find("document");
                // Closed reply validation checks the field type. Validate the
                // shared template too before replacing any user's draft.
                auto normalized = normalizeConfigText(document->stringValue());
                if (!normalized) return normalized.error();
                ConfigWarnings warnings;
                auto config = parseAndValidateConfig(normalized.value(), warnings);
                if (!config) return config.error();
                task->document = std::move(normalized.value());
                return Status::success();
            };
            try {
                const auto status = load();
                if (!status && task->error.empty()) task->error = widen(errorCodeDescription(status.error().code));
            } catch (...) {
                task->error = L"The default configuration could not be loaded.";
            }
            task->done.store(true, std::memory_order_release);
        });
    } catch (...) {
        KillTimer(window_, kResetTimer);
        resetTask_.reset();
        updateEnabled();
        showSystemMessage(window_, L"The default configuration could not be loaded. Your text has been preserved.",
                          L"Edit Config", SystemDialogIcon::error);
    }
}
void EditorWindow::pollReset() {
    if (!resetTask_ || !resetTask_->done.load(std::memory_order_acquire)) return;
    if (resetThread_.joinable()) resetThread_.join();
    auto task = std::move(resetTask_);
    if (!saving_) KillTimer(window_, kResetTimer);
    updateEnabled();
    if (task->generation != generation_) return;
    if (!task->error.empty()) {
        const auto message = task->error + L"\n\nYour text has been preserved.";
        showSystemMessage(window_, message.c_str(), L"Edit Config", SystemDialogIcon::error);
        return;
    }
    setContent(task->document);
    SendMessageW(field_, EM_SETMODIFY, TRUE, 0);
    SetFocus(field_);
}
void EditorWindow::pollLifecycle() {
    if (endingSession_) return;
    if (saving_) {
        const auto poll = pollSave_;
        if (poll) poll();
    }
    if (window_) pollReset();
}
void EditorWindow::open(HINSTANCE instance, const std::wstring& title, const std::string& content, Save save,
                        const std::string& configPath, std::function<void()> pollSave, std::function<void()> abandonSave) {
    if (endingSession_) return;
    if (window_) DestroyWindow(window_);
    ++generation_;
    saving_ = false;
    unknown_ = false;
    saveOperation_ = {};
    save_ = std::move(save);
    pollSave_ = std::move(pollSave); abandonSave_ = std::move(abandonSave);
    title_ = title;
    instance_ = instance;
    oldClient_ = {};
    oldLayout_ = {};
    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.style = CS_HREDRAW | CS_VREDRAW;
    type.lpfnWndProc = procedure;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    type.lpszClassName = kClass;
    RegisterClassExW(&type);
    const auto work = activeWorkArea();
    window_ = CreateWindowExW(WS_EX_COMPOSITED | WS_EX_APPWINDOW, kClass, title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, work.left, work.top, 1, 1,
        nullptr, nullptr, instance, this);
    if (!window_) return;
    // client.md Edit Config: 1080 x 560 is the CLIENT size at 96 DPI.
    sizeAndCenter(window_, defaultEditorClientSize(96));
    if (!save_) {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES};
        InitCommonControlsEx(&controls);
        tabs_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            WS_CLIPSIBLINGS | TCS_FOCUSONBUTTONDOWN | TCS_OWNERDRAWFIXED, 0, 0, 1, 1, window_,
            reinterpret_cast<HMENU>(IDC_INFO_TABS), instance, nullptr);
        if (!tabs_) { DestroyWindow(window_); return; }
        for (const auto label : {L"Who is online", L"Raw server information"}) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(label);
            TabCtrl_InsertItem(tabs_, TabCtrl_GetItemCount(tabs_), &item);
        }
    }
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
                   ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | ES_AUTOHSCROLL;
    if (!save_) style |= ES_READONLY;
    field_ = CreateWindowExW(0, L"EDIT", nullptr, style,
        0, 0, 1, 1, window_, reinterpret_cast<HMENU>(IDC_TEXT_AREA), instance, nullptr);
    if (!field_) { DestroyWindow(window_); return; }
    SendMessageW(field_, EM_SETLIMITTEXT, kIpcMaxMessageBytes, 0);
    setContent(content);
    if (tabs_) selectInfoTab(0);
    heading_ = CreateWindowExW(0, L"STATIC", save_ ? L"Edit configuration" : L"Server information",
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 1, 1, window_, nullptr, instance, nullptr);
    if (save_) {
        const auto displayPath = pathWithExistingCasing(widen(configPath));
        path_ = CreateWindowExW(0, L"EDIT", displayPath.c_str(), WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL,
            0, 0, 1, 1, window_, nullptr, instance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Reset", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReset)), instance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 1, 1, window_, reinterpret_cast<HMENU>(IDC_CANCEL), instance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 1, 1, window_, reinterpret_cast<HMENU>(IDC_SAVE), instance, nullptr);
    } else {
        CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
            0, 0, 1, 1, window_, reinterpret_cast<HMENU>(IDOK), instance, nullptr);
    }
    if ((save_ && (!attachModernButton(GetDlgItem(window_, kReset), resources_, ButtonRole::reset) ||
        !attachModernButton(GetDlgItem(window_, IDC_CANCEL), resources_) ||
        !attachModernButton(GetDlgItem(window_, IDC_SAVE), resources_, ButtonRole::primary))) ||
        (!save_ && !attachModernButton(GetDlgItem(window_, IDOK), resources_, ButtonRole::primary))) {
        DestroyWindow(window_); return;
    }
    updateFonts();
    layout();
    if (resetTask_) {
        // A previous generation can still be finishing. Poll its bounded task,
        // but never allow it to replace this newly opened document.
        SetTimer(window_, kResetTimer, 50, nullptr);
        pollReset();
    }
    updateEnabled();
    ShowWindow(window_, SW_SHOW);
    // The page is a sibling of EDIT. Keep its background behind the raw field.
    // Circular dialog order remains tabs -> visible field -> OK -> tabs.
    if (tabs_) SetWindowPos(tabs_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(window_);
    SetFocus(tabs_ ? tabs_ : field_);
}
void EditorWindow::selectInfoTab(int index) {
    if (!tabs_ || (index != 0 && index != 1)) return;
    infoTab_ = index;
    TabCtrl_SetCurSel(tabs_, index);
    if (index == 0 && GetFocus() == field_) SetFocus(tabs_);
    ShowWindow(field_, index == 1 ? SW_SHOW : SW_HIDE);
    InvalidateRect(tabs_, nullptr, FALSE);
}
void EditorWindow::updateFonts() {
    const int dpi = static_cast<int>(GetDpiForWindow(window_));
    if (!resources_.update(instance_, dpi)) return;
    HFONT text = resources_.font(ui::Font::mono);
    HFONT button = resources_.font(ui::Font::label);
    SendMessageW(field_, WM_SETFONT, reinterpret_cast<WPARAM>(text), TRUE);
    if (tabs_) SendMessageW(tabs_, WM_SETFONT, reinterpret_cast<WPARAM>(button), TRUE);
    for (const int id : {IDC_SAVE, kReset, IDC_CANCEL, IDOK}) {
        if (const HWND child = GetDlgItem(window_, id)) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(button), TRUE);
    }
    if (heading_) SendMessageW(heading_, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::heading)), TRUE);
    if (path_) SendMessageW(path_, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::subtitle)), TRUE);
    SendMessageW(window_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(resources_.icon(2)));
}
void EditorWindow::layout() {
    if (!field_) return;
    LayoutFrame frame(window_);
    RECT client{};
    GetClientRect(window_, &client);
    const Size size{client.right, client.bottom};
    const auto next = computeEditorLayout(size, static_cast<int>(GetDpiForWindow(window_)), static_cast<bool>(save_));
    std::vector<ControlPlacement> controls;
    const auto move = [&](HWND child, Rect rect) {
        if (!child) return;
        controls.push_back({child,rect});
    };
    // The parent paints the continuous surface and frame. Keep the native EDIT,
    // including both non-client scrollbars, inside the rounded corners.
    const int inset = scaleForDpi(6, static_cast<int>(GetDpiForWindow(window_)));
    auto area = next.textArea;
    if (tabs_) {
        move(tabs_, next.tabs);
        RECT page{next.tabs.left, next.tabs.top, next.tabs.right, next.tabs.bottom};
        TabCtrl_AdjustRect(tabs_, FALSE, &page);
        area = {page.left, page.top, page.right, page.bottom};
    }
    const auto fieldRect = Rect{area.left + inset, area.top + inset,
        std::max(area.left + inset, area.right - inset),
        std::max(area.top + inset, area.bottom - inset)};
    move(field_, fieldRect);
    move(GetDlgItem(window_, IDC_SAVE), next.saveButton);
    move(GetDlgItem(window_, kReset), next.resetButton);
    move(GetDlgItem(window_, IDC_CANCEL), next.cancelButton);
    move(GetDlgItem(window_, IDOK), next.saveButton);
    move(heading_,next.heading); move(path_,next.path);
    positionControls(window_,controls);
    oldClient_ = size;
    oldLayout_ = next;
}
LRESULT CALLBACK EditorWindow::procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<EditorWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<EditorWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
        case WM_DRAWITEM:
            if (lParam && drawModernTab(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam), self->tabs_, self->resources_))
                return TRUE;
            break;
        case WM_NOTIFY: {
            const auto notification = reinterpret_cast<NMHDR*>(lParam);
            if (notification && notification->hwndFrom == self->tabs_ &&
                notification->idFrom == IDC_INFO_TABS && notification->code == TCN_SELCHANGE) {
                self->selectInfoTab(TabCtrl_GetCurSel(self->tabs_));
                return 0;
            }
            break;
        }
        case WM_COMMAND:
            if (reinterpret_cast<HWND>(lParam) == self->field_ &&
                (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS)) {
                self->editorFocused_ = HIWORD(wParam) == EN_SETFOCUS;
                const auto& area = self->oldLayout_.textArea;
                RECT frame{area.left, area.top, area.right, area.bottom};
                InvalidateRect(window, &frame, FALSE);
                return 0;
            }
            if (reinterpret_cast<HWND>(lParam) == self->field_ && HIWORD(wParam) == EN_CHANGE && !self->changingContent_) {
                ++self->draftGeneration_;
                return 0;
            }
            if (LOWORD(wParam) == kReset) { self->reset(); return 0; }
            if (LOWORD(wParam) == IDC_CANCEL || LOWORD(wParam) == IDCANCEL) {
                self->requestClose();
                return 0;
            }
            if (LOWORD(wParam) == IDOK && !self->save_) {
                DestroyWindow(window);
                return 0;
            }
            if ((LOWORD(wParam) == IDC_SAVE || LOWORD(wParam) == IDOK) && self->save_) {
                if (self->saving_ || self->unknown_ || self->endingSession_ || (self->resetTask_ && self->resetTask_->generation == self->generation_)) return 0;
                const int length = GetWindowTextLengthW(self->field_);
                std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
                GetWindowTextW(self->field_, text.data(), length + 1);
                text.resize(static_cast<std::size_t>(length));
                const auto utf8 = narrow(text);
                if (!text.empty() && utf8.empty()) {
                    showSystemMessage(window, L"The edited text contains an invalid Unicode sequence. Correct it before saving.", L"Edit Config", SystemDialogIcon::error);
                    return 0;
                }
                auto document = self->document_.edited(utf8, SendMessageW(self->field_, EM_GETMODIFY, 0, 0) != 0);
                if (!document) {
                    showSystemMessage(window, L"Enter a complete JSON string literal, including its surrounding quotes. The file has not been changed.", L"Edit Config", SystemDialogIcon::error);
                    return 0;
                }
                const auto save = self->save_;
                const auto generation = self->generation_;
                if (save(*document) && self->window_ && generation == self->generation_ && !self->endingSession_) {
                    showSystemMessage(window, L"The configuration was stored. It takes effect on the next connection attempt.",
                                      L"Edit Config", SystemDialogIcon::information);
                    DestroyWindow(window);
                }
                return 0;
            }
            break;
        case WM_TIMER:
            if (wParam == kResetTimer) { self->pollLifecycle(); return 0; }
            break;
        case WM_ACTIVATE:
            self->editorActive_ = LOWORD(wParam) != WA_INACTIVE;
            InvalidateRect(window, nullptr, FALSE);
            break;
        case WM_LBUTTONDOWN:
            if (self->field_ && IsWindowVisible(self->field_) && IsWindowEnabled(self->field_)) {
                const auto& area = self->oldLayout_.textArea;
                RECT frame{area.left, area.top, area.right, area.bottom};
                POINT point{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
                if (PtInRect(&frame, point)) { SetFocus(self->field_); return 0; }
            }
            break;
        case WM_SIZE: self->layout(); return 0;
        case WM_SETTINGCHANGE: case WM_SYSCOLORCHANGE: case WM_THEMECHANGED:
            self->updateFonts(); self->layout();
            if (self->tabs_) InvalidateRect(self->tabs_, nullptr, FALSE);
            break;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; auto targetDc = BeginPaint(window,&ps); PaintBuffer buffer(targetDc); auto dc = buffer.dc(); RECT r{}; GetClientRect(window,&r);
            FillRect(dc,&r,self->resources_.brush(ui::Color::background));
            const bool focused = self->editorFocused_ && self->editorActive_;
            const auto surface = self->save_ ? ui::Color::surface : ui::Color::disabledBackground;
            const auto border = focused ? (self->save_ ? ui::Color::focus : ui::Color::buttonNeutralFocus)
                                        : (self->save_ ? ui::Color::border : ui::Color::disabledBackground);
            if (self->save_) paintRoundedShape(dc, self->oldLayout_.textArea, scaleForDpi(12, self->resources_.dpi()),
                self->resources_, surface, border, false,
                std::max(1, scaleForDpi(focused ? 2 : 1, self->resources_.dpi())));
            const int top=self->oldLayout_.footer.top;
            RECT line{0,top,r.right,top+std::max(1,scaleForDpi(1,self->resources_.dpi()))};
            FillRect(dc,&line,self->resources_.brush(ui::Color::separator));
            buffer.finish(); EndPaint(window,&ps); return 0;
        }
        case WM_CTLCOLOREDIT: case WM_CTLCOLORSTATIC: {
            const bool header=reinterpret_cast<HWND>(lParam)==self->heading_ || reinterpret_cast<HWND>(lParam)==self->path_;
            const auto background=header?ui::Color::background:
                (self->save_ ? ui::Color::surface : ui::Color::disabledBackground);
            HDC dc=reinterpret_cast<HDC>(wParam);
            SetTextColor(dc,self->resources_.color(reinterpret_cast<HWND>(lParam)==self->path_?ui::Color::secondaryText:ui::Color::text));
            SetBkColor(dc,self->resources_.color(background));
            return reinterpret_cast<LRESULT>(self->resources_.brush(background));
        }
        case WM_DPICHANGED:
            applyDpiRect(window, lParam);
            self->updateFonts();
            self->layout();
            return 0;
        case WM_GETMINMAXINFO: {
            setMinimumClientTrackSize(window, *reinterpret_cast<MINMAXINFO*>(lParam), {320, 360});
            return 0;
        }
        case WM_QUERYENDSESSION: return TRUE;
        case WM_ENDSESSION: if (wParam) self->endSession(); return 0;
        case WM_CLOSE: self->requestClose(); return 0;
        case WM_DESTROY:
            KillTimer(window, kResetTimer);
            self->clearDraft();
            ++self->generation_;
            ++self->draftGeneration_; ++self->operationGeneration_;
            self->saving_ = self->unknown_ = false;
            self->save_ = {}; self->pollSave_ = {}; self->abandonSave_ = {};
            self->window_ = nullptr;
            self->field_ = nullptr;
            self->tabs_ = nullptr;
            self->infoTab_ = 0;
            self->editorFocused_ = self->editorActive_ = false;
            return 0;
        case WM_NCDESTROY:
            self->heading_=self->path_=nullptr;
            self->resources_.clear();
            SetWindowLongPtrW(window,GWLP_USERDATA,0);
            break;
        default: break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}  // namespace phpvpn::tray
