// GTK-free Configuration Wizard controller of the Linux tray (WP10a), the portable
// counterpart of the workflow in apps/tray/wizard_window.cpp.
//
// The Windows WizardWindow mixes the workflow with Win32 controls. Here the workflow
// (keepalive lease, attempts and operation polling, disconnect gate, resume, reset,
// close, Finish with commitId, get-commit, lifecycle snapshot) lives in this class
// and talks to the service through the asynchronous IpcChannels of ipc_async.h. The
// GTK window (wizard_window.cpp) is only a WizardHost: it builds widgets, forwards
// user input and paints WizardPresentation. Every decision, text and IPC request
// follows the Windows implementation; the unit tests drive this class against a
// scripted Unix-socket service with a fake clock.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/wizard/wizard_model.h"
#include "ipc_async.h"
#include "lifecycle.h"
#include "ui_style.h"

namespace phpvpn::tray {
// UI timer of the Wizard window (Windows SetTimer 250 ms); operations are polled every 500 ms.
inline constexpr unsigned kWizardTickMs = 250;

// UI-thread value snapshot, the same identities as the Windows WizardLifecycleSnapshot.
// No fields, secrets, widgets or mutable model access.
struct WizardLifecycleSnapshot {
    bool open = false, dirty = false, committing = false, commitUnknown = false;
    std::uint64_t windowGeneration = 0, sessionGeneration = 0, draftGeneration = 0;
    std::uint64_t attemptGeneration = 0, operationGeneration = 0;
    std::uint64_t attemptSequence = 0, connectionEpoch = 0;
    std::optional<std::int64_t> commitDeadlineMs;
    bool operator==(const WizardLifecycleSnapshot&) const = default;
};
// The common Exit snapshot of lifecycle.h: committing is busy, commitUnknown is
// unknown, and generation folds every identity above, so an approval applies to
// exactly the confirmed state.
LifecycleSnapshot wizardExitSnapshot(const WizardLifecycleSnapshot& snapshot);

enum class WizardField { none, url, secret, defaultSecret, ip, automaticIp, key, defaultKey, contracts, headers };
enum class WizardNotice { information, warning, error };

struct WizardSidebarItem {
    std::string name;
    // "Completed", "Failed", "Upcoming" or empty for the active pending step.
    std::string status;
    // Number, check mark or cross inside the round marker.
    std::string symbol;
    ui::Color color = ui::Color::secondaryText;
    bool active = false;
};

// Everything the window shows, derived from the controller state only.
struct WizardPresentation {
    WizardStep step = WizardStep::loading;
    bool locked = false;
    std::array<WizardSidebarItem, 4> sidebar;
    // Accessible text of the informative sidebar (the Windows steps STATIC text).
    std::string sidebarText;
    // Message pages (loading, gate, loadError, sessionExpired, commitUnknown, finished).
    std::string eyebrow, heading, text, secondText;
    bool gateCard = false, offline = false;
    // Error panel ("Could not complete this action") with the final text.
    std::optional<std::string> errorText;
    WizardField invalidField = WizardField::none;
    // Progress text below the content while locked, empty otherwise.
    std::string statusText;
    // Secret step.
    std::string secretLabel, secretHint;
    bool defaultSecretWarning = false;
    // Identity step.
    std::string identityDescription;
    bool noSuggestion = false, defaultKeyWarning = false;
    // Contracts step: existing, missing, to add on Finish.
    std::array<std::size_t, 3> contractCounts{};
    bool contractsUnsupported = false, contractsHint = false;
    // Field and switch values.
    std::string url, secret, ip, key;
    bool useDefaultSecret = false, autoDetectIp = false, useDefaultKey = false, createContracts = false;
    bool secretEnabled = false, ipEnabled = false, keyEnabled = false, contractsEnabled = false;
    // Footer: Reset, Back, Error detail, primary.
    std::string primaryLabel;
    bool primaryEnabled = false, backVisible = false, backEnabled = false, resetEnabled = false;
    bool detailVisible = false, detailEnabled = false;
};

// The native side. Every call happens on the UI thread; notify and confirm run a
// nested main loop like the Win32 MessageBox, so IPC deliveries and the tick keep
// running while they are open.
class WizardHost {
public:
    virtual ~WizardHost() = default;
    virtual bool createWindow() = 0;
    virtual void presentWindow() = 0;
    // Wipes the entry texts, closes the owned windows and destroys the window.
    virtual void destroyWindow() = 0;
    virtual void render() = 0;
    // Shows or hides the Error detail button only (text edits, as Windows layout()).
    virtual void refreshDiagnostic() = 0;
    // Replaces the text of one entry without reporting it as a user edit.
    virtual void setFieldText(WizardField field, const std::string& text) = 0;
    // Focuses field, or fallback when field is disabled (Windows next()).
    virtual void focusField(WizardField field, WizardField fallback) = 0;
    virtual void notify(std::string_view text, std::string_view title, WizardNotice icon) = 0;
    // Yes/No question with default No.
    virtual bool confirm(std::string_view text, std::string_view title) = 0;
    virtual void closeDetail() = 0;
};

class WizardController final : public LifecycleParticipant {
public:
    using Clock = std::function<std::int64_t()>;
    using IdSource = std::function<std::string()>;
    WizardController(IpcEndpoint endpoint, Dispatcher dispatcher, WizardHost& host, Clock clock = monotonicMs,
                     IdSource ids = {});
    ~WizardController() override;
    WizardController(const WizardController&) = delete;
    WizardController& operator=(const WizardController&) = delete;

