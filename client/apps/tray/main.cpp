// PHPVPN tray application.
//
// The tray is a plain local IPC client. It never loads the tunnel library, never
// opens the adapter and never reads or writes the configuration file directly.
// It polls `get-status` exactly once every five seconds, and additionally right
// after a `connect` or a `disconnect`, with no adaptive change of that interval.
//
// One tray may run in every interactive session. All of them show the same
// global service state, because the snapshot they render is the state of the
// single service and not of their own session.

#include <windows.h>

#include <commctrl.h>
#include <shellapi.h>

#include <memory>
#include <string>
#include <vector>

#include "core/crypto/crypto_provider.h"
#include "core/ipc/ipc_protocol.h"
#include "core/protocol/json.h"
#include "platform/windows/ipc_pipe.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace {

constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT_PTR kStatusTimerId = 1;

// client.md fixes this interval; it is never shortened, extended or adapted.
constexpr UINT kStatusIntervalMs = 5000;

constexpr const wchar_t* kWindowClass = L"PhpVpnTrayWindow";
constexpr const wchar_t* kEditorClass = L"PhpVpnTrayEditor";
constexpr const wchar_t* kMutexName = L"Local\\phpvpn-tray-session";

std::wstring widen(std::string_view text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int required =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        return std::wstring();
    }
    std::wstring out(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(),
                        required);
    return out;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    const int required = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                             nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return std::string();
    }
    std::string out(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(),
                        required, nullptr, nullptr);
    return out;
}

// Converts the CRLF the edit control produces into the LF the wire uses.
std::string toUnixLineEndings(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char character : text) {
        if (character != '\r') {
            out.push_back(character);
        }
    }
    return out;
}

// The edit control expects CRLF, while every document on the wire uses LF only.
std::wstring widenWithCrLf(const std::string& text) {
    std::string expanded;
    expanded.reserve(text.size() + text.size() / 16);
    for (char character : text) {
        if (character == '\n') {
            expanded.push_back('\r');
        }
        expanded.push_back(character);
    }
    return widen(expanded);
}

class TrayApplication {
public:
    bool initialise(HINSTANCE instance);
    int run();

private:
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam,
                                            LPARAM lParam);
    static LRESULT CALLBACK editorProcedure(HWND window, UINT message, WPARAM wParam,
                                            LPARAM lParam);

    void onTrayMessage(LPARAM lParam);
    void showMenu();
    void refreshStatus();
    void applyStatus(const std::string& response);
    void updateIcon();

    void onEditConfig();
    void onShowInfo();
    void onToggleConnection();

    // Sends one request and reports a failure through a modal message box.
    bool call(phpvpn::IpcCommand command, const std::string& payload, std::string& response);

    void showError(const std::wstring& title, const std::wstring& message);
    void openTextWindow(const std::wstring& title, const std::string& content, bool editable);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND editor_ = nullptr;
    HWND editorField_ = nullptr;
    bool editorIsConfig_ = false;

    NOTIFYICONDATAW icon_{};
    HICON iconDisconnected_ = nullptr;
    HICON iconConnecting_ = nullptr;
    HICON iconConnected_ = nullptr;

    std::string state_ = "disconnected";
    std::string phase_ = "idle";
    std::unique_ptr<phpvpn::CryptoProvider> crypto_;
};

TrayApplication* g_application = nullptr;

