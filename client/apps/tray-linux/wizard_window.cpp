#include "wizard_window.h"
#include "gtk_util.h"

#include <memory>
#include <vector>

#include "dialogs.h"
#include "style.h"
#include "tray_app.h"
#include "tray_text.h"
#include "ui_style.h"
#include "wizard_style.h"
#include "wizard_text.h"

namespace phpvpn::tray {
namespace {
constexpr Size kWizardMinimum{480, 320};
constexpr int kSidebarWidth = 230;      // wizard_layout.cpp: fixed 230 DIP step panel
constexpr int kCompactWidth = 760;      // tray_UX.md UX03: compact sidebar below 760 DIP
constexpr int kPrimaryMinWidth = 200;   // computeWizardLayout primary width
constexpr int kDetailMinWidth = 108;
constexpr int kHeadersButtonWidth = 160;

std::string text(const wchar_t* value) { return utf8(value); }

GtkWidget* label(const std::string& value, const char* cssClass = style::kLabel, bool wrap = true) {
    GtkWidget* widget = gtk_label_new(value.c_str());
    gtk_label_set_xalign(GTK_LABEL(widget), 0.0f);
    if (wrap) {
        gtk_label_set_line_wrap(GTK_LABEL(widget), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(widget), PANGO_WRAP_WORD_CHAR);
        // Wrapping labels would otherwise request their full single-line width.
        gtk_label_set_max_width_chars(GTK_LABEL(widget), 1);
        gtk_widget_set_hexpand(widget, TRUE);
    }
    if (cssClass) style::addClass(widget, cssClass);
    return widget;
}
void setLabel(GtkWidget* widget, const std::string& value) {
    if (widget && value != gtk_label_get_text(GTK_LABEL(widget))) gtk_label_set_text(GTK_LABEL(widget), value.c_str());
}
GtkWidget* separator() {
    GtkWidget* line = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    style::addClass(line, style::kSeparator);
    gtk_widget_set_margin_top(line, 4);
    gtk_widget_set_margin_bottom(line, 4);
    return line;
}
GtkWidget* card() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, ui::fieldGap);
    style::addClass(box, style::kCard);
    return box;
}
GtkWidget* panel(const char* kind) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, ui::fieldGap);
    style::addClass(box, style::kPanel);
    style::addClass(box, kind);
    return box;
}
GtkWidget* page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, ui::relatedGap);
    return box;
}
// Visibility of a dynamic widget is owned by render(), never by show_all.
void dynamic(GtkWidget* widget) {
    gtk_widget_show_all(widget);
    gtk_widget_set_no_show_all(widget, TRUE);
    gtk_widget_hide(widget);
}
GtkWidget* button(const char* title, const char* cssClass) {
    GtkWidget* widget = gtk_button_new_with_label(title);
    style::addClass(widget, cssClass);
    return widget;
}
void setButtonLabel(GtkWidget* widget, const std::string& value) {
    if (value != gtk_button_get_label(GTK_BUTTON(widget))) gtk_button_set_label(GTK_BUTTON(widget), value.c_str());
}
void setEntry(GtkWidget* entry, const std::string& value) {
    // Only a real change touches the entry, so polling keeps selection and cursor.
    if (entry && value != gtk_entry_get_text(GTK_ENTRY(entry))) gtk_entry_set_text(GTK_ENTRY(entry), value.c_str());
}
void setSwitch(GtkWidget* toggle, bool value) {
    if (toggle && static_cast<bool>(gtk_switch_get_active(GTK_SWITCH(toggle))) != value)
        gtk_switch_set_active(GTK_SWITCH(toggle), value);
}
const char* markerClass(ui::Color color) {
    switch (color) {
        case ui::Color::primary: return wizardStyle::kMarkerPrimary;
        case ui::Color::success: return wizardStyle::kMarkerSuccess;
        case ui::Color::error: return wizardStyle::kMarkerError;
        default: break;
    }
    return wizardStyle::kMarkerPending;
}
const char* textClass(ui::Color color) {
    switch (color) {
        case ui::Color::primary: return wizardStyle::kTextPrimary;
        case ui::Color::success: return wizardStyle::kTextSuccess;
        case ui::Color::error: return wizardStyle::kTextError;
        default: break;
    }
    return wizardStyle::kTextPending;
}
void setColorClass(GtkWidget* widget, const char* wanted, std::initializer_list<const char*> all) {
    for (const char* name : all) style::setClass(widget, name, name == wanted);
}
DialogIcon dialogIcon(WizardNotice notice) {
    switch (notice) {
        case WizardNotice::warning: return DialogIcon::warning;
        case WizardNotice::error: return DialogIcon::error;
        case WizardNotice::information: break;
    }
    return DialogIcon::information;
}
const char* pageName(WizardStep step) {
    switch (step) {
        case WizardStep::serverUrl: return "url";
        case WizardStep::serverSecret: return "secret";
        case WizardStep::identity: return "identity";
        case WizardStep::contracts: return "contracts";
        default: break;
    }
    return "message";
}
}  // namespace

WizardWindow::WizardWindow(IpcEndpoint endpoint, Dispatcher dispatcher)
    : controller_(std::move(endpoint), std::move(dispatcher), *this) {}

WizardWindow::~WizardWindow() {
    // Windows destructor: one best-effort wizard-close (1 s) while a session is open.
    controller_.closeOnExit();
    if (window_) destroyWindow();
}

