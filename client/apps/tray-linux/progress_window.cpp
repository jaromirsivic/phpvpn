#include "progress_window.h"
#include "gtk_util.h"

#include "dialogs.h"
#include "style.h"
#include "tray_text.h"
#include "ui_style.h"

namespace phpvpn::tray {
namespace {
constexpr int kCountdownBesideWidth = 760;  // tray_UX.md UX05, as computeProgressLayout
const char* outcome(CheckpointResult check) {
    return check == CheckpointResult::success ? "Success" : check == CheckpointResult::failed ? "Failed" : "Pending";
}
const char* glyph(CheckpointResult check) {
    return check == CheckpointResult::success ? "✓" : check == CheckpointResult::failed ? "✗" : "●";
}
const char* checkClass(CheckpointResult check) {
    return check == CheckpointResult::success ? style::kCheckSuccess
         : check == CheckpointResult::failed ? style::kCheckFailed : style::kCheckPending;
}
void setText(GtkWidget* label, const std::string& text) {
    if (label && text != gtk_label_get_text(GTK_LABEL(label))) gtk_label_set_text(GTK_LABEL(label), text.c_str());
}
void setEntry(GtkWidget* entry, const std::string& text) {
    if (entry && text != gtk_entry_get_text(GTK_ENTRY(entry))) gtk_entry_set_text(GTK_ENTRY(entry), text.c_str());
}
std::string closingText(unsigned seconds) {
    return "Closing in " + std::to_string(seconds) + (seconds == 1 ? " second" : " seconds");
}
GtkWidget* label(const char* text, const char* cssClass, bool wrap = false) {
    GtkWidget* widget = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(widget), 0.0f);
    if (wrap) {
        gtk_label_set_line_wrap(GTK_LABEL(widget), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(widget), PANGO_WRAP_WORD_CHAR);
    }
    if (cssClass) style::addClass(widget, cssClass);
    return widget;
}
}  // namespace

ProgressWindow::ProgressWindow(IpcEndpoint endpoint, Dispatcher dispatcher)
    : endpoint_(endpoint), dispatcher_(dispatcher),
      reader_(endpoint, dispatcher, [this](const TrayStatus& status) {
          readPending_ = false;
          onSnapshot(status);
      }),
      detailsReader_(endpoint, dispatcher), dismissReader_(endpoint, dispatcher) {}

ProgressWindow::~ProgressWindow() {
    reader_.abandon();
    detailsReader_.abandon();
    if (window_) gtk_widget_destroy(window_);
}

