#pragma once
#include <windows.h>
#include <functional>
#include <cstdint>
#include <string>
#include <memory>
#include <thread>
#include "layout.h"
#include "core/ipc/ipc_protocol.h"
#include "editor_document.h"

namespace phpvpn::tray {
class EditorWindow {
public:
    explicit EditorWindow(std::wstring pipeName = kIpcPipeNameW) : pipeName_(std::move(pipeName)) {}
    using Save = std::function<bool(const std::string&)>;
    ~EditorWindow();
    void open(HINSTANCE instance, const std::wstring& title, const std::string& content, Save save = {});
    void completeSave(std::uint64_t generation);
    void setSaving(std::uint64_t generation, bool saving);
    std::uint64_t generation() const { return generation_; }
    bool translate(MSG& message) const { return window_ && IsDialogMessageW(window_, &message); }
private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void layout();
    void updateFonts();
    void updateEnabled();
    void reset();
    void pollReset();
    void setContent(const std::string& content);
    struct ResetTask;
    std::shared_ptr<ResetTask> resetTask_;
    std::thread resetThread_;
    bool saving_ = false;
    HWND window_ = nullptr;
    HWND field_ = nullptr;
    HFONT textFont_ = nullptr;
    HFONT buttonFont_ = nullptr;
    Save save_;
    Size oldClient_;
    EditorLayout oldLayout_;
    std::uint64_t generation_ = 0;
    const std::wstring pipeName_;
    EditorDocument document_;
    std::wstring title_;
};
}  // namespace phpvpn::tray