// --- construction ------------------------------------------------------------------
bool WizardWindow::createWindow() {
    if (window_) return true;
    build();
    if (!window_) return false;
    timer_ = g_timeout_add(kWizardTickMs, [](gpointer data) -> gboolean {
        static_cast<WizardWindow*>(data)->controller_.tick();
        return G_SOURCE_CONTINUE;
    }, this);
    rendered_ = false;
    return true;
}

void WizardWindow::presentWindow() {
    if (headers_.window()) { gtk_window_present(headers_.window()); return; }
    if (window_) gtk_window_present(GTK_WINDOW(window_));
}

void WizardWindow::build() {
    wizardStyle::install();
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    style::prepareWindow(GTK_WINDOW(window_), "Configuration Wizard", ui::wizardClient);
    gtk_widget_set_size_request(window_, kWizardMinimum.width, kWizardMinimum.height);
    // X and Escape cancel the Wizard's own work (never Disconnect or a commit).
    g_signal_connect(window_, "delete-event", gtkCallback([](GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
        auto* self = static_cast<WizardWindow*>(data);
        self->deferred_.post([self] { self->controller_.cancel(false); });
        return TRUE;
    }), this);
    g_signal_connect(window_, "key-press-event", gtkCallback([](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
        return static_cast<WizardWindow*>(data)->onKey(event) ? TRUE : FALSE;
    }), this);
    g_signal_connect(window_, "destroy", gtkCallback([](GtkWidget*, gpointer data) {
        static_cast<WizardWindow*>(data)->onDestroyed();
    }), this);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    body_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    // The content width (without client-side decorations) selects the compact layout.
    g_signal_connect(body_, "size-allocate", gtkCallback([](GtkWidget*, GdkRectangle* allocation, gpointer data) {
        auto* self = static_cast<WizardWindow*>(data);
        if ((allocation->width < kCompactWidth) != self->compact_) self->schedulePlacement();
    }), this);
    gtk_box_pack_start(GTK_BOX(body_), buildSidebar(), FALSE, FALSE, 0);
    divider_ = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    style::addClass(divider_, style::kSeparator);
    gtk_box_pack_start(GTK_BOX(body_), divider_, FALSE, FALSE, 0);

    scroller_ = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller_), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, ui::relatedGap);
    gtk_container_set_border_width(GTK_CONTAINER(content), ui::sectionGap);
    stack_ = gtk_stack_new();
    gtk_stack_set_vhomogeneous(GTK_STACK(stack_), FALSE);
    gtk_stack_set_transition_type(GTK_STACK(stack_), GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_stack_add_named(GTK_STACK(stack_), buildMessagePage(), "message");
    gtk_stack_add_named(GTK_STACK(stack_), buildUrlPage(), "url");
    gtk_stack_add_named(GTK_STACK(stack_), buildSecretPage(), "secret");
    gtk_stack_add_named(GTK_STACK(stack_), buildIdentityPage(), "identity");
    gtk_stack_add_named(GTK_STACK(stack_), buildContractsPage(), "contracts");
    gtk_box_pack_start(GTK_BOX(content), stack_, FALSE, FALSE, 0);
    status_ = label("", style::kLabel);
    style::addClass(status_, wizardStyle::kTextPending);
    gtk_box_pack_start(GTK_BOX(content), status_, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(scroller_), content);
    // Keyboard focus scrolls the focused field into view (Windows revealFocus).
    gtk_container_set_focus_vadjustment(GTK_CONTAINER(content),
                                        gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller_)));
    gtk_widget_set_hexpand(scroller_, TRUE);
    gtk_widget_set_vexpand(scroller_, TRUE);
    gtk_box_pack_start(GTK_BOX(body_), scroller_, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), body_, TRUE, TRUE, 0);
    GtkWidget* rule = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    style::addClass(rule, style::kSeparator);
    gtk_box_pack_start(GTK_BOX(root), rule, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), buildFooter(), FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window_), root);
    gtk_widget_show_all(root);
    dynamic(status_);
    compact_ = false;
}

GtkWidget* WizardWindow::buildSidebar() {
    sidebar_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    style::addClass(sidebar_, wizardStyle::kSidebar);
    gtk_widget_set_size_request(sidebar_, kSidebarWidth, -1);
    // The step texts expand horizontally; without an explicit FALSE that would
    // propagate to the sidebar and split the width with the form (Windows keeps
    // the step panel at its fixed width and gives the rest to the form).
    gtk_widget_set_hexpand(sidebar_, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(sidebar_), 12);
    sidebarHeading_ = label(text(wizardText::configure), style::kLabel);
    style::addClass(sidebarHeading_, wizardStyle::kTextPending);
    gtk_widget_set_margin_bottom(sidebarHeading_, 24);
    gtk_box_pack_start(GTK_BOX(sidebar_), sidebarHeading_, FALSE, FALSE, 0);
    for (std::size_t i = 0; i < 4; ++i) {
        stepRows_[i] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ui::fieldGap);
        gtk_container_set_border_width(GTK_CONTAINER(stepRows_[i]), 8);
        gtk_widget_set_size_request(stepRows_[i], -1, 80);
        stepMarkers_[i] = gtk_label_new(std::to_string(i + 1).c_str());
        style::addClass(stepMarkers_[i], wizardStyle::kMarker);
        gtk_widget_set_valign(stepMarkers_[i], GTK_ALIGN_START);
        gtk_widget_set_halign(stepMarkers_[i], GTK_ALIGN_CENTER);
        stepTexts_[i] = label("", style::kLabel);
        gtk_widget_set_valign(stepTexts_[i], GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(stepRows_[i]), stepMarkers_[i], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(stepRows_[i]), stepTexts_[i], TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(sidebar_), stepRows_[i], FALSE, FALSE, 0);
    }
    // The sidebar is informative only; it is not a navigation control.
    atk_object_set_name(gtk_widget_get_accessible(sidebar_), "Wizard steps");
    return sidebar_;
}