void ProgressWindow::build() {
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    style::prepareWindow(GTK_WINDOW(window_), disconnect_ ? "PHPVPN disconnection" : "PHPVPN connection",
                         disconnect_ ? ui::disconnectClient : ui::connectClient);
    if (disconnect_) gtk_widget_set_size_request(window_, 400, -1);
    g_signal_connect(window_, "delete-event", gtkCallback([](GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
        static_cast<ProgressWindow*>(data)->requestClose();
        return TRUE;
    }), this);
    // X/Escape close only the progress presentation, never Disconnect (UX05).
    g_signal_connect(window_, "key-press-event", gtkCallback([](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
        if (event->keyval != GDK_KEY_Escape) return FALSE;
        static_cast<ProgressWindow*>(data)->requestClose();
        return TRUE;
    }), this);
    g_signal_connect(window_, "destroy", gtkCallback([](GtkWidget*, gpointer data) {
        static_cast<ProgressWindow*>(data)->onDestroyed();
    }), this);
    g_signal_connect(window_, "size-allocate", gtkCallback([](GtkWidget*, GdkRectangle* allocation, gpointer data) {
        auto* self = static_cast<ProgressWindow*>(data);
        const bool beside = allocation->width >= kCountdownBesideWidth;
        if (beside == self->countdownBeside_ || self->placement_) return;
        // Re-attaching during allocation would recurse into layout; defer it.
        self->placement_ = g_idle_add([](gpointer raw) -> gboolean {
            auto* owner = static_cast<ProgressWindow*>(raw);
            owner->placement_ = 0;
            owner->placeCountdown();
            return G_SOURCE_REMOVE;
        }, self);
    }), this);

    GtkWidget* scroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(window_), scroller);
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, ui::fieldGap);
    gtk_widget_set_margin_top(content, ui::contentMargin);
    gtk_widget_set_margin_bottom(content, ui::contentMargin);
    gtk_widget_set_margin_start(content, ui::compactMargin);
    gtk_widget_set_margin_end(content, ui::compactMargin);
    gtk_container_add(GTK_CONTAINER(scroller), content);

    // Text only, like the Win32 window: the eyebrow, the check glyphs and the coloured
    // result panel carry the state, no decorative state symbol.
    eyebrow_ = label("", style::kEyebrow, true);
    gtk_box_pack_start(GTK_BOX(content), eyebrow_, FALSE, FALSE, 0);

    card_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    style::addClass(card_, style::kCard);
    gtk_widget_set_margin_bottom(card_, ui::fieldGap);
    GtkWidget* detailsHeading = label("Connection details", style::kCardTitle);
    gtk_widget_set_margin_bottom(detailsHeading, 16);
    gtk_box_pack_start(GTK_BOX(card_), detailsHeading, FALSE, FALSE, 0);
    GtkWidget* details = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(details), ui::fieldGap);
    gtk_grid_set_column_spacing(GTK_GRID(details), 16);
    const char* names[]{"Server URL", "Virtual IP"};
    for (int i = 0; i < 2; ++i) {
        GtkWidget* name = label(names[i], style::kLabel);
        gtk_widget_set_size_request(name, 160, -1);
        GtkWidget* field = gtk_entry_new();
        // Read-only but focusable, selectable and copyable (UX05); never editable.
        gtk_editable_set_editable(GTK_EDITABLE(field), FALSE);
        gtk_entry_set_text(GTK_ENTRY(field), "Unavailable");
        gtk_widget_set_hexpand(field, TRUE);
        style::addClass(field, style::kReadOnly);
        style::addClass(field, i == 0 ? style::kMono : style::kValue);
        gtk_label_set_mnemonic_widget(GTK_LABEL(name), field);
        gtk_grid_attach(GTK_GRID(details), name, 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(details), field, 1, i, 1, 1);
        fields_[static_cast<std::size_t>(i)] = field;
    }
    gtk_box_pack_start(GTK_BOX(card_), details, FALSE, FALSE, 0);
    GtkWidget* separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    style::addClass(separator, style::kSeparator);
    gtk_widget_set_margin_top(separator, 16);
    gtk_widget_set_margin_bottom(separator, 20);
    gtk_box_pack_start(GTK_BOX(card_), separator, FALSE, FALSE, 0);
    GtkWidget* checksHeading = label("Connection checks", style::kCardTitle);
    gtk_widget_set_margin_bottom(checksHeading, 16);
    gtk_box_pack_start(GTK_BOX(card_), checksHeading, FALSE, FALSE, 0);
    GtkWidget* checks = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(checks), ui::fieldGap);
    gtk_grid_set_column_spacing(GTK_GRID(checks), ui::relatedGap);
    const char* checkNames[]{"Server URL", "Server secret", "Access key", "Communication contracts"};
    for (int i = 0; i < 4; ++i) {
        GtkWidget* icon = label("", style::kValue);
        gtk_widget_set_size_request(icon, 28, -1);
        GtkWidget* name = label(checkNames[i], style::kValue);
        gtk_widget_set_size_request(name, 228, -1);
        GtkWidget* state = label("", style::kValue);
        gtk_widget_set_hexpand(state, TRUE);
        gtk_grid_attach(GTK_GRID(checks), icon, 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(checks), name, 1, i, 1, 1);
        gtk_grid_attach(GTK_GRID(checks), state, 2, i, 1, 1);
        checkIcons_[static_cast<std::size_t>(i)] = icon;
        checks_[static_cast<std::size_t>(i)] = state;
    }
    gtk_box_pack_start(GTK_BOX(card_), checks, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), card_, FALSE, FALSE, 0);

    panel_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    style::addClass(panel_, style::kPanel);
    panelGrid_ = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(panelGrid_), 16);
    gtk_grid_set_row_spacing(GTK_GRID(panelGrid_), ui::spacing);
    panelHeading_ = label("", style::kCardTitle, true);
    gtk_widget_set_hexpand(panelHeading_, TRUE);
    panelAction_ = label("", style::kLabel, true);
    countdown_ = label("", style::kValue);
    gtk_label_set_xalign(GTK_LABEL(countdown_), 1.0f);
    errorDetail_ = gtk_button_new_with_label("Error detail");
    style::addClass(errorDetail_, style::kErrorButton);
    gtk_widget_set_valign(errorDetail_, GTK_ALIGN_CENTER);
    gtk_widget_set_no_show_all(errorDetail_, TRUE);
    gtk_widget_set_no_show_all(countdown_, TRUE);
    // The fallback message is modal; it never runs inside the click action.
    g_signal_connect(errorDetail_, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        auto* self = static_cast<ProgressWindow*>(data);
        self->deferred_.post([self] {
            const auto& diagnostic = self->model_.httpDiagnostic();
            if (!self->window_ || !diagnostic) return;
            if (self->openErrorDetail) self->openErrorDetail(GTK_WINDOW(self->window_), *diagnostic);
            else showMessage(GTK_WINDOW(self->window_), kErrorDetailUnavailableText, "PHPVPN", DialogIcon::information);
        });
    }), this);
    // The success countdown starts at the first real paint of the visible heading.
    g_signal_connect_after(panelHeading_, "draw", gtkCallback([](GtkWidget* widget, cairo_t*, gpointer data) -> gboolean {
        auto* self = static_cast<ProgressWindow*>(data);
        if (!self->window_ || !gtk_widget_get_mapped(widget) ||
            self->model_.view().stage != ProgressStage::succeeded) return FALSE;
        if (GdkWindow* surface = gtk_widget_get_window(self->window_);
            surface && (gdk_window_get_state(surface) & GDK_WINDOW_STATE_ICONIFIED)) return FALSE;
        self->model_.onSuccessPresented(monotonicMs());
        return FALSE;
    }), this);
    gtk_grid_attach(GTK_GRID(panelGrid_), panelHeading_, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(panelGrid_), panelAction_, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(panelGrid_), countdown_, 1, 0, 1, 2);
    gtk_grid_attach(GTK_GRID(panelGrid_), errorDetail_, 2, 0, 1, 2);
    countdownBeside_ = true;
    gtk_box_pack_start(GTK_BOX(panel_), panelGrid_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), panel_, FALSE, FALSE, 0);
    gtk_widget_show_all(scroller);
}

