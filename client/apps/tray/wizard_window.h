#pragma once
#include <functional>
#include <vector>
#include <map>
#include "wizard_reader.h"
#include "core/wizard/wizard_model.h"
#include "layout.h"
#include "wizard_layout.h"
#include "ui_resources.h"
#include "http_error_detail_window.h"
#include "http_headers_window.h"

namespace phpvpn::tray {
// UI-thread value snapshot. No fields, secrets, HWND or mutable model access.
// Generations are local identities, never additions to the IPC schema.
struct WizardLifecycleSnapshot {
    bool open = false, dirty = false, committing = false, commitUnknown = false;
    std::uint64_t windowGeneration = 0, sessionGeneration = 0, draftGeneration = 0;
    std::uint64_t attemptGeneration = 0, operationGeneration = 0;
    std::uint64_t attemptSequence = 0, connectionEpoch = 0;
    std::optional<std::int64_t> commitDeadlineMs;
    bool operator==(const WizardLifecycleSnapshot&) const = default;
};
class WizardWindow {
public:
    explicit WizardWindow(const std::wstring& pipeName)
        : primary_(pipeName), control_(pipeName), heartbeat_(pipeName), disconnect_(pipeName), status_(pipeName), pipeName_(pipeName) {}
    ~WizardWindow();
    bool open(HINSTANCE instance, std::function<void()> onConfirmed = {});
    bool translate(MSG& message);
    bool isOpen() const { return window_ != nullptr; }
    WizardLifecycleSnapshot lifecycleSnapshot() const;
    // Consumes an externally approved exact snapshot. No second dialog or IPC.
    // Local discard abandons readers. Remote cleanup falls back to its lease.
    bool approvedDiscard(const WizardLifecycleSnapshot& approved);
    // Terminal and idempotent. No UI, remote close, reader join or deadline wait.
    void endSession();
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
    void abandonReaders();
    void draftChanged();
    void syncHttpDiagnostic();
    bool send(WizardCommand command, WizardReader& reader);
    WizardRequest request(WizardCommand command) const;
    HWND add(const wchar_t* type, const std::wstring& text, DWORD style, int id, int height = 0);
    void label(const std::wstring& text, bool heading = false);
    void beginCard(ui::Color color = ui::Color::surface);
    void endCard();
    void separator();
    HWND window_ = nullptr, body_ = nullptr, steps_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HFONT font_ = nullptr, headingFont_ = nullptr;
    UiResources resources_;
    WizardLayout geometry_{};
    WizardModel model_;
    HttpErrorDetailWindow detail_;
    HttpHeadersWindow headers_;
    std::optional<WizardRequest> diagnosticRequest_;
    std::optional<std::pair<std::string, std::uint64_t>> diagnosticCleanup_;
    WizardReader primary_, control_, heartbeat_;
    CommandReader disconnect_;
    WizardStatusReader status_;
    std::function<void()> onConfirmed_;
    std::vector<std::pair<HWND, Rect>> children_;
    std::vector<std::pair<HWND, Rect>> previousChildren_;
    std::vector<std::pair<Rect, ui::Color>> cards_;
    std::vector<Rect> separators_;
    int staticId_ = 1000, cardTop_ = 0;
    bool inCard_ = false;
    ui::Color cardColor_ = ui::Color::surface;
    WizardStep renderedStep_ = WizardStep::loading;
    std::map<int, std::pair<DWORD, DWORD>> selections_;
    int dpi_ = 96, contentWidth_ = 0, contentHeight_ = 0, cursorY_ = 0, scroll_ = 0;
    bool updating_ = false, closing_ = false, resetting_ = false, disconnecting_ = false;
    bool offlineKnown_ = false, confirmationShown_ = false;
    std::int64_t lastHeartbeat_ = 0, lastContact_ = 0, lastPoll_ = 0, commitDeadline_ = 0;
    std::string commitId_, commitOperation_;
    std::uint64_t generation_ = 0;
    std::uint64_t windowGeneration_ = 0, draftGeneration_ = 0, attemptGeneration_ = 0, operationGeneration_ = 0;
    bool draftDirty_ = false, endingSession_ = false;
    const std::wstring pipeName_;
};
} // namespace phpvpn::tray