GtkWidget* WizardWindow::buildFooter() {
    // [Reset] [Back, Error detail, primary]: one row while they fit, otherwise two
    // rows (Windows computeButtonFooterLayout); see placeFooter.
    footer_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ui::fieldGap);
    style::addClass(footer_, wizardStyle::kFooter);
    gtk_container_set_border_width(GTK_CONTAINER(footer_), ui::compactMargin);
    footerTop_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ui::fieldGap);
    footerRow_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ui::fieldGap);
    reset_ = button("Reset", style::kErrorButton);
    back_ = button("Back", style::kSecondaryButton);
    detailButton_ = button("Error detail", style::kErrorButton);
    next_ = button(utf8(wizardText::verify).c_str(), style::kPrimaryButton);
    gtk_widget_set_size_request(next_, kPrimaryMinWidth, -1);
    gtk_widget_set_size_request(detailButton_, kDetailMinWidth, -1);
    gtk_box_pack_start(GTK_BOX(footerTop_), reset_, FALSE, FALSE, 0);
    // Reading and keyboard order: Reset, Back, Error detail, primary.
    gtk_box_pack_end(GTK_BOX(footerRow_), next_, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(footerRow_), detailButton_, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(footerRow_), back_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(footer_), footerTop_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(footer_), footerRow_, TRUE, TRUE, 0);
    g_signal_connect(footer_, "size-allocate", gtkCallback([](GtkWidget*, GdkRectangle* allocation, gpointer data) {
        auto* self = static_cast<WizardWindow*>(data);
        if (self->footerWraps(allocation->width) != self->footerWrapped_) self->schedulePlacement();
    }), this);
    g_signal_connect(reset_, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        auto* self = static_cast<WizardWindow*>(data);
        self->deferred_.post([self] { self->controller_.cancel(true); });
    }), this);
    g_signal_connect(back_, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        static_cast<WizardWindow*>(data)->controller_.back();
    }), this);
    g_signal_connect(detailButton_, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        static_cast<WizardWindow*>(data)->openErrorDetail();
    }), this);
    g_signal_connect(next_, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        auto* self = static_cast<WizardWindow*>(data);
        self->deferred_.post([self] { self->controller_.next(); });
    }), this);
    gtk_widget_show_all(footer_);
    gtk_widget_set_no_show_all(back_, TRUE);
    gtk_widget_set_no_show_all(detailButton_, TRUE);
    gtk_widget_hide(back_);
    gtk_widget_hide(detailButton_);
    return footer_;
}

WizardWindow::ErrorPanel WizardWindow::errorPanel() {
    ErrorPanel result;
    result.card = panel(style::kPanelError);
    gtk_box_pack_start(GTK_BOX(result.card), label("Could not complete this action", style::kCardTitle), FALSE, FALSE, 0);
    result.text = label("", style::kLabel);
    gtk_label_set_selectable(GTK_LABEL(result.text), TRUE);
    gtk_box_pack_start(GTK_BOX(result.card), result.text, FALSE, FALSE, 0);
    return result;
}

GtkWidget* WizardWindow::entry(WizardField field, const char* accessibleName) {
    GtkWidget* widget = gtk_entry_new();
    gtk_widget_set_hexpand(widget, TRUE);
    atk_object_set_name(gtk_widget_get_accessible(widget), accessibleName);
    g_object_set_data(G_OBJECT(widget), "phpvpn-field", GINT_TO_POINTER(static_cast<int>(field)));
    g_signal_connect(widget, "changed", gtkCallback([](GtkEditable* editable, gpointer data) {
        auto* self = static_cast<WizardWindow*>(data);
        if (self->updating_ || !self->window_) return;
        const auto field = static_cast<WizardField>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(editable), "phpvpn-field")));
        std::string value = gtk_entry_get_text(GTK_ENTRY(editable));
        switch (field) {
            case WizardField::url: self->controller_.editUrl(std::move(value)); break;
            case WizardField::secret: self->controller_.editSecret(std::move(value)); break;
            case WizardField::ip: self->controller_.editIp(std::move(value)); break;
            case WizardField::key: self->controller_.editKey(std::move(value)); break;
            default: break;
        }
    }), this);
    return widget;
}