void ProgressWindow::placeCountdown() {
    if (!window_ || !countdown_) return;
    const bool beside = gtk_widget_get_allocated_width(window_) >= kCountdownBesideWidth;
    if (beside == countdownBeside_) return;
    g_object_ref(countdown_);
    gtk_container_remove(GTK_CONTAINER(panelGrid_), countdown_);
    if (beside) gtk_grid_attach(GTK_GRID(panelGrid_), countdown_, 1, 0, 1, 2);
    else gtk_grid_attach(GTK_GRID(panelGrid_), countdown_, 0, 2, 1, 1);
    gtk_label_set_xalign(GTK_LABEL(countdown_), beside ? 1.0f : 0.0f);
    gtk_widget_set_margin_top(countdown_, beside ? 0 : ui::relatedGap);
    g_object_unref(countdown_);
    countdownBeside_ = beside;
}

void ProgressWindow::open(bool disconnect) {
    if (closeErrorDetail) closeErrorDetail();
    diagnosticEpoch_ = 0;
    diagnosticAttemptId_.clear();
    reader_.abandon();
    detailsReader_.abandon();
    readPending_ = false;
    disconnect_ = disconnect;
    waitingForReply_ = disconnect;
    model_ = ProgressModel{disconnect};
    details_ = {};
    if (!window_) build();
    gtk_window_set_title(GTK_WINDOW(window_), disconnect_ ? "PHPVPN disconnection" : "PHPVPN connection");
    setText(eyebrow_, disconnect_ ? "Disconnecting client from the server." : "Connecting client to the server.");
    gtk_widget_set_visible(card_, !disconnect_);
    refresh();
    polling_.start(monotonicMs());
    if (timer_) g_source_remove(timer_);
    timer_ = g_timeout_add(kProgressIntervalMs, [](gpointer data) -> gboolean {
        static_cast<ProgressWindow*>(data)->tick();
        return G_SOURCE_CONTINUE;
    }, this);
    gtk_window_present(GTK_WINDOW(window_));
    // One separate read per local Connect. It cannot change the bootstrap model.
    if (!disconnect_ && !detailsReader_.request(IpcCommand::get_config, {}, [this](std::string& response) {
            details_ = consumeProgressConfig(std::move(response));
            refresh();
        }))
        details_ = {};
    // The first status read is due in 500 ms. The command reply seeds the view.
}

void ProgressWindow::onDisconnectReply(const TrayStatus& snapshot) {
    if (!window_ || !disconnect_ || !waitingForReply_) return;
    waitingForReply_ = false;
    if (!snapshot.valid) {
        model_.onCommandFailure();
        polling_.stop();
        refresh();
        return;
    }
    onSnapshot(snapshot);
}

void ProgressWindow::onDisconnectTimeout() {
    if (!window_ || !disconnect_) return;
    waitingForReply_ = false;
    reader_.abandon();
    readPending_ = false;
    polling_.stop();
    model_.onCommandFailure();
    refresh();
}

void ProgressWindow::endSession() {
    // Revoke deliveries before destroying the widgets.
    reader_.abandon();
    detailsReader_.abandon();
    if (window_) gtk_widget_destroy(window_);
}

void ProgressWindow::requestClose() {
    if (!window_) return;
    if (disconnectPending() && !model_.view().statusUnavailable && model_.view().errorCode.empty()) return;
    model_.onClose();
    gtk_widget_destroy(window_);
}

