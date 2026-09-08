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
#include "ui_resources.h"
#include "status_reader.h"

namespace phpvpn::tray {
struct EditorLifecycleSnapshot {
    bool open = false, dirty = false, saving = false, unknown = false;
    std::uint64_t windowGeneration = 0, draftGeneration = 0, operationGeneration = 0;
    std::uint64_t deadlineMs = 0;
    bool operator==(const EditorLifecycleSnapshot&) const = default;
};
class EditorWindow {
public:
    explicit EditorWindow(std::wstring pipeName) : pipeName_(std::move(pipeName)) {}
    using Save = std::function<bool(const std::string&)>;
    ~EditorWindow();
    void open(HINSTANCE instance, const std::wstring& title, const std::string& content, Save save = {},
              const std::string& configPath = {}, std::function<void()> pollSave = {},
              std::function<void()> abandonSave = {});
    EditorLifecycleSnapshot lifecycleSnapshot() const;
    std::optional<SaveOperation> beginSave(std::uint64_t deadlineMs);
    bool finishSave(const SaveResult& result);
    bool approvedDiscard(const EditorLifecycleSnapshot& approved);
    void endSession();
    std::uint64_t generation() const { return generation_; }
    bool translate(MSG& message) const { return window_ && IsDialogMessageW(window_, &message); }
    HWND window() const { return window_; }
private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void layout();
    void updateFonts();
    void updateEnabled();
    void reset();
    void pollReset();
    void pollLifecycle();
    void requestClose();
    void clearDraft();
    void setContent(const std::string& content);
    struct ResetTask;
    std::shared_ptr<ResetTask> resetTask_;
    std::thread resetThread_;
    bool saving_ = false;
    bool unknown_ = false, endingSession_ = false, changingContent_ = false;
    std::uint64_t draftGeneration_ = 0, operationGeneration_ = 0;
    SaveOperation saveOperation_;
    HWND window_ = nullptr;
    HWND field_ = nullptr;
    UiResources resources_;
    HINSTANCE instance_ = nullptr;
    HWND heading_ = nullptr, path_ = nullptr;
    Save save_;
    std::function<void()> pollSave_, abandonSave_;
    Size oldClient_;
    EditorLayout oldLayout_;
    std::uint64_t generation_ = 0;
    const std::wstring pipeName_;
    EditorDocument document_;
    std::wstring title_;
};
}  // namespace phpvpn::tray