GtkWidget* WizardWindow::toggle(WizardField field, const char* title, GtkWidget** row) {
    // A visual switch with the checkbox semantics of Windows: the label is part of
    // the click target, Space toggles, the accessible name is the label.
    *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ui::relatedGap);
    gtk_widget_set_size_request(*row, -1, 32);
    GtkWidget* widget = gtk_switch_new();
    gtk_widget_set_valign(widget, GTK_ALIGN_CENTER);
    atk_object_set_name(gtk_widget_get_accessible(widget), title);
    g_object_set_data(G_OBJECT(widget), "phpvpn-field", GINT_TO_POINTER(static_cast<int>(field)));
    GtkWidget* caption = label(title, style::kLabel);
    gtk_widget_set_valign(caption, GTK_ALIGN_CENTER);
    GtkWidget* target = gtk_event_box_new();
    gtk_widget_add_events(target, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    gtk_container_add(GTK_CONTAINER(target), caption);
    g_object_set_data(G_OBJECT(target), "phpvpn-switch", widget);
    g_signal_connect(target, "button-release-event", gtkCallback([](GtkWidget* box, GdkEventButton* event, gpointer) -> gboolean {
        auto* toggle = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(box), "phpvpn-switch"));
        if (event->button != 1 || !gtk_widget_is_sensitive(toggle)) return FALSE;
        gtk_widget_grab_focus(toggle);
        gtk_switch_set_active(GTK_SWITCH(toggle), !gtk_switch_get_active(GTK_SWITCH(toggle)));
        return TRUE;
    }), nullptr);
    AtkObject* accessible = gtk_widget_get_accessible(widget);
    atk_object_add_relationship(accessible, ATK_RELATION_LABELLED_BY, gtk_widget_get_accessible(caption));
    g_signal_connect(widget, "notify::active", gtkCallback([](GObject* object, GParamSpec*, gpointer data) {
        auto* self = static_cast<WizardWindow*>(data);
        if (self->updating_ || !self->window_) return;
        const bool active = gtk_switch_get_active(GTK_SWITCH(object));
        switch (static_cast<WizardField>(GPOINTER_TO_INT(g_object_get_data(object, "phpvpn-field")))) {
            case WizardField::defaultSecret: self->controller_.setDefaultSecret(active); break;
            case WizardField::automaticIp: self->controller_.setAutoDetectIp(active); break;
            case WizardField::defaultKey: self->controller_.setDefaultKey(active); break;
            case WizardField::contracts: self->controller_.setCreateContracts(active); break;
            default: break;
        }
        // A refused change (locked) restores the model value.
        if (self->window_) self->render();
    }), this);
    gtk_box_pack_start(GTK_BOX(*row), widget, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(*row), target, TRUE, TRUE, 0);
    return widget;
}

GtkWidget* WizardWindow::buildMessagePage() {
    GtkWidget* box = page();
    messageEyebrow_ = label("", style::kEyebrow);
    messageHeading_ = label("", style::kHeading);
    messageText_ = label("", style::kLabel);
    messageSecond_ = label("", style::kLabel);
    gateCard_ = panel(style::kPanelWarning);
    gtk_box_pack_start(GTK_BOX(gateCard_), label(text(wizardText::gateWarning), style::kLabel), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(gateCard_), label(text(wizardText::gateImpact), style::kLabel), FALSE, FALSE, 0);
    offline_ = label(text(wizardText::offline), style::kLabel);
    messageError_ = errorPanel();
    for (GtkWidget* child : {messageEyebrow_, messageHeading_, messageText_, messageSecond_, gateCard_, offline_, messageError_.card})
        gtk_box_pack_start(GTK_BOX(box), child, FALSE, FALSE, 0);
    gtk_widget_show_all(box);
    for (GtkWidget* child : {messageEyebrow_, messageSecond_, gateCard_, offline_, messageError_.card}) dynamic(child);
    return box;
}

GtkWidget* WizardWindow::buildUrlPage() {
    GtkWidget* box = page();
    gtk_box_pack_start(GTK_BOX(box), label(text(wizardText::urlHeading), style::kHeading), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), label(text(wizardText::urlDescription), style::kLabel), FALSE, FALSE, 0);
    GtkWidget* content = card();
    gtk_box_pack_start(GTK_BOX(content), label(text(wizardText::urlLabel), style::kLabel), FALSE, FALSE, 0);
    url_ = entry(WizardField::url, "Server URL");
    gtk_box_pack_start(GTK_BOX(content), url_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), label(text(wizardText::urlHint), style::kHint), FALSE, FALSE, 0);
    GtkWidget* example = label(text(wizardText::urlExample), style::kHint);
    style::addClass(example, wizardStyle::kExample);
    gtk_box_pack_start(GTK_BOX(content), example, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), separator(), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), label(text(wizardText::headersHeading), style::kLabel), FALSE, FALSE, 0);
    headersButton_ = button(utf8(wizardText::headersButton).c_str(), style::kSecondaryButton);
    gtk_widget_set_halign(headersButton_, GTK_ALIGN_START);
    gtk_widget_set_size_request(headersButton_, kHeadersButtonWidth, -1);
    g_signal_connect(headersButton_, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        auto* self = static_cast<WizardWindow*>(data);
        self->deferred_.post([self] { self->openHeaders(); });
    }), this);
    gtk_box_pack_start(GTK_BOX(content), headersButton_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), label(text(wizardText::headersHint), style::kHint), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), separator(), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), label(text(wizardText::urlRules), style::kLabel), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), label(text(wizardText::urlCheck), style::kLabel), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), content, FALSE, FALSE, 0);
    urlError_ = errorPanel();
    gtk_box_pack_start(GTK_BOX(box), urlError_.card, FALSE, FALSE, 0);
    gtk_widget_show_all(box);
    dynamic(urlError_.card);
    return box;
}

GtkWidget* WizardWindow::buildSecretPage() {
    GtkWidget* box = page();
    gtk_box_pack_start(GTK_BOX(box), label(text(wizardText::secretHeading), style::kHeading), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), label(text(wizardText::secretDescription), style::kLabel), FALSE, FALSE, 0);
    GtkWidget* content = card();
    secretLabel_ = label(text(wizardText::secretLabel), style::kLabel);
    secret_ = entry(WizardField::secret, "Server secret");
    secretHint_ = label(text(wizardText::secretHint), style::kHint);
    GtkWidget* row = nullptr;
    defaultSecret_ = toggle(WizardField::defaultSecret, utf8(wizardText::defaultSecret).c_str(), &row);
    for (GtkWidget* child : {secretLabel_, secret_, secretHint_, row, separator(), label(text(wizardText::secretCheck))})
        gtk_box_pack_start(GTK_BOX(content), child, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), content, FALSE, FALSE, 0);
    secretError_ = errorPanel();
    gtk_box_pack_start(GTK_BOX(box), secretError_.card, FALSE, FALSE, 0);
    defaultSecretWarning_ = panel(style::kPanelWarning);
    gtk_box_pack_start(GTK_BOX(defaultSecretWarning_), label(text(wizardText::defaultSecretWarningHeading), style::kCardTitle), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(defaultSecretWarning_), label(text(wizardText::defaultSecretWarning), style::kLabel), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), defaultSecretWarning_, FALSE, FALSE, 0);
    gtk_widget_show_all(box);
    dynamic(secretError_.card);
    dynamic(defaultSecretWarning_);
    return box;
}

