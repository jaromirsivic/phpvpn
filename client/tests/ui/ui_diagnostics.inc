// Test host only. Opt-in numeric trace, never configuration text or IPC payloads.
// The hook observes the current UI thread and does not alter message delivery.
struct UiDiagnostics {
    struct Entry { ULONGLONG time; DWORD thread; const char* event; UINT_PTR a, b, c; };
    std::mutex mutex;
    std::vector<Entry> entries;
    HHOOK hook = nullptr;
    ULONGLONG started = 0;
    std::size_t dropped = 0;
    bool enabled = false;
    void add(const char* event, UINT_PTR a = 0, UINT_PTR b = 0, UINT_PTR c = 0) noexcept {
        if (!enabled) return;
        try {
            std::lock_guard guard(mutex);
            if (entries.size() < 65536) entries.push_back({GetTickCount64() - started, GetCurrentThreadId(), event, a, b, c});
            else ++dropped;
        } catch (...) {} // Observation must never unwind through a Win32 hook.
    }
    void state(const char* event, HWND root) {
        if (!enabled) return;
        add(event, reinterpret_cast<UINT_PTR>(root), reinterpret_cast<UINT_PTR>(GetFocus()), reinterpret_cast<UINT_PTR>(GetActiveWindow()));
        add("foreground/capture", reinterpret_cast<UINT_PTR>(GetForegroundWindow()), reinterpret_cast<UINT_PTR>(GetCapture()));
        // BM_CLICK may synchronously close its owner (for example Password OK).
        // A diagnostic after the click must not inspect that destroyed HWND.
        if (!root || !ownWindow(root)) return;
        for (int id : {nextId, urlId, secretId, ipId, contractsId}) {
            const HWND control = child(root, id);
            add("control:id/hwnd/visible+enabled", id, reinterpret_cast<UINT_PTR>(control),
                (IsWindowVisible(control) ? 1 : 0) | (IsWindowEnabled(control) ? 2 : 0));
        }
    }
    static LRESULT CALLBACK messages(int code, WPARAM w, LPARAM l);
    void begin() {
        enabled = GetEnvironmentVariableW(L"PHPVPN_UI_TRACE", nullptr, 0) != 0;
        if (!enabled) return;
        entries.reserve(65536); started = GetTickCount64();
        STARTUPINFOW startup{sizeof(startup)}; GetStartupInfoW(&startup);
        add("startup:flags/show/console", startup.dwFlags, startup.wShowWindow, reinterpret_cast<UINT_PTR>(GetConsoleWindow()));
        hook = SetWindowsHookExW(WH_CALLWNDPROC, messages, nullptr, GetCurrentThreadId());
        check(hook != nullptr, "UI diagnostic hook failed");
    }
    void finish() {
        if (!enabled) return;
        UnhookWindowsHookEx(hook); hook = nullptr;
        std::lock_guard guard(mutex);
        for (const auto& e : entries)
            std::cout << "[ UI TRACE ] " << e.time << ' ' << e.thread << ' ' << e.event << ' ' << e.a << ' ' << e.b << ' ' << e.c << '\n';
        std::cout << "[ UI TRACE END ] dropped=" << dropped << '\n';
    }
    struct Scope { ~Scope(); };
} uiDiagnostics;
UiDiagnostics::Scope::~Scope() { uiDiagnostics.finish(); }
LRESULT CALLBACK UiDiagnostics::messages(int code, WPARAM w, LPARAM l) {
    if (code >= 0) {
        const auto& m = *reinterpret_cast<CWPSTRUCT*>(l);
        if (m.message == WM_COMMAND || m.message == BM_CLICK || m.message == WM_LBUTTONDOWN || m.message == WM_LBUTTONUP ||
            m.message == WM_SETFOCUS || m.message == WM_KILLFOCUS || m.message == WM_TIMER ||
            m.message == WM_CLOSE || m.message == WM_DESTROY || (m.message >= WM_APP + 51 && m.message <= WM_APP + 55))
            uiDiagnostics.add("sent:hwnd/msg/wparam", reinterpret_cast<UINT_PTR>(m.hwnd), m.message, m.wParam);
    }
    return CallNextHookEx(nullptr, code, w, l);
}