void ProgressWindow::refresh() {
    if (!window_) return;
    const bool diagnostic = model_.httpDiagnostic().has_value();
    gtk_widget_set_visible(errorDetail_, diagnostic);
    if (!diagnostic && closeErrorDetail) closeErrorDetail();
    const auto& v = model_.view();
    const bool success = v.stage == ProgressStage::succeeded, failure = v.stage == ProgressStage::failed;
    const auto& error = progressErrorText(v.statusUnavailable ? "status_unavailable" : v.errorCode);
    bool waiting = false;
    bool panelVisible = false;
    if (disconnect_) {
        const bool issue = failure || v.statusUnavailable || !v.errorCode.empty();
        waiting = !success && !issue;
        setText(panelHeading_, success ? "Disconnected" : issue ? "Disconnect not confirmed" : "Disconnecting...");
        setText(panelAction_, success ? "You have been disconnected successfully."
                              : issue ? "The service did not confirm Disconnect. Try Disconnect again or check the service status."
                                      : "Please wait while the VPN disconnects.");
        panelVisible = true;
        gtk_window_set_deletable(GTK_WINDOW(window_), success || issue);
    } else {
        setEntry(fields_[0], details_.available ? details_.serverUrl : "Unavailable");
        setEntry(fields_[1], details_.available ? details_.virtualIp : "Unavailable");
        const CheckpointResult checks[]{v.serverUrl, v.serverSecret, v.accessKey, v.communicationContracts};
        for (std::size_t i = 0; i < 4; ++i) {
            setText(checks_[i], outcome(checks[i]));
            setText(checkIcons_[i], glyph(checks[i]));
            for (auto* widget : {checks_[i], checkIcons_[i]})
                for (const char* name : {style::kCheckSuccess, style::kCheckFailed, style::kCheckPending})
                    style::setClass(widget, name, name == checkClass(checks[i]));
        }
        setText(panelHeading_, success ? "Connected successfully"
                              : failure || v.statusUnavailable ? std::string(error.panel) : std::string{});
        setText(panelAction_, success ? "This window will close automatically."
                             : failure || v.statusUnavailable ? std::string(error.action) : std::string{});
        panelVisible = success || failure || v.statusUnavailable;
    }
    setText(countdown_, success ? closingText(v.closingSeconds) : std::string{});
    gtk_widget_set_visible(countdown_, success);
    gtk_widget_set_visible(panel_, panelVisible);
    style::setClass(panel_, style::kPanelSuccess, success);
    style::setClass(panel_, style::kPanelWarning, !success && waiting);
    style::setClass(panel_, style::kPanelError, !success && !waiting);
}

void ProgressWindow::onSnapshot(const TrayStatus& snapshot) {
    if (!window_) return;
    model_.onSnapshot(snapshot, monotonicMs());
    if (disconnect_ && disconnectSnapshot) disconnectSnapshot(snapshot);
    if (!window_) return;
    if (model_.httpDiagnostic()) {
        diagnosticEpoch_ = model_.connectionEpoch();
        diagnosticAttemptId_ = model_.connectAttemptId().value_or("");
    }
    if (model_.view().closeRequested) {
        gtk_widget_destroy(window_);
        return;
    }
    refresh();
}

void ProgressWindow::tick() {
    if (!window_) return;
    const auto before = model_.view().closingSeconds;
    model_.onTick(monotonicMs());
    if (model_.view().closeRequested) {
        gtk_widget_destroy(window_);
        return;
    }
    if (before != model_.view().closingSeconds) setText(countdown_, closingText(model_.view().closingSeconds));
    if (!waitingForReply_ && polling_.onTimer(monotonicMs()) && !readPending_ &&
        (model_.view().stage == ProgressStage::connecting || model_.httpDiagnostic())) {
        readPending_ = true;
        reader_.request();
    }
}

void ProgressWindow::onDestroyed() {
    if (!disconnect_ && diagnosticEpoch_)
        dismissReader_.request(IpcCommand::dismiss_connect_diagnostic,
                               IpcProtocol::dismissDiagnosticPayload(diagnosticEpoch_, diagnosticAttemptId_), {});
    diagnosticEpoch_ = 0;
    diagnosticAttemptId_.clear();
    model_.onClose();
    if (closeErrorDetail) closeErrorDetail();
    if (timer_) { g_source_remove(timer_); timer_ = 0; }
    if (placement_) { g_source_remove(placement_); placement_ = 0; }
    polling_.stop();
    reader_.abandon();
    detailsReader_.abandon();
    readPending_ = false;
    window_ = eyebrow_ = card_ = panel_ = panelGrid_ = panelHeading_ = panelAction_ = countdown_ =
        errorDetail_ = nullptr;
    fields_ = {};
    checks_ = {};
    checkIcons_ = {};
    details_ = {};
}
}  // namespace phpvpn::tray