GtkWidget* WizardWindow::buildIdentityPage() {
    GtkWidget* box = page();
    gtk_box_pack_start(GTK_BOX(box), label(text(wizardText::identityHeading), style::kHeading), FALSE, FALSE, 0);
    identityDescription_ = label("", style::kLabel);
    gtk_box_pack_start(GTK_BOX(box), identityDescription_, FALSE, FALSE, 0);
    GtkWidget* content = card();
    ip_ = entry(WizardField::ip, "Virtual IP address");
    GtkWidget* ipRow = nullptr;
    automaticIp_ = toggle(WizardField::automaticIp, utf8(wizardText::automaticIp).c_str(), &ipRow);
    noSuggestion_ = label(text(wizardText::noSuggestion), style::kLabel);
    key_ = entry(WizardField::key, "Access key");
    GtkWidget* keyRow = nullptr;
    defaultKey_ = toggle(WizardField::defaultKey, utf8(wizardText::defaultKey).c_str(), &keyRow);
    for (GtkWidget* child : {label(text(wizardText::ipLabel)), ip_, ipRow, noSuggestion_, separator(),
                             label(text(wizardText::keyLabel)), key_, keyRow, label(text(wizardText::keyHint), style::kHint)})
        gtk_box_pack_start(GTK_BOX(content), child, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), content, FALSE, FALSE, 0);
    identityError_ = errorPanel();
    gtk_box_pack_start(GTK_BOX(box), identityError_.card, FALSE, FALSE, 0);
    defaultKeyWarning_ = panel(style::kPanelWarning);
    gtk_box_pack_start(GTK_BOX(defaultKeyWarning_), label(text(wizardText::defaultKeyWarningHeading), style::kCardTitle), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(defaultKeyWarning_), label(text(wizardText::defaultKeyWarning), style::kLabel), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), defaultKeyWarning_, FALSE, FALSE, 0);
    gtk_widget_show_all(box);
    dynamic(noSuggestion_);
    dynamic(identityError_.card);
    dynamic(defaultKeyWarning_);
    return box;
}

GtkWidget* WizardWindow::buildContractsPage() {
    GtkWidget* box = page();
    gtk_box_pack_start(GTK_BOX(box), label(text(wizardText::contractsHeading), style::kHeading), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), label(text(wizardText::contractsDescription), style::kLabel), FALSE, FALSE, 0);
    GtkWidget* content = card();
    GtkWidget* row = nullptr;
    contracts_ = toggle(WizardField::contracts, utf8(wizardText::addContracts).c_str(), &row);
    gtk_box_pack_start(GTK_BOX(content), row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), separator(), FALSE, FALSE, 0);
    // Three columns; they move below each other on a narrow window (UX09 step 4).
    GtkWidget* columns = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(columns), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(columns), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(columns), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(columns), 3);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(columns), ui::relatedGap);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(columns), ui::relatedGap);
    const char* headings[]{"Existing contracts:", "Missing defaults:", "To add on Finish:"};
    for (std::size_t i = 0; i < 3; ++i) {
        GtkWidget* column = gtk_box_new(GTK_ORIENTATION_VERTICAL, ui::fieldGap);
        gtk_box_pack_start(GTK_BOX(column), label(headings[i], style::kLabel, false), FALSE, FALSE, 0);
        contractCounts_[i] = label("0", wizardStyle::kCounter, false);
        gtk_box_pack_start(GTK_BOX(column), contractCounts_[i], FALSE, FALSE, 0);
        gtk_flow_box_insert(GTK_FLOW_BOX(columns), column, -1);
        GtkWidget* child = gtk_widget_get_parent(column);
        if (child) gtk_widget_set_can_focus(child, FALSE);
    }
    gtk_box_pack_start(GTK_BOX(content), columns, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), content, FALSE, FALSE, 0);
    contractsUnsupported_ = label(text(wizardText::contractsUnsupported), style::kLabel);
    gtk_box_pack_start(GTK_BOX(box), contractsUnsupported_, FALSE, FALSE, 0);
    contractsHint_ = panel(style::kPanelWarning);
    gtk_box_pack_start(GTK_BOX(contractsHint_), label(text(wizardText::contractsHint), style::kLabel), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), contractsHint_, FALSE, FALSE, 0);
    contractsError_ = errorPanel();
    gtk_box_pack_start(GTK_BOX(box), contractsError_.card, FALSE, FALSE, 0);
    gtk_widget_show_all(box);
    dynamic(contractsUnsupported_);
    dynamic(contractsHint_);
    dynamic(contractsError_.card);
    return box;
}

// --- rendering ---------------------------------------------------------------------
void WizardWindow::updateErrorPanel(const ErrorPanel& errorPanel, const WizardPresentation& p) {
    gtk_widget_set_visible(errorPanel.card, p.errorText.has_value());
    setLabel(errorPanel.text, p.errorText.value_or(std::string{}));
}

