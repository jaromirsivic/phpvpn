#pragma once
#include "script_runner.h"
#include <memory>
#include <mutex>
#include <thread>
#include <oleacc.h>

namespace phpvpn::package {
class PackageWindow {
public:
    explicit PackageWindow(Mode mode) : mode_(mode) {}
    ~PackageWindow();
    bool open(HINSTANCE instance, const std::filesystem::path& directory);
    HWND handle() const { return window_; }
    HWND logHandle() const { return log_; }
    State state() const { return state_; }
private:
    struct Shared {
        std::mutex mutex;
        std::wstring pending;
        Result result;
        bool done = false;
        std::atomic_bool cancel{false};
    };
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    LRESULT message(UINT, WPARAM, LPARAM);
    void start();
    void poll();
    void append(const std::wstring& text);
    void layout();
    void updateDpi();
    void stop();
    Mode mode_;
    State state_ = State::running;
    HWND window_ = nullptr, log_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HFONT font_ = nullptr;
    HICON largeIcon_ = nullptr, smallIcon_ = nullptr;
    IAccPropServices* accessibility_ = nullptr;
    bool comInitialized_ = false;
    std::filesystem::path directory_;
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
    std::thread worker_;
    bool started_ = false, captureFailed_ = false, closeNotice_ = false;
    bool pendingCr_ = false;
};
} // namespace phpvpn::package