    // Opens the window and sends wizard-open, or activates the open window.
    bool open(std::function<void()> onConfirmed = {});
    bool isOpen() const { return open_; }
    // 250 ms UI timer of the window.
    void tick();
    // User actions.
    void next();
    void back();
    void cancel(bool reset);
    void editUrl(std::string text);
    void editSecret(std::string text);
    void editIp(std::string text);
    void editKey(std::string text);
    void setDefaultSecret(bool checked);
    void setAutoDetectIp(bool checked);
    void setDefaultKey(bool checked);
    void setCreateContracts(bool checked);
    // HTTP headers dialog OK: validated by the shared model; changes invalidate the checks.
    Status acceptHttpHeaders(std::string text);
    // True while the window may take user input (not ending the session).
    bool acceptsInput() const { return open_ && !endingSession_; }

    WizardPresentation presentation() const;
    const WizardModel& model() const { return model_; }
    WizardLifecycleSnapshot wizardSnapshot() const;
    // Consumes an externally approved exact snapshot. No second dialog or IPC.
    bool approvedDiscard(const WizardLifecycleSnapshot& approved);

    // LifecycleParticipant (lifecycle.h).
    LifecycleSnapshot lifecycleSnapshot() const override { return wizardExitSnapshot(wizardSnapshot()); }
    bool approvedDiscard(const LifecycleSnapshot& approved) override;
    // Terminal and idempotent. No UI, remote close, worker join or deadline wait.
    void endSession() override;
    // The native window disappeared without the controller (defensive).
    void windowLost();
    // Destructor path of the Windows window: one best-effort wizard-close with the
    // 1 s budget while a session is open, then local destruction. Call before the
    // host is destroyed.
    void closeOnExit();

private:
    void render();
    void fail(WizardError error);
    void finish();
    void confirm(const WizardReceipt& receipt);
    void destroy();
    void abandonChannels();
    void draftChanged();
    void syncHttpDiagnostic();
    bool send(WizardCommand command, IpcChannel& channel);
    bool sendRequest(WizardRequest request, IpcChannel& channel);
    void receive(WizardResult& result);
    void receiveDisconnect(const std::string& response);
    void receiveStatus(const std::string& response);
    WizardRequest request(WizardCommand command) const;
    std::string newId() const;
    bool locked() const;

    IpcEndpoint endpoint_;
    WizardHost& host_;
    Clock clock_;
    IdSource ids_;
    WizardModel model_;
    IpcChannel primary_, control_, heartbeat_, disconnect_, status_;
    std::optional<WizardRequest> diagnosticRequest_;
    std::optional<std::pair<std::string, std::uint64_t>> diagnosticCleanup_;
    std::function<void()> onConfirmed_;
    bool open_ = false;
    bool closing_ = false, resetting_ = false, disconnecting_ = false;
    bool offlineKnown_ = false, confirmationShown_ = false;
    std::int64_t lastHeartbeat_ = 0, lastContact_ = 0, lastPoll_ = 0, commitDeadline_ = 0;
    std::string commitId_, commitOperation_;
    std::uint64_t generation_ = 0;
    std::uint64_t windowGeneration_ = 0, draftGeneration_ = 0, attemptGeneration_ = 0, operationGeneration_ = 0;
    bool draftDirty_ = false, endingSession_ = false;
};
}  // namespace phpvpn::tray