void WizardWindow::renderSidebar(const WizardPresentation& p) {
    const std::initializer_list<const char*> markers{wizardStyle::kMarkerPrimary, wizardStyle::kMarkerSuccess,
                                                     wizardStyle::kMarkerError, wizardStyle::kMarkerPending};
    const std::initializer_list<const char*> texts{wizardStyle::kTextPrimary, wizardStyle::kTextSuccess,
                                                   wizardStyle::kTextError, wizardStyle::kTextPending};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& item = p.sidebar[i];
        auto color = item.color;
        std::string symbol = item.symbol;
        if (compact_) {
            // Windows compact strip: numbers, a check mark only for other completed steps.
            if (item.active) color = ui::Color::primary;
            symbol = !item.active && item.status == "Completed" ? "✓" : std::to_string(i + 1);
        }
        setLabel(stepMarkers_[i], symbol);
        setColorClass(stepMarkers_[i], markerClass(color), markers);
        setLabel(stepTexts_[i], item.status.empty() ? item.name : item.name + "\n" + item.status);
        setColorClass(stepTexts_[i], textClass(color), texts);
        style::setClass(stepTexts_[i], style::kStrongValue, item.active);
        style::setClass(stepRows_[i], wizardStyle::kStepActive, item.active && !compact_);
        atk_object_set_name(gtk_widget_get_accessible(stepRows_[i]),
                            (std::to_string(i + 1) + " " + item.name + (item.status.empty() ? "" : ", " + item.status)).c_str());
    }
    atk_object_set_description(gtk_widget_get_accessible(sidebar_), p.sidebarText.c_str());
}

void WizardWindow::render() {
    if (!window_) return;
    const auto p = controller_.presentation();
    const bool stepChanged = !rendered_ || renderedStep_ != p.step;
    renderedStep_ = p.step;
    rendered_ = true;
    if (p.step != WizardStep::serverUrl) headers_.close(false);
    updating_ = true;
    renderSidebar(p);
    gtk_stack_set_visible_child_name(GTK_STACK(stack_), pageName(p.step));

    // Message page.
    setLabel(messageEyebrow_, p.eyebrow);
    gtk_widget_set_visible(messageEyebrow_, !p.eyebrow.empty());
    setLabel(messageHeading_, p.heading);
    setLabel(messageText_, p.text);
    setLabel(messageSecond_, p.secondText);
    gtk_widget_set_visible(messageSecond_, !p.secondText.empty());
    gtk_widget_set_visible(gateCard_, p.gateCard);
    gtk_widget_set_visible(offline_, p.offline && p.step == WizardStep::disconnectGate);
    for (const auto* errorPanel : {&messageError_, &urlError_, &secretError_, &identityError_, &contractsError_})
        updateErrorPanel(*errorPanel, p);

    // Fields keep their widget, text and selection; only real changes are written.
    setEntry(url_, p.url);
    setEntry(secret_, p.secret);
    setEntry(ip_, p.ip);
    setEntry(key_, p.key);
    setSwitch(defaultSecret_, p.useDefaultSecret);
    setSwitch(automaticIp_, p.autoDetectIp);
    setSwitch(defaultKey_, p.useDefaultKey);
    setSwitch(contracts_, p.createContracts);
    gtk_widget_set_sensitive(url_, !p.locked);
    gtk_widget_set_sensitive(headersButton_, !p.locked);
    gtk_widget_set_sensitive(secret_, p.secretEnabled);
    gtk_widget_set_sensitive(defaultSecret_, !p.locked);
    gtk_widget_set_sensitive(ip_, p.ipEnabled);
    gtk_widget_set_sensitive(automaticIp_, !p.locked);
    gtk_widget_set_sensitive(key_, p.keyEnabled);
    gtk_widget_set_sensitive(defaultKey_, !p.locked);
    gtk_widget_set_sensitive(contracts_, p.contractsEnabled);
    for (const auto& [field, widget] : {std::pair{WizardField::url, url_}, std::pair{WizardField::secret, secret_},
                                        std::pair{WizardField::ip, ip_}, std::pair{WizardField::key, key_}}) {
        const bool invalid = p.invalidField == field;
        style::setClass(widget, wizardStyle::kInvalid, invalid);
        atk_object_set_description(gtk_widget_get_accessible(widget),
                                   invalid && p.errorText ? ("Invalid. " + *p.errorText).c_str() : "");
    }
    setLabel(secretLabel_, p.secretLabel);
    setLabel(secretHint_, p.secretHint);
    gtk_widget_set_visible(defaultSecretWarning_, p.defaultSecretWarning);
    setLabel(identityDescription_, p.identityDescription);
    gtk_widget_set_visible(noSuggestion_, p.noSuggestion);
    gtk_widget_set_visible(defaultKeyWarning_, p.defaultKeyWarning);
    for (std::size_t i = 0; i < 3; ++i) setLabel(contractCounts_[i], std::to_string(p.contractCounts[i]));
    gtk_widget_set_visible(contractsUnsupported_, p.contractsUnsupported);
    gtk_widget_set_visible(contractsHint_, p.contractsHint);
    setLabel(status_, p.statusText);
    gtk_widget_set_visible(status_, p.locked);

    // Footer.
    setButtonLabel(next_, p.primaryLabel);
    gtk_widget_set_sensitive(next_, p.primaryEnabled);
    gtk_widget_set_visible(back_, p.backVisible);
    gtk_widget_set_sensitive(back_, p.backEnabled);
    gtk_widget_set_visible(detailButton_, p.detailVisible);
    gtk_widget_set_sensitive(detailButton_, p.detailEnabled);
    gtk_widget_set_sensitive(reset_, p.resetEnabled);
    // Before GTK grows the window for a new button, the footer may wrap instead.
    placeFooter();
    updating_ = false;

    if (stepChanged)
        gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller_)), 0);
    // Focus: preserved in the owned windows and on an unchanged step, otherwise the
    // first enabled field of the step (Windows GetNextDlgTabItem, then primary).
    if (ownedWindowActive()) return;
    GtkWidget* focus = gtk_window_get_focus(GTK_WINDOW(window_));
    const bool usable = focus && gtk_widget_is_sensitive(focus) && gtk_widget_is_visible(focus);
    if (!stepChanged && usable) return;
    if (GtkWidget* first = firstFocusable()) gtk_widget_grab_focus(first);
}

