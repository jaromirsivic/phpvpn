// Local presentation only. client.md fixes the tray timer at five seconds.
// Progress owns a separate 500 ms timer and a separate read-only IPC worker.
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <optional>
#include <string>

#include "core/protocol/json.h"
#include "editor_window.h"
#include "poll_schedule.h"
#include "progress_window.h"
#include "resource.h"
#include "statistics_window.h"
#include "status_reader.h"
#include "window_utils.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace phpvpn::tray {
namespace {
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kStatusMessage = WM_APP + 2;
constexpr UINT kCommandMessage = WM_APP + 3;
constexpr UINT_PTR kStatusTimer = 1;
constexpr wchar_t kClass[] = L"PhpVpnTrayWindow";
constexpr wchar_t kMutexName[] = L"Local\\phpvpn-tray-session";

class TrayApplication {
public:
    ~TrayApplication() {
        commands_.stop();
        statusReader_.stop();
        if (window_) {
            KillTimer(window_, kStatusTimer);
            Shell_NotifyIconW(NIM_DELETE, &icon_);
            DestroyWindow(window_);
        }
    }
    bool initialise(HINSTANCE instance);
    int run();
private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void showMenu();
    void updateIcon();
    void onSnapshot(const TrayStatus& snapshot);
    void startCommand(IpcCommand command, const std::string& payload = {});
    void finishCommand();
    void showError(const wchar_t* message) { MessageBoxW(window_, message, L"PHPVPN", MB_OK | MB_ICONERROR); }
    bool connectedOrConnecting() const {
        return snapshot_.state == "connected" || snapshot_.state == "connecting" || snapshot_.state == "recovering";
    }
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    NOTIFYICONDATAW icon_{};
    HICON disconnected_ = nullptr;
    HICON connecting_ = nullptr;
    HICON connected_ = nullptr;
    TrayStatus snapshot_;
    bool statusUnavailable_ = true;
    PollSchedule polling_{kStatusIntervalMs};
    StatusReader statusReader_;
    CommandReader commands_;
    std::optional<IpcCommand> pendingCommand_;
    std::uint64_t saveGeneration_ = 0;
    EditorWindow editor_;
    ProgressWindow progress_;
    StatisticsWindow statistics_;
};

bool TrayApplication::initialise(HINSTANCE instance) {
    instance_ = instance;
    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.lpfnWndProc = procedure;
    type.hInstance = instance;
    type.lpszClassName = kClass;
    if (!RegisterClassExW(&type)) return false;
    window_ = CreateWindowExW(0, kClass, L"PHPVPN", 0, 0, 0, 0, 0, HWND_MESSAGE,
                              nullptr, instance, this);
    if (!window_) return false;
    disconnected_ = LoadIconW(instance, MAKEINTRESOURCEW(IDI_PHPVPN_DISCONNECTED));
    connecting_ = LoadIconW(instance, MAKEINTRESOURCEW(IDI_PHPVPN_CONNECTING));
    connected_ = LoadIconW(instance, MAKEINTRESOURCEW(IDI_PHPVPN_CONNECTED));
    icon_.cbSize = sizeof(icon_);
    icon_.hWnd = window_;
    icon_.uID = 1;
    icon_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    icon_.uCallbackMessage = kTrayMessage;
    icon_.hIcon = disconnected_;
    wcscpy_s(icon_.szTip, L"PHPVPN");
    Shell_NotifyIconW(NIM_ADD, &icon_);
    polling_.start(nowMs());
    SetTimer(window_, kStatusTimer, kStatusIntervalMs, nullptr);
    statusReader_.request(window_, kStatusMessage);
    return true;
}
int TrayApplication::run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (editor_.translate(message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
LRESULT CALLBACK TrayApplication::procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<TrayApplication*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<TrayApplication*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
        case kTrayMessage:
            if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_LBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU)
                self->showMenu();
            return 0;
        case WM_TIMER:
            if (wParam == kStatusTimer && self->polling_.onTimer(nowMs())) self->statusReader_.request(window, kStatusMessage);
            return 0;
        case kStatusMessage:
            if (auto snapshot = self->statusReader_.take()) self->onSnapshot(*snapshot);
            return 0;
        case kCommandMessage: self->finishCommand(); return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_EDIT_CONFIG: self->startCommand(IpcCommand::get_config); return 0;
                case IDM_SHOW_INFO: self->startCommand(IpcCommand::get_info); return 0;
                case IDM_TOGGLE:
                    self->startCommand(self->connectedOrConnecting() ? IpcCommand::disconnect : IpcCommand::connect);
                    return 0;
                case IDM_STATISTICS:
                    self->statistics_.open(self->instance_, self->snapshot_);
                    // One extra read. Neither window's timer is changed.
                    self->statusReader_.request(window, kStatusMessage);
                    return 0;
                case IDM_EXIT: PostQuitMessage(0); return 0;
                default: break;
            }
            break;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
void TrayApplication::showMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const UINT flags = MF_STRING | (pendingCommand_ ? MF_GRAYED : MF_ENABLED);
    AppendMenuW(menu, flags, IDM_EDIT_CONFIG, L"Edit Config");
    AppendMenuW(menu, flags, IDM_SHOW_INFO, L"Show Info");
    AppendMenuW(menu, flags, IDM_TOGGLE, connectedOrConnecting() ? L"Disconnect" : L"Connect");
    AppendMenuW(menu, MF_STRING, IDM_STATISTICS, L"Statistics");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");
    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(window_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, window_, nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}
void TrayApplication::onSnapshot(const TrayStatus& snapshot) {
    statusUnavailable_ = !snapshot.valid;
    if (applyStatusSnapshot(snapshot_, snapshot)) statistics_.onSnapshot(snapshot_);
    // Invalid reads retain the last authoritative snapshot, but indicate its
    // availability separately. They never invent a new zero-valued snapshot.
    updateIcon();
}
void TrayApplication::updateIcon() {
    HICON icon = disconnected_;
    const wchar_t* tip = L"PHPVPN: disconnected";
    if (statusUnavailable_) tip = L"PHPVPN: service status unavailable";
    else if (snapshot_.state == "connected") { icon = connected_; tip = L"PHPVPN: connected"; }
    else if (snapshot_.state == "connecting") { icon = connecting_; tip = L"PHPVPN: connecting"; }
    else if (snapshot_.state == "recovering") { icon = connecting_; tip = L"PHPVPN: recovering"; }
    else if (snapshot_.state == "error") tip = L"PHPVPN: error";
    icon_.hIcon = icon;
    wcscpy_s(icon_.szTip, tip);
    Shell_NotifyIconW(NIM_MODIFY, &icon_);
}
void TrayApplication::startCommand(IpcCommand command, const std::string& payload) {
    if (pendingCommand_) return;
    if (commands_.request(command, payload, window_, kCommandMessage)) {
        pendingCommand_ = command;
        if (command == IpcCommand::set_config) {
            saveGeneration_ = editor_.generation();
            editor_.setSaving(saveGeneration_, true);
        }
    }
    else showError(L"The command could not be started.");
}
void TrayApplication::finishCommand() {
    const auto response = commands_.take();
    if (!response || !pendingCommand_) return;
    const auto command = *pendingCommand_;
    pendingCommand_.reset();
    if (command == IpcCommand::set_config) editor_.setSaving(saveGeneration_, false);
    // These extra reads do not restart or replace the periodic tray timer.
    if (command == IpcCommand::connect || command == IpcCommand::disconnect) {
        // Discard any read initiated before the command. It may otherwise
        // arrive after the command's newer snapshot and roll the icon back.
        statusReader_.stop();
        statusReader_.request(window_, kStatusMessage);
    }
    if (response->empty()) { showError(L"The PHPVPN service did not complete the command."); return; }
    auto parsed = Json::parse(*response);
    if (!parsed.ok() || !parsed.value().isObject()) { showError(L"The service returned an invalid response."); return; }
    if (const auto* error = parsed.value().find("error"); error && error->isObject()) {
        // Never display raw transport/system/configuration text as an error.
        const auto* description = error->find("description");
        const auto text = description && description->isString() ? widen(description->stringValue()) : L"The command failed.";
        showError(text.c_str());
        return;
    }
    switch (command) {
        case IpcCommand::get_config:
            editor_.open(instance_, L"PHPVPN configuration", *response, [this](const std::string& content) {
                startCommand(IpcCommand::set_config, content);
                return false;  // Kept open until the asynchronous service reply.
            });
            break;
        case IpcCommand::get_info:
            editor_.open(instance_, L"PHPVPN server information", Json::serializePretty(parsed.value()));
            break;
        case IpcCommand::set_config: editor_.completeSave(saveGeneration_); break;
        case IpcCommand::connect:
            progress_.open(instance_);
            onSnapshot(parseStatusResponse(*response));
            progress_.onSnapshot(parseStatusResponse(*response));
            break;
        case IpcCommand::disconnect:
            onSnapshot(parseStatusResponse(*response));
            progress_.onSnapshot(parseStatusResponse(*response));
            break;
        default: break;
    }
}
}  // namespace
}  // namespace phpvpn::tray

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    using namespace phpvpn::tray;
    HANDLE single = CreateMutexW(nullptr, TRUE, kMutexName);
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(single); return 0; }
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);
    int result = 1;
    {
        TrayApplication application;
        if (application.initialise(instance)) result = application.run();
        else MessageBoxW(nullptr, L"The PHPVPN tray application could not start.", L"PHPVPN", MB_OK | MB_ICONERROR);
    }
    if (single) { ReleaseMutex(single); CloseHandle(single); }
    return result;
}
