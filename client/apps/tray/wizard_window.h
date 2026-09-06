#pragma once
#include <functional>
#include <vector>
#include "wizard_reader.h"
#include "core/wizard/wizard_model.h"
#include "layout.h"

namespace phpvpn::tray {
class WizardWindow {
public:
    explicit WizardWindow(const std::wstring& pipeName = kIpcPipeNameW)
        : primary_(pipeName), control_(pipeName), heartbeat_(pipeName), disconnect_(pipeName), status_(pipeName), pipeName_(pipeName) {}
    ~WizardWindow();
    bool open(HINSTANCE instance, std::function<void()> onConfirmed);
    bool translate(MSG& message);
    bool isOpen() const { return window_ != nullptr; }
private:
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK bodyProcedure(HWND, UINT, WPARAM, LPARAM);
    void render();
    void layout();
    void scroll(int position);
    void revealFocus();
    void drawSteps(const DRAWITEMSTRUCT& item);
    void command(int id, int notification);
    void tick();
    void receive(WizardReader& reader);
    void receiveDisconnect();
    void error(WizardError error);
    void next();
    void cancel(bool reset);
    void finish();
    void confirm(const WizardReceipt& receipt);
    void destroy();
    bool send(WizardCommand command, WizardReader& reader);
    WizardRequest request(WizardCommand command) const;
    HWND add(const wchar_t* type, const std::wstring& text, DWORD style, int id, int height = 0);
    void label(const std::wstring& text, bool heading = false);
    HWND window_ = nullptr, body_ = nullptr, steps_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HFONT font_ = nullptr, headingFont_ = nullptr;
    WizardModel model_;
    WizardReader primary_, control_, heartbeat_;
    CommandReader disconnect_;
    StatusReader status_;
    std::function<void()> onConfirmed_;
    std::vector<std::pair<HWND, Rect>> children_;
    int dpi_ = 96, contentWidth_ = 0, contentHeight_ = 0, cursorY_ = 0, scroll_ = 0;
    bool updating_ = false, closing_ = false, resetting_ = false, disconnecting_ = false;
    bool offlineKnown_ = false, confirmationShown_ = false;
    std::int64_t lastHeartbeat_ = 0, lastContact_ = 0, lastPoll_ = 0, commitDeadline_ = 0;
    std::string commitId_, commitOperation_;
    std::uint64_t generation_ = 0;
    const std::wstring pipeName_;
};
} // namespace phpvpn::tray