bool WizardWindow::ownedWindowActive() const {
    return headers_.window() || detail_.active();
}

GtkWidget* WizardWindow::firstFocusable() const {
    std::vector<GtkWidget*> order;
    switch (renderedStep_) {
        case WizardStep::serverUrl: order = {url_, headersButton_}; break;
        case WizardStep::serverSecret: order = {secret_, defaultSecret_}; break;
        case WizardStep::identity: order = {ip_, automaticIp_, key_, defaultKey_}; break;
        case WizardStep::contracts: order = {contracts_}; break;
        // tray_UX.md UX09: an accidental Enter must not confirm the global
        // Disconnect, so the gate starts on the non-destructive Reset.
        case WizardStep::disconnectGate: order = {reset_}; break;
        default: break;
    }
    order.push_back(next_);
    order.push_back(reset_);
    for (GtkWidget* widget : order)
        if (widget && gtk_widget_is_sensitive(widget) && gtk_widget_get_visible(widget)) return widget;
    return nullptr;
}

void WizardWindow::refreshDiagnostic() {
    if (!window_) return;
    const bool visible = controller_.model().httpDiagnostic().has_value();
    gtk_widget_set_visible(detailButton_, visible);
    placeFooter();
}

GtkWidget* WizardWindow::widgetFor(WizardField field) const {
    switch (field) {
        case WizardField::url: return url_;
        case WizardField::secret: return secret_;
        case WizardField::defaultSecret: return defaultSecret_;
        case WizardField::ip: return ip_;
        case WizardField::automaticIp: return automaticIp_;
        case WizardField::key: return key_;
        case WizardField::defaultKey: return defaultKey_;
        case WizardField::contracts: return contracts_;
        case WizardField::headers: return headersButton_;
        case WizardField::none: break;
    }
    return nullptr;
}

void WizardWindow::setFieldText(WizardField field, const std::string& value) {
    GtkWidget* widget = widgetFor(field);
    if (!window_ || !widget || !GTK_IS_ENTRY(widget)) return;
    updating_ = true;
    setEntry(widget, value);
    updating_ = false;
}

void WizardWindow::focusField(WizardField field, WizardField fallback) {
    if (!window_) return;
    GtkWidget* widget = widgetFor(field);
    if (!widget || !gtk_widget_is_sensitive(widget)) widget = widgetFor(fallback);
    if (widget && gtk_widget_is_sensitive(widget)) gtk_widget_grab_focus(widget);
}

void WizardWindow::notify(std::string_view message, std::string_view title, WizardNotice icon) {
    showMessage(window(), message, title, dialogIcon(icon));
}

bool WizardWindow::confirm(std::string_view question, std::string_view title) {
    return tray::confirm(window(), question, title, false);
}

// --- owned windows -------------------------------------------------------------------
void WizardWindow::openHeaders() {
    if (!window_ || !controller_.acceptsInput() || controller_.presentation().locked) return;
    const bool opened = headers_.open(GTK_WINDOW(window_), controller_.model().fields().httpHeaders,
        [this](std::string value) { return controller_.acceptHttpHeaders(std::move(value)); },
        [this] { if (window_ && headersButton_) gtk_widget_grab_focus(headersButton_); });
    if (!opened) showError(GTK_WINDOW(window_), "The HTTP headers editor could not be opened.", "Configuration Wizard");
}

void WizardWindow::openErrorDetail() {
    if (!window_) return;
    if (const auto& diagnostic = controller_.model().httpDiagnostic())
        detail_.open(GTK_WINDOW(window_), *diagnostic);
}

// --- keyboard, layout and teardown ---------------------------------------------------------
bool WizardWindow::onKey(GdkEventKey* event) {
    if (event->keyval == GDK_KEY_Escape) { deferred_.post([this] { controller_.cancel(false); }); return true; }
    if (event->keyval != GDK_KEY_Return && event->keyval != GDK_KEY_KP_Enter && event->keyval != GDK_KEY_ISO_Enter)
        return false;
    // Enter on a footer or HTTP headers button activates that button; anywhere else
    // it runs the primary action, and only while that action is enabled.
    GtkWidget* focus = gtk_window_get_focus(GTK_WINDOW(window_));
    if (focus && GTK_IS_BUTTON(focus)) return false;
    if (next_ && gtk_widget_is_sensitive(next_) && gtk_widget_get_visible(next_)) controller_.next();
    return true;
}