bool TrayApplication::initialise(HINSTANCE instance) {
    instance_ = instance;

    auto crypto = phpvpn::createPlatformCryptoProvider();
    if (!crypto.ok()) {
        return false;
    }
    crypto_ = std::move(crypto).value();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &TrayApplication::windowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    RegisterClassExW(&windowClass);

    WNDCLASSEXW editorClass{};
    editorClass.cbSize = sizeof(editorClass);
    editorClass.lpfnWndProc = &TrayApplication::editorProcedure;
    editorClass.hInstance = instance;
    editorClass.lpszClassName = kEditorClass;
    editorClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    editorClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&editorClass);

    window_ = CreateWindowExW(0, kWindowClass, L"PHPVPN", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                              instance, nullptr);
    if (window_ == nullptr) {
        return false;
    }

    iconDisconnected_ = LoadIconW(instance, MAKEINTRESOURCEW(IDI_PHPVPN_DISCONNECTED));
    iconConnecting_ = LoadIconW(instance, MAKEINTRESOURCEW(IDI_PHPVPN_CONNECTING));
    iconConnected_ = LoadIconW(instance, MAKEINTRESOURCEW(IDI_PHPVPN_CONNECTED));

    icon_.cbSize = sizeof(icon_);
    icon_.hWnd = window_;
    icon_.uID = 1;
    icon_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    icon_.uCallbackMessage = kTrayCallbackMessage;
    icon_.hIcon = iconDisconnected_;
    wcscpy_s(icon_.szTip, L"PHPVPN");
    Shell_NotifyIconW(NIM_ADD, &icon_);

    // The very first action of a new session is a plain status read, so a
    // running connection is displayed without starting a second bootstrap.
    refreshStatus();
    SetTimer(window_, kStatusTimerId, kStatusIntervalMs, nullptr);
    return true;
}

int TrayApplication::run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (editor_ != nullptr && IsDialogMessageW(editor_, &message)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    Shell_NotifyIconW(NIM_DELETE, &icon_);
    return 0;
}

