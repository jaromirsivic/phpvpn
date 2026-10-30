// Configuration Wizard window of the macOS tray, the counterpart of
// apps/tray/wizard_window and apps/tray-linux/wizard_window (tray_UX.md UX08/UX09,
// client.md "Configuration Wizard").
//
// WizardController (the shared GTK-free workflow, wizard_controller.h) owns every
// decision and IPC request; this class is its WizardHost: a 1000 x 680 DIP window
// with the informative sidebar (230 DIP, a compact strip of four numbers below
// 760 DIP), the scrolling step content and the footer Reset | Back, Error detail,
// primary. It owns the HTTP headers sheet and the Wizard's HTTP error detail window.
// Fields persist across renders, so text and selection survive polling, resize and Back.
#pragma once
#include <array>
#include <functional>

#include "ipc_async.h"
#include "mac_http_error_detail_window.h"
#include "mac_http_headers_window.h"
#include "mac_ui.h"
#include "wizard_controller.h"

namespace phpvpn::tray {
class TrayApp;

class WizardWindow final : private WizardHost {
public:
    WizardWindow(IpcEndpoint endpoint, Dispatcher dispatcher);
    ~WizardWindow() override;
    WizardWindow(const WizardWindow&) = delete;
    WizardWindow& operator=(const WizardWindow&) = delete;
    bool open(std::function<void()> onConfirmed = {}) { return controller_.open(std::move(onConfirmed)); }
    bool isOpen() const { return controller_.isOpen(); }
    NSWindow* window() const { return window_; }
    // The Exit/session-end participant (lifecycle.h). Register it first.
    WizardController& controller() { return controller_; }

private:
    struct ErrorPanel {
        PVStackView* card = nil;
        NSTextField* text = nil;
    };
    // WizardHost
    bool createWindow() override;
    void presentWindow() override;
    void destroyWindow() override;
    void render() override;
    void refreshDiagnostic() override;
    void setFieldText(WizardField field, const std::string& text) override;
    void focusField(WizardField field, WizardField fallback) override;
    void notify(std::string_view text, std::string_view title, WizardNotice icon) override;
    bool confirm(std::string_view text, std::string_view title) override;
    void closeDetail() override { detail_.close(); }

    void build();
    PVStackView* buildSidebar();
    void buildFooter();
    PVStackView* buildMessagePage();
    PVStackView* buildUrlPage();
    PVStackView* buildSecretPage();
    PVStackView* buildIdentityPage();
    PVStackView* buildContractsPage();
    NSTextField* entry(WizardField field, const char* accessibleName);
    mac::SwitchRow toggle(WizardField field, const std::string& title);
    ErrorPanel errorPanel();
    PVStackView* page();
    void updateErrorPanel(const ErrorPanel& panel, const WizardPresentation& p);
    void renderSidebar(const WizardPresentation& p);
    void placeCompact();
    void openHeaders();
    void openErrorDetail();
    NSView* widgetFor(WizardField field) const;
    NSView* firstFocusable() const;
    bool ownedWindowActive() const;
    void clearPointers();

    WizardController controller_;
    HttpHeadersWindow headers_;
    HttpErrorDetailWindow detail_;
    // Button actions that may open a modal question run from a deferred call.
    mac::Deferred deferred_;
    mac::Timer timer_;
    bool updating_ = false, compact_ = false;
    WizardStep renderedStep_ = WizardStep::loading;
    bool rendered_ = false;
    PVWindow* window_ = nil;
    PVStackView* root_ = nil;
    PVStackView* body_ = nil;
    PVStackView* sidebar_ = nil;
    NSTextField* sidebarHeading_ = nil;
    std::array<PVStackView*, 4> stepRows_{};
    std::array<PVMarkerView*, 4> stepMarkers_{};
    std::array<NSTextField*, 4> stepTexts_{};
    PVLineView* divider_ = nil;
    PVScrollView* scroller_ = nil;
    PVStackView* content_ = nil;
    PVStackView* messagePage_ = nil;
    PVStackView* urlPage_ = nil;
    PVStackView* secretPage_ = nil;
    PVStackView* identityPage_ = nil;
    PVStackView* contractsPage_ = nil;
    NSTextField* status_ = nil;
    NSButton* reset_ = nil;
    NSButton* back_ = nil;
    NSButton* detailButton_ = nil;
    NSButton* next_ = nil;
    // Message page.
    NSTextField* messageEyebrow_ = nil;
    NSTextField* messageHeading_ = nil;
    NSTextField* messageText_ = nil;
    NSTextField* messageSecond_ = nil;
    PVStackView* gateCard_ = nil;
    NSTextField* offline_ = nil;
    ErrorPanel messageError_, urlError_, secretError_, identityError_, contractsError_;
    // Step pages.
    NSTextField* url_ = nil;
    NSButton* headersButton_ = nil;
    NSTextField* secretLabel_ = nil;
    NSTextField* secret_ = nil;
    NSTextField* secretHint_ = nil;
    NSSwitch* defaultSecret_ = nil;
    PVStackView* defaultSecretWarning_ = nil;
    NSTextField* identityDescription_ = nil;
    NSTextField* ip_ = nil;
    NSSwitch* automaticIp_ = nil;
    NSTextField* noSuggestion_ = nil;
    NSTextField* key_ = nil;
    NSSwitch* defaultKey_ = nil;
    PVStackView* defaultKeyWarning_ = nil;
    NSSwitch* contracts_ = nil;
    std::array<NSTextField*, 3> contractCounts_{};
    NSTextField* contractsUnsupported_ = nil;
    PVStackView* contractsHint_ = nil;
};

// Creates the Wizard and the Connect HTTP error detail window, installs their
// TrayWindowHooks and registers the Wizard as the first lifecycle participant.
void installWizardWindows(TrayApp& app);
}  // namespace phpvpn::tray
