// Configuration Wizard window of the GTK tray (WP10a), the counterpart of
// apps/tray/wizard_window (tray_UX.md UX08/UX09, client.md "Configuration Wizard").
//
// WizardController owns the workflow and every IPC request; this class is its
// WizardHost: a 1000 x 680 DIP window with the informative sidebar (230 DIP, a
// compact strip of four numbers below 760 DIP), the scrolling step content and the
// footer Reset | Back, Error detail, primary. It owns the HTTP headers dialog and
// the Wizard's HTTP error detail window. Entries persist across renders, so text
// and selection survive polling, resize and Back.
#pragma once
#include <gtk/gtk.h>

#include <array>
#include <functional>
#include <memory>

#include "gtk_util.h"  // DeferredCalls
#include "http_error_detail_window.h"
#include "http_headers_window.h"
#include "ipc_async.h"
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
    GtkWindow* window() const { return window_ ? GTK_WINDOW(window_) : nullptr; }
    // The Exit/session-end participant (lifecycle.h). Register it first.
    WizardController& controller() { return controller_; }

private:
    struct Page;
    struct ErrorPanel { GtkWidget* card = nullptr; GtkWidget* text = nullptr; };
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
    GtkWidget* buildSidebar();
    GtkWidget* buildFooter();
    GtkWidget* buildMessagePage();
    GtkWidget* buildUrlPage();
    GtkWidget* buildSecretPage();
    GtkWidget* buildIdentityPage();
    GtkWidget* buildContractsPage();
    GtkWidget* entry(WizardField field, const char* accessibleName);
    GtkWidget* toggle(WizardField field, const char* label, GtkWidget** row);
    ErrorPanel errorPanel();
    void updateErrorPanel(const ErrorPanel& panel, const WizardPresentation& p);
    void renderSidebar(const WizardPresentation& p);
    void placeCompact();
    void openHeaders();
    void openErrorDetail();
    bool onKey(GdkEventKey* event);
    void onDestroyed();
    GtkWidget* widgetFor(WizardField field) const;
    GtkWidget* firstFocusable() const;
    bool ownedWindowActive() const;

    WizardController controller_;
    HttpHeadersWindow headers_;
    HttpErrorDetailWindow detail_;
    // Button actions that may open a modal question run from an idle callback.
    DeferredCalls deferred_;
    bool updating_ = false, destroying_ = false, compact_ = false;
    WizardStep renderedStep_ = WizardStep::loading;
    bool rendered_ = false;
    guint timer_ = 0, placement_ = 0;
    GtkWidget* window_ = nullptr;
    GtkWidget* body_ = nullptr;
    GtkWidget* sidebar_ = nullptr;
    GtkWidget* sidebarHeading_ = nullptr;
    std::array<GtkWidget*, 4> stepRows_{}, stepMarkers_{}, stepTexts_{};
    GtkWidget* divider_ = nullptr;
    GtkWidget* scroller_ = nullptr;
    GtkWidget* stack_ = nullptr;
    GtkWidget* status_ = nullptr;
    GtkWidget* footer_ = nullptr;
    GtkWidget* footerTop_ = nullptr;
    GtkWidget* footerRow_ = nullptr;
    GtkWidget* reset_ = nullptr;
    GtkWidget* back_ = nullptr;
    GtkWidget* detailButton_ = nullptr;
    GtkWidget* next_ = nullptr;
    // Message page.
    GtkWidget* messageEyebrow_ = nullptr;
    GtkWidget* messageHeading_ = nullptr;
    GtkWidget* messageText_ = nullptr;
    GtkWidget* messageSecond_ = nullptr;
    GtkWidget* gateCard_ = nullptr;
    GtkWidget* offline_ = nullptr;
    ErrorPanel messageError_, urlError_, secretError_, identityError_, contractsError_;
    // Step pages.
    GtkWidget* url_ = nullptr;
    GtkWidget* headersButton_ = nullptr;
    GtkWidget* secretLabel_ = nullptr;
    GtkWidget* secret_ = nullptr;
    GtkWidget* secretHint_ = nullptr;
    GtkWidget* defaultSecret_ = nullptr;
    GtkWidget* defaultSecretWarning_ = nullptr;
    GtkWidget* identityDescription_ = nullptr;
    GtkWidget* ip_ = nullptr;
    GtkWidget* automaticIp_ = nullptr;
    GtkWidget* noSuggestion_ = nullptr;
    GtkWidget* key_ = nullptr;
    GtkWidget* defaultKey_ = nullptr;
    GtkWidget* defaultKeyWarning_ = nullptr;
    GtkWidget* contracts_ = nullptr;
    std::array<GtkWidget*, 3> contractCounts_{};
    GtkWidget* contractsUnsupported_ = nullptr;
    GtkWidget* contractsHint_ = nullptr;
};

// Creates the Wizard and the Connect HTTP error detail window, installs their
// TrayWindowHooks and registers the Wizard as the first lifecycle participant.
// Called once from the TrayApp constructor; the windows live until process exit.
void installWizardWindows(TrayApp& app);
}  // namespace phpvpn::tray