LRESULT CALLBACK TrayApplication::windowProcedure(HWND window, UINT message, WPARAM wParam,
                                                  LPARAM lParam) {
    TrayApplication* application = g_application;
    if (application == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    switch (message) {
        case kTrayCallbackMessage:
            application->onTrayMessage(lParam);
            return 0;

        case WM_TIMER:
            if (wParam == kStatusTimerId) {
                application->refreshStatus();
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_EDIT_CONFIG:
                    application->onEditConfig();
                    return 0;
                case IDM_SHOW_INFO:
                    application->onShowInfo();
                    return 0;
                case IDM_TOGGLE:
                    application->onToggleConnection();
                    return 0;
                case IDM_EXIT:
                    // Closing the tray never stops the service and never
                    // disconnects the VPN.
                    PostQuitMessage(0);
                    return 0;
                default:
                    break;
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

void TrayApplication::onTrayMessage(LPARAM lParam) {
    const UINT event = LOWORD(lParam);
    if (event == WM_RBUTTONUP || event == WM_LBUTTONUP || event == WM_CONTEXTMENU) {
        showMenu();
    }
}

void TrayApplication::showMenu() {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    const bool connectedOrConnecting =
        state_ == "connected" || state_ == "connecting" || state_ == "recovering";

    AppendMenuW(menu, MF_STRING, IDM_EDIT_CONFIG, L"Edit Config");
    AppendMenuW(menu, MF_STRING, IDM_SHOW_INFO, L"Show Info");
    AppendMenuW(menu, MF_STRING, IDM_TOGGLE, connectedOrConnecting ? L"Disconnect" : L"Connect");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(window_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, window_, nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

bool TrayApplication::call(phpvpn::IpcCommand command, const std::string& payload,
                           std::string& response) {
    auto requestId = crypto_->randomUuidV4();
    if (!requestId.ok()) {
        showError(L"PHPVPN", L"A request identifier could not be generated.");
        return false;
    }

    const std::string request =
        phpvpn::IpcProtocol::buildRequest(command, requestId.value(), payload);

    auto result = phpvpn::sendIpcRequest(request);
    if (!result.ok()) {
        showError(L"PHPVPN", L"The PHPVPN service is not reachable.");
        return false;
    }
    response = std::move(result).value();
    return true;
}

void TrayApplication::refreshStatus() {
    std::string response;
    auto requestId = crypto_->randomUuidV4();
    if (!requestId.ok()) {
        return;
    }
    const std::string request =
        phpvpn::IpcProtocol::buildRequest(phpvpn::IpcCommand::get_status, requestId.value());

    auto result = phpvpn::sendIpcRequest(request, 2000);
    if (!result.ok()) {
        // A missing service is shown as disconnected without a modal error, so
        // the periodic poll cannot flood the desktop with message boxes.
        state_ = "disconnected";
        phase_ = "idle";
        updateIcon();
        return;
    }
    applyStatus(result.value());
}

void TrayApplication::applyStatus(const std::string& response) {
    auto document = phpvpn::Json::parse(response);
    if (!document.ok() || !document.value().isObject()) {
        return;
    }
    const phpvpn::JsonValue* status = document.value().find("status");
    if (status == nullptr || !status->isObject()) {
        return;
    }

    const phpvpn::JsonValue* state = status->find("state");
    const phpvpn::JsonValue* phase = status->find("phase");
    if (state != nullptr && state->isString()) {
        state_ = state->stringValue();
    }
    if (phase != nullptr && phase->isString()) {
        phase_ = phase->stringValue();
    }
    updateIcon();
}

void TrayApplication::updateIcon() {
    HICON icon = iconDisconnected_;
    std::wstring tip = L"PHPVPN: disconnected";

    if (state_ == "connected") {
        icon = iconConnected_;
        tip = L"PHPVPN: connected";
    } else if (state_ == "connecting" || state_ == "recovering") {
        icon = iconConnecting_;
        tip = state_ == "connecting" ? L"PHPVPN: connecting" : L"PHPVPN: recovering";
    } else if (state_ == "error") {
        tip = L"PHPVPN: error";
    }

    icon_.hIcon = icon;
    wcscpy_s(icon_.szTip, tip.c_str());
    Shell_NotifyIconW(NIM_MODIFY, &icon_);
}

void TrayApplication::onEditConfig() {
    std::string response;
    if (!call(phpvpn::IpcCommand::get_config, std::string(), response)) {
        return;
    }

    // `get-config` answers without an envelope, so an envelope means an error.
    auto document = phpvpn::Json::parse(response);
    if (document.ok() && document.value().isObject() &&
        document.value().find("error") != nullptr) {
        showError(L"Edit Config", L"The service has no valid configuration to return.");
        return;
    }

    editorIsConfig_ = true;
    openTextWindow(L"PHPVPN configuration", response, /*editable=*/true);
}

void TrayApplication::onShowInfo() {
    std::string response;
    if (!call(phpvpn::IpcCommand::get_info, std::string(), response)) {
        return;
    }

    auto document = phpvpn::Json::parse(response);
    if (document.ok() && document.value().isObject()) {
        const phpvpn::JsonValue* error = document.value().find("error");
        if (error != nullptr && error->isObject()) {
            const phpvpn::JsonValue* description = error->find("description");
            showError(L"Show Info", description != nullptr && description->isString()
                                        ? widen(description->stringValue())
                                        : L"The server information could not be retrieved.");
            return;
        }
        response = phpvpn::Json::serializePretty(document.value());
    }

    editorIsConfig_ = false;
    openTextWindow(L"PHPVPN server information", response, /*editable=*/false);
}

void TrayApplication::onToggleConnection() {
    const bool connectedOrConnecting =
        state_ == "connected" || state_ == "connecting" || state_ == "recovering";

    std::string response;
    if (!call(connectedOrConnecting ? phpvpn::IpcCommand::disconnect : phpvpn::IpcCommand::connect,
              std::string(), response)) {
        return;
    }

    auto document = phpvpn::Json::parse(response);
    if (document.ok() && document.value().isObject()) {
        const phpvpn::JsonValue* error = document.value().find("error");
        if (error != nullptr && error->isObject()) {
            const phpvpn::JsonValue* description = error->find("description");
            showError(L"PHPVPN", description != nullptr && description->isString()
                                     ? widen(description->stringValue())
                                     : L"The command failed.");
            state_ = "disconnected";
            updateIcon();
            return;
        }
        applyStatus(response);
    }

    // An immediate extra read right after the command, as the contract requires.
    refreshStatus();
}

void TrayApplication::showError(const std::wstring& title, const std::wstring& message) {
    MessageBoxW(window_, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
}

void TrayApplication::openTextWindow(const std::wstring& title, const std::string& content,
                                     bool editable) {
    if (editor_ != nullptr) {
        DestroyWindow(editor_);
        editor_ = nullptr;
        editorField_ = nullptr;
    }

    editor_ = CreateWindowExW(WS_EX_APPWINDOW, kEditorClass, title.c_str(),
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 720, 560, nullptr,
                              nullptr, instance_, nullptr);
    if (editor_ == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(editor_, &client);
    const int buttonAreaHeight = editable ? 48 : 0;

    DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_WANTRETURN |
                  ES_AUTOVSCROLL | ES_AUTOHSCROLL;
    if (!editable) {
        style |= ES_READONLY;
    }

    editorField_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, style, 8, 8,
                                   client.right - 16, client.bottom - 16 - buttonAreaHeight,
                                   editor_, reinterpret_cast<HMENU>(IDC_TEXT_AREA), instance_,
                                   nullptr);

    HFONT font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             FIXED_PITCH | FF_MODERN, L"Consolas");
    SendMessageW(editorField_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SetWindowTextW(editorField_, widenWithCrLf(content).c_str());
    SendMessageW(editorField_, EM_SETLIMITTEXT, phpvpn::kIpcMaxMessageBytes, 0);

    if (editable) {
        CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                        client.right - 188, client.bottom - 40, 84, 30, editor_,
                        reinterpret_cast<HMENU>(IDC_SAVE), instance_, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        client.right - 96, client.bottom - 40, 84, 30, editor_,
                        reinterpret_cast<HMENU>(IDC_CANCEL), instance_, nullptr);
    }

    ShowWindow(editor_, SW_SHOW);
    SetForegroundWindow(editor_);
}

LRESULT CALLBACK TrayApplication::editorProcedure(HWND window, UINT message, WPARAM wParam,
                                                  LPARAM lParam) {
    TrayApplication* application = g_application;
    if (application == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    switch (message) {
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_CANCEL) {
                DestroyWindow(window);
                return 0;
            }
            if (LOWORD(wParam) == IDC_SAVE && application->editorIsConfig_) {
                const int length = GetWindowTextLengthW(application->editorField_);
                std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
                GetWindowTextW(application->editorField_, text.data(), length + 1);
                text.resize(static_cast<std::size_t>(length));

                const std::string document = toUnixLineEndings(narrow(text));

                std::string response;
                if (!application->call(phpvpn::IpcCommand::set_config, document, response)) {
                    return 0;
                }

                auto parsed = phpvpn::Json::parse(response);
                if (parsed.ok() && parsed.value().isObject()) {
                    const phpvpn::JsonValue* error = parsed.value().find("error");
                    if (error != nullptr && error->isObject()) {
                        const phpvpn::JsonValue* description = error->find("description");
                        application->showError(
                            L"Edit Config", description != nullptr && description->isString()
                                                ? widen(description->stringValue())
                                                : L"The configuration was rejected.");
                        return 0;
                    }
                }

                MessageBoxW(window,
                            L"The configuration was stored. It takes effect on the next "
                            L"connection attempt.",
                            L"Edit Config", MB_OK | MB_ICONINFORMATION);
                DestroyWindow(window);
                return 0;
            }
            break;

        case WM_SIZE: {
            if (application->editorField_ != nullptr) {
                RECT client{};
                GetClientRect(window, &client);
                const bool editable = application->editorIsConfig_;
                const int buttonAreaHeight = editable ? 48 : 0;
                MoveWindow(application->editorField_, 8, 8, client.right - 16,
                           client.bottom - 16 - buttonAreaHeight, TRUE);
                if (editable) {
                    HWND save = GetDlgItem(window, IDC_SAVE);
                    HWND cancel = GetDlgItem(window, IDC_CANCEL);
                    if (save != nullptr) {
                        MoveWindow(save, client.right - 188, client.bottom - 40, 84, 30, TRUE);
                    }
                    if (cancel != nullptr) {
                        MoveWindow(cancel, client.right - 96, client.bottom - 40, 84, 30, TRUE);
                    }
                }
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            application->editor_ = nullptr;
            application->editorField_ = nullptr;
            return 0;

        default:
            break;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    // One tray per interactive session; a second instance in the same session
    // would only duplicate the icon.
    HANDLE single = CreateMutexW(nullptr, TRUE, kMutexName);
    if (single != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(single);
        return 0;
    }

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    TrayApplication application;
    g_application = &application;

    if (!application.initialise(instance)) {
        MessageBoxW(nullptr, L"The PHPVPN tray application could not start.", L"PHPVPN",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    const int result = application.run();
    g_application = nullptr;
    if (single != nullptr) {
        ReleaseMutex(single);
        CloseHandle(single);
    }
    return result;
}