void WizardWindow::placeCompact() {
    if (!window_) return;
    const int width = gtk_widget_get_allocated_width(body_);
    const bool compact = width < kCompactWidth;
    if (compact != compact_) {
        compact_ = compact;
        gtk_orientable_set_orientation(GTK_ORIENTABLE(body_), compact ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
        gtk_orientable_set_orientation(GTK_ORIENTABLE(divider_), compact ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL);
        gtk_orientable_set_orientation(GTK_ORIENTABLE(sidebar_), compact ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL);
        gtk_box_set_homogeneous(GTK_BOX(sidebar_), compact);
        gtk_widget_set_size_request(sidebar_, compact ? -1 : kSidebarWidth, compact ? 64 : -1);
        gtk_widget_set_visible(sidebarHeading_, !compact);
        for (std::size_t i = 0; i < 4; ++i) {
            gtk_widget_set_visible(stepTexts_[i], !compact);
            gtk_widget_set_size_request(stepRows_[i], -1, compact ? -1 : 80);
            gtk_widget_set_halign(stepRows_[i], compact ? GTK_ALIGN_CENTER : GTK_ALIGN_FILL);
            gtk_widget_set_valign(stepMarkers_[i], compact ? GTK_ALIGN_CENTER : GTK_ALIGN_START);
        }
    }
    render();
}

void WizardWindow::schedulePlacement() {
    if (placement_) return;
    // Changing the layout during an allocation would recurse into it; defer it.
    placement_ = g_idle_add([](gpointer raw) -> gboolean {
        auto* window = static_cast<WizardWindow*>(raw);
        window->placement_ = 0;
        window->placeCompact();
        return G_SOURCE_REMOVE;
    }, this);
}

bool WizardWindow::footerWraps(int width) const {
    int leading = 0, trailing = 0;
    gtk_widget_get_preferred_width(footerTop_, nullptr, &leading);
    gtk_widget_get_preferred_width(footerRow_, nullptr, &trailing);
    const int oneRow = 2 * ui::compactMargin + leading + ui::fieldGap + trailing;
    // A one-row footer requests its whole width as the window minimum, so the
    // window never gets narrower than oneRow while it is on one row. Reaching
    // that limit wraps the footer, which lowers the minimum again.
    return width <= oneRow;
}

void WizardWindow::placeFooter() {
    if (!footer_) return;
    const int width = gtk_widget_get_allocated_width(footer_);
    if (width <= 1) return;  // not allocated yet
    const bool wraps = footerWraps(width);
    if (wraps == footerWrapped_) return;
    footerWrapped_ = wraps;
    // Reset gets its own row only when the buttons do not fit side by side (Windows
    // computeButtonFooterLayout). Only the orientation changes; nothing is re-parented.
    gtk_orientable_set_orientation(GTK_ORIENTABLE(footer_), wraps ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
}

void WizardWindow::destroyWindow() {
    if (!window_) return;
    destroying_ = true;
    headers_.close(false);
    detail_.close();
    // Entry texts (including the server secret) leave the widgets first.
    updating_ = true;
    for (GtkWidget* field : {url_, secret_, ip_, key_}) if (field) gtk_entry_set_text(GTK_ENTRY(field), "");
    updating_ = false;
    gtk_widget_destroy(window_);
    destroying_ = false;
}

void WizardWindow::onDestroyed() {
    if (timer_) { g_source_remove(timer_); timer_ = 0; }
    if (placement_) { g_source_remove(placement_); placement_ = 0; }
    window_ = body_ = sidebar_ = sidebarHeading_ = divider_ = scroller_ = stack_ = status_ = nullptr;
    footer_ = footerTop_ = footerRow_ = reset_ = back_ = detailButton_ = next_ = nullptr;
    messageEyebrow_ = messageHeading_ = messageText_ = messageSecond_ = gateCard_ = offline_ = nullptr;
    messageError_ = urlError_ = secretError_ = identityError_ = contractsError_ = {};
    url_ = headersButton_ = secretLabel_ = secret_ = secretHint_ = defaultSecret_ = defaultSecretWarning_ = nullptr;
    identityDescription_ = ip_ = automaticIp_ = noSuggestion_ = key_ = defaultKey_ = defaultKeyWarning_ = nullptr;
    contracts_ = contractsUnsupported_ = contractsHint_ = nullptr;
    stepRows_ = {}; stepMarkers_ = {}; stepTexts_ = {}; contractCounts_ = {};
    rendered_ = false;
    compact_ = footerWrapped_ = false;
    if (!destroying_) controller_.windowLost();
}

// --- application wiring ------------------------------------------------------------------
namespace {
struct WizardWindows {
    WizardWindows(const IpcEndpoint& endpoint, const Dispatcher& dispatcher) : wizard(endpoint, dispatcher) {}
    WizardWindow wizard;
    // The Connect progress window's own HTTP error detail (Windows ProgressWindow::errorDetail_).
    HttpErrorDetailWindow connectDetail;
};
std::unique_ptr<WizardWindows>& installedWindows() {
    static std::unique_ptr<WizardWindows> windows;
    return windows;
}
}  // namespace

void installWizardWindows(TrayApp& app) {
    auto& windows = installedWindows();
    windows = std::make_unique<WizardWindows>(app.endpoint(), app.dispatcher());
    auto* installed = windows.get();
    app.hooks().openWizard = [installed] { return installed->wizard.open(); };
    app.hooks().openHttpErrorDetail = [installed](GtkWindow* parent, const HttpDiagnostic& diagnostic) {
        installed->connectDetail.open(parent, diagnostic);
    };
    app.hooks().closeHttpErrorDetail = [installed] { installed->connectDetail.close(); };
    // First participant: its approvedDiscard veto is checked before any other window.
    app.registerParticipant({&installed->wizard.controller(), [installed] { return installed->wizard.window(); }});
}
}  // namespace phpvpn::tray
