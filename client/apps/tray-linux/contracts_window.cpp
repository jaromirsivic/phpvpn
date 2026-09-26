#include "contracts_window.h"
#include "gtk_util.h"

#include <algorithm>

#include "core/base/secure_buffer.h"
#include "core/presentation/table_pagination.h"
#include "dialogs.h"
#include "editor_window.h"
#include "style.h"
#include "tray_app.h"
#include "ui_style.h"

namespace phpvpn::tray {
namespace {
// Column geometry of the Win32 table in DIP (tray_UX.md UX10): Contract 250,
// Enabled 80, Key flexible (at least 260), and 12 DIP cell padding.
// keyWidth = max(260, viewport - 446) and the row width 446 + keyWidth as on
// Windows, so the Actions column (Change 104) gets the remaining 116 DIP and a
// window wide enough for the minimum key needs no horizontal scrolling.
constexpr int kContractWidth = 250, kEnabledWidth = 80, kFixedWidth = 446;
constexpr int kActionsWidth = kFixedWidth - kContractWidth - kEnabledWidth;
constexpr int kMinKeyWidth = 260, kCellPadding = 12, kChangeWidth = 104;
// The Windows default client (ui::contractsClient, 1180 DIP) is wider than a
// 1024 x 768 screen. The GTK window starts at the Configuration Wizard size, which
// still shows the whole 810 DIP table; narrower windows scroll the table and the
// pager inside the window.
constexpr Size kDefaultClient = ui::wizardClient;
// Title bar allowance when the default size is fitted to a small work area.
constexpr int kTitleBarAllowance = 56;

Size defaultClientSize() {
    Size size = kDefaultClient;
    GdkDisplay* display = gdk_display_get_default();
    GdkMonitor* monitor = display ? gdk_display_get_primary_monitor(display) : nullptr;
    if (!monitor && display && gdk_display_get_n_monitors(display) > 0) monitor = gdk_display_get_monitor(display, 0);
    if (!monitor) return size;
    GdkRectangle area{};
    gdk_monitor_get_workarea(monitor, &area);
    if (area.width > 0) size.width = std::min(size.width, area.width);
    if (area.height > kTitleBarAllowance) size.height = std::min(size.height, area.height - kTitleBarAllowance);
    return size;
}

GtkWidget* label(std::string_view text, const char* cssClass, bool wrap = false) {
    GtkWidget* widget = gtk_label_new(std::string(text).c_str());
    gtk_label_set_xalign(GTK_LABEL(widget), 0.0f);
    if (wrap) {
        gtk_label_set_line_wrap(GTK_LABEL(widget), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(widget), PANGO_WRAP_WORD_CHAR);
    }
    if (cssClass) style::addClass(widget, cssClass);
    return widget;
}
void setAccessibleName(GtkWidget* widget, const std::string& name) {
    atk_object_set_name(gtk_widget_get_accessible(widget), name.c_str());
}
void wipeJson(JsonValue& value) {
    if (value.isString()) {
        auto& text = const_cast<std::string&>(value.stringValue());
        secureWipe(text.data(), text.size());
    } else if (value.isObject()) {
        for (auto& entry : value.objectValue()) wipeJson(entry.second);
    } else if (value.isArray()) {
        for (auto& item : const_cast<JsonArray&>(value.arrayValue())) wipeJson(item);
    }
}
}  // namespace

ContractsWindow::ContractsWindow(TrayApp& app, CryptoFactory cryptoFactory)
    : app_(app), cryptoFactory_(std::move(cryptoFactory)), writer_(app.endpoint(), app.dispatcher()),
      checker_(app.endpoint(), app.dispatcher()) {}

ContractsWindow::~ContractsWindow() {
    // Owned by the TrayApp hooks: the application has already ended the session.
    endSession();
}

void installContractsWindow(TrayApp& app) {
    auto contracts = std::make_shared<ContractsWindow>(app);
    ContractsWindow* raw = contracts.get();
    app.registerParticipant({raw, [raw] { return raw->window(); }});
    app.hooks().activateContracts = [raw] { return raw->activate(); };
    app.hooks().openContracts = [contracts](const std::string& document, const std::string& path,
                                            const std::string& revision) {
        return contracts->open(document, path, revision);
    };
    // The saved configuration applies to the next connection (UX12).
    app.addStatusListener([raw, &app](const TrayStatus&, bool) { raw->setConnectionActive(app.connectedOrConnectingNow()); });
}

bool ContractsWindow::activate() {
    if (!window_) return false;
    gtk_window_present(GTK_WINDOW(window_));
    return true;
}

int ContractsWindow::pageCount() const {
    return machine_.model() ? tablePageCount(machine_.model()->rows().size()) : 1;
}

bool ContractsWindow::open(std::string_view document, std::string path, std::string revision) {
    if (window_) { activate(); return true; }
    if (ending_ || !cryptoFactory_) return false;
    auto crypto = cryptoFactory_();
    if (!crypto) return false;
    crypto_ = std::move(crypto).value();
    if (!machine_.load(*crypto_, document, std::move(path), std::move(revision))) {
        crypto_.reset();
        return false;
    }
    page_ = 0;
    keyWidth_ = kMinKeyWidth;
    localError_.clear();
    savedSnapshot_.reset();
    connectionActive_ = app_.connectedOrConnectingNow();
    build();
    if (!createRows()) localError_ = std::string(kContractsPageFailed);
    createPager();
    refresh();
    gtk_widget_show_all(window_);
    refresh();
    gtk_window_present(GTK_WINDOW(window_));
    if (!rows_.empty()) gtk_widget_grab_focus(rows_.front().toggle);
    return true;
}

void ContractsWindow::build() {
    config_style::install();
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    style::prepareWindow(GTK_WINDOW(window_), std::string(kContractsTitle).c_str(), defaultClientSize());
    gtk_window_set_role(GTK_WINDOW(window_), "phpvpn-contracts");
    gtk_widget_set_size_request(window_, 480, 320);
    g_signal_connect(window_, "delete-event", gtkCallback([](GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
        auto* self = static_cast<ContractsWindow*>(data);
        self->deferred_.post([self] { self->close(); });
        return TRUE;
    }), this);
    g_signal_connect(window_, "key-press-event", gtkCallback([](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
        return static_cast<ContractsWindow*>(data)->onKey(event) ? TRUE : FALSE;
    }), this);
    g_signal_connect(window_, "destroy", gtkCallback([](GtkWidget*, gpointer data) {
        static_cast<ContractsWindow*>(data)->onDestroyed();
    }), this);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window_), root);
    GtkWidget* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(body, ui::sectionGap);
    gtk_widget_set_margin_start(body, ui::contentMargin);
    gtk_widget_set_margin_end(body, ui::contentMargin);
    gtk_widget_set_vexpand(body, TRUE);
    gtk_box_pack_start(GTK_BOX(root), body, TRUE, TRUE, 0);

    // Heading and description on the left, own virtual IP on the right; they
    // stack when the window is narrow (GtkFlowBox-free: the IP block wraps below).
    GtkWidget* top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ui::sectionGap);
    GtkWidget* texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, ui::spacing);
    gtk_box_pack_start(GTK_BOX(texts), label(kContractsTitle, style::kHeading), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(texts), label(kContractsDescription, style::kLabel, true), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), texts, TRUE, TRUE, 0);
    GtkWidget* identity = gtk_box_new(GTK_ORIENTATION_VERTICAL, ui::spacing);
    gtk_box_pack_start(GTK_BOX(identity), label(kContractsLocalLabel, style::kLabel), FALSE, FALSE, 0);
    local_ = label(machine_.model()->localIp().toString(), style::kStrongValue);
    gtk_label_set_selectable(GTK_LABEL(local_), TRUE);
    gtk_widget_set_can_focus(local_, FALSE);
    gtk_box_pack_start(GTK_BOX(identity), local_, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(top), identity, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(body), top, FALSE, FALSE, 0);

    notice_ = label(kContractsNextConnection, style::kLabel);
    gtk_widget_set_margin_top(notice_, ui::relatedGap);
    gtk_widget_set_no_show_all(notice_, TRUE);
    gtk_box_pack_start(GTK_BOX(body), notice_, FALSE, FALSE, 0);

    // Table: fixed header, vertically scrolled rows, both following one
    // horizontal adjustment so the header and the rows move together (UX02).
    GtkWidget* table = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    style::addClass(table, config_style::kTable);
    gtk_widget_set_margin_top(table, ui::relatedGap);
    gtk_widget_set_vexpand(table, TRUE);
    rowsScroller_ = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(rowsScroller_), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(rowsScroller_), FALSE);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(rowsScroller_), 112);
    gtk_widget_set_vexpand(rowsScroller_, TRUE);
    GtkAdjustment* horizontal = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(rowsScroller_));
    GtkWidget* headerScroller = gtk_scrolled_window_new(horizontal, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(headerScroller), GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    style::addClass(header, config_style::kTableHeader);
    const int widths[]{kContractWidth, kEnabledWidth, kMinKeyWidth, kActionsWidth};
    for (int column = 0; column < 4; ++column) {
        GtkWidget* name = label(kContractsColumns[column], nullptr);
        gtk_widget_set_margin_start(name, kCellPadding);
        gtk_widget_set_size_request(name, widths[column] - kCellPadding, ui::headerHeight);
        gtk_box_pack_start(GTK_BOX(header), name, FALSE, FALSE, 0);
        if (column == 2) headerKey_ = name;
    }
    gtk_container_add(GTK_CONTAINER(headerScroller), header);
    gtk_box_pack_start(GTK_BOX(table), headerScroller, FALSE, FALSE, 0);
    rowsBox_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    setAccessibleName(rowsBox_, "Communication contracts table");
    gtk_container_add(GTK_CONTAINER(rowsScroller_), rowsBox_);
    // Keyboard focus reveals the focused control in both directions.
    gtk_container_set_focus_vadjustment(GTK_CONTAINER(rowsBox_),
                                        gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(rowsScroller_)));
    gtk_container_set_focus_hadjustment(GTK_CONTAINER(rowsBox_), horizontal);
    GtkWidget* viewport = gtk_bin_get_child(GTK_BIN(rowsScroller_));
    g_signal_connect(viewport, "size-allocate", gtkCallback([](GtkWidget*, GdkRectangle* allocation, gpointer data) {
        auto* self = static_cast<ContractsWindow*>(data);
        const int next = std::max(kMinKeyWidth, allocation->width - kFixedWidth);
        if (next == self->keyWidth_ || self->keyWidthIdle_) return;
        // Resizing children during the allocation would recurse; defer it.
        struct Pending { ContractsWindow* self; int width; };
        self->keyWidthIdle_ = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, [](gpointer raw) -> gboolean {
            auto* pending = static_cast<Pending*>(raw);
            pending->self->keyWidthIdle_ = 0;
            pending->self->applyKeyWidth(pending->width + kFixedWidth);
            return G_SOURCE_REMOVE;
        }, new Pending{self, next}, [](gpointer raw) { delete static_cast<Pending*>(raw); });
    }), this);
    gtk_box_pack_start(GTK_BOX(table), rowsScroller_, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(body), table, TRUE, TRUE, 0);

    pager_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ui::spacing);
    style::addClass(pager_, config_style::kPager);
    setAccessibleName(pager_, "Contract pages");
    // The page buttons never widen the window: a narrow window scrolls them, and
    // keyboard focus reveals the focused page button.
    GtkWidget* pagerScroller = gtk_scrolled_window_new(nullptr, nullptr);
    style::addClass(pagerScroller, config_style::kPager);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pagerScroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(pagerScroller), TRUE);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(pagerScroller), TRUE);
    gtk_widget_set_halign(pagerScroller, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(pagerScroller, ui::fieldGap);
    gtk_widget_set_margin_bottom(pagerScroller, ui::spacing);
    GtkWidget* pagerViewport = gtk_viewport_new(nullptr, nullptr);
    gtk_viewport_set_shadow_type(GTK_VIEWPORT(pagerViewport), GTK_SHADOW_NONE);
    gtk_container_add(GTK_CONTAINER(pagerViewport), pager_);
    gtk_container_add(GTK_CONTAINER(pagerScroller), pagerViewport);
    // Only createPager shows it, and only for more than one page.
    gtk_widget_set_no_show_all(pagerScroller, TRUE);
    gtk_container_set_focus_hadjustment(GTK_CONTAINER(pager_),
                                        gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(pagerScroller)));
    gtk_box_pack_start(GTK_BOX(body), pagerScroller, FALSE, FALSE, 0);

    // The message area sits directly above the footer; a click focuses the
    // first blocking contract.
    GtkWidget* errorBox = gtk_event_box_new();
    error_ = label("", style::kLabel, true);
    gtk_widget_set_margin_start(error_, ui::contentMargin);
    gtk_widget_set_margin_end(error_, ui::contentMargin);
    gtk_widget_set_margin_top(error_, ui::fieldGap);
    gtk_widget_set_margin_bottom(error_, ui::fieldGap);
    gtk_container_add(GTK_CONTAINER(errorBox), error_);
    g_signal_connect(errorBox, "button-press-event", gtkCallback([](GtkWidget*, GdkEventButton*, gpointer data) -> gboolean {
        auto* self = static_cast<ContractsWindow*>(data);
        if (self->machine_.model())
            if (auto focus = self->machine_.model()->firstBlockingFocus()) self->focusContract(focus->remoteIp, 1);
        return TRUE;
    }), this);
    gtk_box_pack_start(GTK_BOX(root), errorBox, FALSE, FALSE, 0);

    GtkWidget* separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    style::addClass(separator, style::kSeparator);
    gtk_box_pack_start(GTK_BOX(root), separator, FALSE, FALSE, 0);
    // Wizard footer geometry: check and repeat leading, Cancel and Save trailing.
    GtkWidget* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ui::fieldGap);
    gtk_widget_set_margin_top(footer, ui::compactMargin);
    gtk_widget_set_margin_bottom(footer, ui::compactMargin);
    gtk_widget_set_margin_start(footer, ui::compactMargin);
    gtk_widget_set_margin_end(footer, ui::compactMargin);
    check_ = gtk_button_new_with_label(std::string(kContractsCheck).c_str());
    repeat_ = gtk_button_new_with_label(std::string(kContractsRepeat).c_str());
    cancel_ = gtk_button_new_with_label("Cancel");
    save_ = gtk_button_new_with_label("Save");
    for (auto* button : {check_, repeat_, cancel_}) style::addClass(button, style::kSecondaryButton);
    style::addClass(save_, style::kPrimaryButton);
    gtk_widget_set_size_request(save_, 180, -1);
    gtk_widget_set_size_request(cancel_, 104, -1);
    for (auto* button : {check_, repeat_, save_}) gtk_widget_set_no_show_all(button, TRUE);
    gtk_box_pack_start(GTK_BOX(footer), check_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(footer), repeat_, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(footer), save_, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(footer), cancel_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), footer, FALSE, FALSE, 0);
    // Only Save is the dialog's default action (Enter outside another button).
    gtk_widget_set_can_default(save_, TRUE);
    gtk_window_set_default(GTK_WINDOW(window_), save_);
    g_signal_connect(check_, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        static_cast<ContractsWindow*>(data)->check();
    }), this);
    g_signal_connect(repeat_, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        auto* self = static_cast<ContractsWindow*>(data);
        self->deferred_.post([self] { self->save(true); });
    }), this);
    g_signal_connect(cancel_, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        auto* self = static_cast<ContractsWindow*>(data);
        self->deferred_.post([self] { self->close(); });
    }), this);
    g_signal_connect(save_, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        auto* self = static_cast<ContractsWindow*>(data);
        self->deferred_.post([self] { self->save(false); });
    }), this);
}

bool ContractsWindow::createRows() {
    auto* model = machine_.model();
    if (!model || !rowsBox_) return false;
    changing_ = true;
    const auto sources = model->rows();
    const auto start = static_cast<std::size_t>(page_) * kTablePageSize;
    const auto count = start < sources.size() ? std::min(sources.size() - start, static_cast<std::size_t>(kTablePageSize)) : 0;
    rows_.reserve(count);
    bool complete = true;
    for (const auto& source : sources.subspan(start, count)) {
        auto target = model->target(source.remoteIp());
        if (!target) { complete = false; continue; }
        Row row;
        row.remote = source.remoteIp();
        row.identity = target->contractId();
        row.box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        style::addClass(row.box, config_style::kTableRow);
        GtkWidget* name = label(row.identity, style::kLabel);
        gtk_widget_set_margin_start(name, kCellPadding);
        gtk_widget_set_size_request(name, kContractWidth - kCellPadding, ui::rowHeight);
        gtk_box_pack_start(GTK_BOX(row.box), name, FALSE, FALSE, 0);
        row.toggle = gtk_check_button_new();
        gtk_widget_set_size_request(row.toggle, kEnabledWidth, -1);
        gtk_widget_set_halign(row.toggle, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(row.box), row.toggle, FALSE, FALSE, 0);
        row.key = gtk_entry_new();
        gtk_entry_set_activates_default(GTK_ENTRY(row.key), TRUE);
        gtk_widget_set_valign(row.key, GTK_ALIGN_CENTER);
        gtk_widget_set_margin_start(row.key, kCellPadding);
        gtk_widget_set_margin_end(row.key, kCellPadding);
        gtk_widget_set_size_request(row.key, keyWidth_ - 2 * kCellPadding, 36);
        style::addClass(row.key, style::kMono);
        gtk_box_pack_start(GTK_BOX(row.box), row.key, FALSE, FALSE, 0);
        GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_size_request(actions, kActionsWidth, -1);
        row.change = gtk_button_new_with_label(std::string(kContractsChange).c_str());
        style::addClass(row.change, style::kSecondaryButton);
        style::addClass(row.change, config_style::kChange);
        gtk_widget_set_size_request(row.change, kChangeWidth, 36);
        gtk_widget_set_valign(row.change, GTK_ALIGN_CENTER);
        gtk_widget_set_margin_start(row.change, ui::fieldGap);
        gtk_box_pack_start(GTK_BOX(actions), row.change, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row.box), actions, FALSE, FALSE, 0);
        // Accessible names from the row identity (UX03): "<contract> Enabled" etc.
        setAccessibleName(row.toggle, row.identity + " Enabled");
        setAccessibleName(row.key, row.identity + " Key");
        setAccessibleName(row.change, row.identity + " Change");
        const auto index = rows_.size();
        const auto remote = row.remote;
        attachContractsEdit(row.key, kContractsKeyBound, [this, remote](std::u16string_view text) {
            return text.size() <= kContractsKeyBound && acceptKey(remote, text);
        }, &changing_);
        GtkWidget* controls[]{row.toggle, row.key, row.change};
        for (int column = 0; column < 3; ++column) {
            auto& binding = bindings_.emplace_back(std::make_unique<Binding>(Binding{this, index, column}));
            g_signal_connect(controls[column], "focus-in-event", gtkCallback([](GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
                auto* bound = static_cast<Binding*>(data);
                auto* self = bound->owner;
                if (bound->index < self->rows_.size() && self->machine_.model())
                    (void)self->machine_.model()->setFocus(ContractFocus{self->rows_[bound->index].remote,
                        bound->column == 2 ? ContractColumn::password : static_cast<ContractColumn>(bound->column)});
                return FALSE;
            }), binding.get());
        }
        g_signal_connect(row.toggle, "toggled", gtkCallback([](GtkToggleButton*, gpointer data) {
            auto* bound = static_cast<Binding*>(data);
            bound->owner->rowEvent(bound->index, 0);
        }), bindings_[bindings_.size() - 3].get());
        g_signal_connect(row.key, "changed", gtkCallback([](GtkEditable*, gpointer data) {
            auto* bound = static_cast<Binding*>(data);
            bound->owner->rowEvent(bound->index, 1);
        }), bindings_[bindings_.size() - 2].get());
        g_signal_connect(row.change, "clicked", gtkCallback([](GtkButton*, gpointer data) {
            auto* bound = static_cast<Binding*>(data);
            bound->owner->rowEvent(bound->index, 2);
        }), bindings_[bindings_.size() - 1].get());
        gtk_box_pack_start(GTK_BOX(rowsBox_), row.box, FALSE, FALSE, 0);
        rows_.push_back(row);
        syncRow(rows_.back());
    }
    gtk_widget_show_all(rowsBox_);
    changing_ = false;
    return complete;
}

void ContractsWindow::destroyRows() {
    const bool wasChanging = changing_;
    changing_ = true;
    for (auto& row : rows_) {
        // Clear the native Key text before the widget goes away (UX13).
        if (row.key) gtk_entry_set_text(GTK_ENTRY(row.key), "");
        if (row.box) gtk_widget_destroy(row.box);
    }
    rows_.clear();
    bindings_.clear();
    changing_ = wasChanging;
}

void ContractsWindow::createPager() {
    if (!pager_) return;
    GList* children = gtk_container_get_children(GTK_CONTAINER(pager_));
    for (GList* item = children; item; item = item->next) gtk_widget_destroy(GTK_WIDGET(item->data));
    g_list_free(children);
    const auto items = contractsPagerItems(pageCount(), page_);
    for (const auto& item : items) {
        if (item.page < 0) {
            GtkWidget* ellipsis = gtk_label_new(item.label.c_str());
            gtk_widget_set_size_request(ellipsis, 36, 36);
            gtk_box_pack_start(GTK_BOX(pager_), ellipsis, FALSE, FALSE, 0);
            continue;
        }
        GtkWidget* button = gtk_button_new_with_label(item.label.c_str());
        style::addClass(button, item.current ? style::kPrimaryButton : style::kSecondaryButton);
        // Page buttons use the table's compact 36 DIP square, not the footer minimum.
        gtk_widget_set_size_request(button, 36, 36);
        gtk_widget_set_sensitive(button, item.sensitive);
        setAccessibleName(button, item.accessibleName);
        g_object_set_data(G_OBJECT(button), "phpvpn-page", GINT_TO_POINTER(item.page));
        g_signal_connect(button, "clicked", gtkCallback([](GtkButton* clicked, gpointer data) {
            auto* self = static_cast<ContractsWindow*>(data);
            // The button is destroyed by the page change; run it after this handler.
            self->pendingPage_ = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(clicked), "phpvpn-page"));
            self->pendingLastRow_ = false;
            if (!self->pageIdle_)
                self->pageIdle_ = g_idle_add([](gpointer raw) -> gboolean {
                    auto* owner = static_cast<ContractsWindow*>(raw);
                    owner->pageIdle_ = 0;
                    owner->changePage(owner->pendingPage_);
                    return G_SOURCE_REMOVE;
                }, self);
        }), this);
        gtk_box_pack_start(GTK_BOX(pager_), button, FALSE, FALSE, 0);
    }
    // The pager's scrolled window (a no-show-all holder) follows the pager.
    GtkWidget* viewport = gtk_widget_get_parent(pager_);
    if (!items.empty()) gtk_widget_show_all(viewport);
    gtk_widget_set_visible(gtk_widget_get_parent(viewport), !items.empty());
}

void ContractsWindow::changePage(int page) {
    auto* model = machine_.model();
    if (!window_ || ending_ || closing_ || !model || model->frozen() || page < 0 || page >= pageCount()) return;
    GtkWidget* focused = gtk_window_get_focus(GTK_WINDOW(window_));
    const bool pagerFocus = focused && gtk_widget_is_ancestor(focused, pager_);
    if (page != page_) {
        const int previous = page_;
        gtk_widget_grab_focus(cancel_);
        destroyRows();
        page_ = page;
        if (!createRows()) {
            destroyRows();
            page_ = previous;
            if (!createRows()) destroyRows();
            localError_ = std::string(kContractsPageFailed);
        }
        createPager();
        gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(rowsScroller_)), 0);
        refresh();
    }
    GtkWidget* next = rows_.empty() ? save_ : (pendingLastRow_ ? rows_.back().toggle : rows_.front().toggle);
    if (pagerFocus) {
        GList* children = gtk_container_get_children(GTK_CONTAINER(pager_));
        for (GList* item = children; item; item = item->next) {
            auto* child = GTK_WIDGET(item->data);
            if (GTK_IS_BUTTON(child) && GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "phpvpn-page")) == page_ &&
                gtk_widget_get_sensitive(child)) { next = child; break; }
        }
        g_list_free(children);
    }
    pendingLastRow_ = false;
    gtk_widget_grab_focus(next);
}

void ContractsWindow::focusContract(Ipv4Address remote, int column) {
    auto* model = machine_.model();
    if (!model) return;
    const auto sources = model->rows();
    for (std::size_t i = 0; i < sources.size(); ++i) {
        if (sources[i].remoteIp() != remote) continue;
        changePage(static_cast<int>(i / kTablePageSize));
        for (const auto& row : rows_) {
            if (row.remote != remote) continue;
            GtkWidget* controls[]{row.toggle, row.key, row.change};
            gtk_widget_grab_focus(controls[std::clamp(column, 0, 2)]);
        }
        break;
    }
}

void ContractsWindow::syncRow(Row& row) {
    const auto* source = machine_.model() ? machine_.model()->row(row.remote) : nullptr;
    if (!source) return;
    const bool wasChanging = changing_;
    changing_ = true;
    const std::string key(source->keyText());
    if (key != gtk_entry_get_text(GTK_ENTRY(row.key))) gtk_entry_set_text(GTK_ENTRY(row.key), key.c_str());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(row.toggle), source->enabled());
    changing_ = wasChanging;
}

bool ContractsWindow::acceptKey(Ipv4Address remote, std::u16string_view text) {
    if (changing_) return true;
    auto* model = machine_.model();
    if (!model || model->frozen()) return false;
    const auto* source = model->row(remote);
    if (!source || !source->enabled()) return false;
    std::string utf8;
    if (!contractsKeyTextSupported(text, utf8)) {
        localError_ = std::string(kContractsUnsupportedKey);
        refresh();
        return false;
    }
    const auto accepted = model->setKey(remote, utf8);
    secureWipe(utf8.data(), utf8.size());
    if (!accepted) {
        localError_ = std::string(kContractsKeyBudget);
        refresh();
        return false;
    }
    localError_.clear();
    return true;
}

void ContractsWindow::rowEvent(std::size_t index, int column) {
    auto* model = machine_.model();
    if (ending_ || closing_ || !model || index >= rows_.size() || changing_) return;
    auto& row = rows_[index];
    if (savedSnapshot_ || model->frozen()) {
        // A frozen or saved draft never changes; restore the visual state.
        if (column != 2) syncRow(row);
        return;
    }
    if (column == 0) {
        (void)model->setEnabled(row.remote, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(row.toggle)) != FALSE);
        machine_.changed();
        refresh();
        return;
    }
    if (column == 1) {
        auto text = contractsUtf16(gtk_entry_get_text(GTK_ENTRY(row.key)));
        acceptKey(row.remote, text);
        secureWipe(text.data(), text.size() * sizeof(char16_t));
        machine_.changed();
        refresh();
        return;
    }
    openPasswordSha(index);
}

void ContractsWindow::openPasswordSha(std::size_t index) {
    auto* model = machine_.model();
    const auto remote = rows_[index].remote;
    const auto* source = model->row(remote);
    if (!source || !source->enabled() || !crypto_) return;
    auto target = model->target(remote);
    if (!target) return;
    std::erase_if(children_, [](const auto& child) { return !child->window(); });
    auto child = std::make_unique<PasswordShaWindow>(*crypto_, *target,
        [this](const ContractTarget& captured, std::string_view key) {
            auto* current = machine_.model();
            if (ending_ || closing_ || !current) return false;
            const auto* row = current->row(captured.remoteIp());
            if (!row || !row->enabled()) return false;
            if (!current->applyPasswordKey(captured, key)) return false;
            machine_.changed();
            for (auto& targetRow : rows_)
                if (targetRow.remote == captured.remoteIp()) syncRow(targetRow);
            localError_.clear();
            refresh();
            return true;
        },
        [this](const ContractTarget& closed) {
            auto* current = machine_.model();
            if (ending_ || closing_ || !window_ || !current || current->frozen()) return;
            const auto* row = current->row(closed.remoteIp());
            focusContract(closed.remoteIp(), row && row->enabled() ? 2 : 1);
        });
    if (child->open(GTK_WINDOW(window_))) children_.push_back(std::move(child));
    else {
        localError_ = std::string(kContractsPasswordFailed);
        refresh();
    }
}

void ContractsWindow::save(bool repeat) {
    auto* model = machine_.model();
    if (!window_ || savedSnapshot_ || !model || contractsSnapshot().saving) return;
    if (!repeat && !model->dirty()) return;
    if (repeat) {
        if (!machine_.canRepeat(checker_.busy())) return;
        const auto before = contractsSnapshot();
        if (!confirm(GTK_WINDOW(window_), kContractsRepeatQuestion, kContractsRepeat) || !window_ ||
            contractsSnapshot() != before)
            return;
    }
    if (!machine_.prepare(repeat, checker_.busy())) {
        refresh();
        return;
    }
    // The main OK freezes the candidate first, then closes its children without
    // applying their unconfirmed changes (UX03).
    closeChildren();
    machine_.dispatch(static_cast<std::uint64_t>(monotonicMs()), [this](const SaveOperation& operation, const std::string& payload) {
        return writer_.requestSave(operation, payload, [this](const SaveResult& result) { onSaveResult(result); });
    });
    refresh();
}

void ContractsWindow::check() {
    if (!window_ || !machine_.model()) return;
    machine_.check(checker_.busy(), [this](std::uint64_t generation) {
        WizardRequest request;
        request.command = WizardCommand::get_config_document;
        return checker_.requestWizard(request, [this, generation](WizardResult& result) { onCheckResult(generation, result); });
    });
    refresh();
}

void ContractsWindow::onSaveResult(const SaveResult& result) {
    if (!machine_.saving()) return;
    handle(machine_.saveFinished(result));
}

void ContractsWindow::onCheckResult(std::uint64_t checkGeneration, WizardResult& result) {
    struct Wipe {
        WizardResult& result;
        ~Wipe() { if (result.reply) wipeJson(result.reply.value().result); }
    } wipe{result};
    handle(machine_.checkFinished(checkGeneration, result.reply));
}

void ContractsWindow::handle(ContractsEvent event) {
    if (!window_) return;
    if (event == ContractsEvent::saved) {
        savedSnapshot_ = contractsSnapshot();
        refresh();
        // The tray verifies the connection, asks the shared reconnect question and
        // finally discards exactly this saved snapshot.
        app_.requestReconnectAfterSave(this, lifecycleSnapshot());
        return;
    }
    if (event == ContractsEvent::matched) {
        const auto before = contractsSnapshot();
        showMessage(GTK_WINDOW(window_), kContractsMatched, kContractsTitle, DialogIcon::information);
        if (window_ && contractsSnapshot() == before) closeWindow();
        return;
    }
    if (event != ContractsEvent::none) refresh();
}

void ContractsWindow::refresh() {
    auto* model = machine_.model();
    if (!window_ || !save_ || !model) return;
    const auto sources = model->rows();
    const auto changes = static_cast<std::size_t>(std::count_if(sources.begin(), sources.end(),
                                                                [](const auto& row) { return row.dirty(); }));
    const auto saveText = contractsSaveLabel(changes);
    if (saveText != gtk_button_get_label(GTK_BUTTON(save_))) gtk_button_set_label(GTK_BUTTON(save_), saveText.c_str());
    const auto snapshot = contractsSnapshot();
    const auto footer = contractsFooter(snapshot, savedSnapshot_.has_value(), changes, model->canPrepareSave(),
                                        machine_.canRepeat(checker_.busy()));
    gtk_widget_set_sensitive(rowsBox_, footer.tableSensitive);
    gtk_widget_set_sensitive(pager_, footer.tableSensitive);
    gtk_widget_set_visible(save_, footer.saveVisible);
    gtk_widget_set_sensitive(save_, footer.saveEnabled);
    gtk_widget_set_visible(check_, footer.checkVisible);
    gtk_widget_set_sensitive(check_, footer.checkEnabled);
    gtk_widget_set_visible(repeat_, footer.repeatVisible);
    gtk_widget_set_sensitive(cancel_, !snapshot.saving);
    gtk_window_set_deletable(GTK_WINDOW(window_), !snapshot.saving);
    const auto message = contractsMessage(localError_, model->firstBlockingFocus(), machine_.notice());
    if (message.text != gtk_label_get_text(GTK_LABEL(error_))) gtk_label_set_text(GTK_LABEL(error_), message.text.c_str());
    style::setClass(error_, config_style::kValidation, message.validation);
    gtk_widget_set_visible(gtk_widget_get_parent(error_), !message.text.empty());
    gtk_widget_set_visible(error_, !message.text.empty());
    gtk_widget_set_visible(notice_, connectionActive_);
    for (auto& row : rows_) {
        const auto* source = model->row(row.remote);
        if (!source) continue;
        const bool frozen = snapshot.saving || snapshot.unknown;
        gtk_widget_set_sensitive(row.toggle, !frozen);
        gtk_widget_set_sensitive(row.key, !frozen);
        // A disabled contract keeps its Key read-only but selectable (UX02).
        gtk_editable_set_editable(GTK_EDITABLE(row.key), source->enabled());
        gtk_widget_set_sensitive(row.change, source->enabled() && !frozen);
        const bool dirty = source->dirty();
        if (dirty != row.lastDirty) {
            row.lastDirty = dirty;
            style::setClass(row.box, config_style::kChanged, dirty);
            style::setClass(row.key, config_style::kChanged, dirty);
        }
        const bool valid = !source->keyErrorVisible();
        if (valid != row.lastValid) {
            row.lastValid = valid;
            style::setClass(row.key, config_style::kInvalid, !valid);
            atk_object_set_description(gtk_widget_get_accessible(row.key),
                                       valid ? "" : std::string(kContractsInvalidKeyDescription).c_str());
        }
    }
    for (const auto& child : children_) {
        if (!child->window()) continue;
        const auto* source = model->row(child->remoteIp());
        child->setTargetEnabled(source && source->enabled() && !snapshot.saving && !snapshot.unknown);
    }
}

void ContractsWindow::applyKeyWidth(int viewportWidth) {
    const int next = std::max(kMinKeyWidth, viewportWidth - kFixedWidth);
    if (!window_ || next == keyWidth_) return;
    keyWidth_ = next;
    if (headerKey_) gtk_widget_set_size_request(headerKey_, keyWidth_ - kCellPadding, ui::headerHeight);
    for (auto& row : rows_) gtk_widget_set_size_request(row.key, keyWidth_ - 2 * kCellPadding, 36);
}

void ContractsWindow::setConnectionActive(bool active) {
    if (connectionActive_ == active) return;
    connectionActive_ = active;
    refresh();
}

ContractsLifecycleSnapshot ContractsWindow::contractsSnapshot() const {
    auto snapshot = machine_.snapshot(checker_.busy());
    snapshot.open = window_ != nullptr;
    return snapshot;
}

LifecycleSnapshot ContractsWindow::lifecycleSnapshot() const {
    const auto current = contractsSnapshot();
    // Any change of the detailed snapshot (generations, deadline, checking) or of
    // the saved state is a new lifecycle generation.
    if (current != lastSnapshot_ || savedSnapshot_.has_value() != lastSaved_) {
        lastSnapshot_ = current;
        lastSaved_ = savedSnapshot_.has_value();
        ++revision_;
    }
    LifecycleSnapshot snapshot;
    snapshot.open = current.open;
    snapshot.dirty = current.open && current.dirty;
    snapshot.unknown = current.open && current.unknown;
    snapshot.busy = current.open && current.saving;
    snapshot.generation = revision_;
    return snapshot;
}

bool ContractsWindow::approvedDiscard(const LifecycleSnapshot& approved) {
    if (!window_ || approved.busy || lifecycleSnapshot() != approved) return false;
    closeWindow();
    return true;
}

void ContractsWindow::close() {
    if (!window_ || closing_) return;
    const auto before = lifecycleSnapshot();
    if (before.busy) return;
    if ((before.unknown || before.dirty) &&
        !confirm(GTK_WINDOW(window_), before.unknown ? kContractsUnknownClose : kContractsDiscard, kContractsTitle))
        return;
    approvedDiscard(before);
}

void ContractsWindow::closeChildren() {
    for (auto& child : children_) child->dismiss();
    children_.clear();
}

void ContractsWindow::closeWindow() {
    closing_ = true;
    savedSnapshot_.reset();
    closeChildren();
    if (window_) gtk_widget_destroy(window_);
    writer_.abandon();
    checker_.abandon();
    machine_.close();
    crypto_.reset();
    closing_ = false;
}

void ContractsWindow::endSession() {
    if (ending_) return;
    ending_ = true;
    closeWindow();
}

bool ContractsWindow::onKey(GdkEventKey* event) {
    if (event->keyval == GDK_KEY_Escape) {
        close();
        return true;
    }
    GtkWidget* focused = window_ ? gtk_window_get_focus(GTK_WINDOW(window_)) : nullptr;
    bool navigation = false;
    for (const auto& row : rows_) navigation = navigation || focused == row.toggle || focused == row.change;
    if (!navigation) return false;
    // Outside the Key text: Ctrl+End goes to the last row, PageUp/PageDown scroll.
    if (event->keyval == GDK_KEY_End && (event->state & GDK_CONTROL_MASK)) {
        pendingLastRow_ = true;
        changePage(pageCount() - 1);
        return true;
    }
    if (event->keyval == GDK_KEY_Page_Down || event->keyval == GDK_KEY_Page_Up) {
        auto* adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(rowsScroller_));
        const double page = gtk_adjustment_get_page_size(adjustment);
        gtk_adjustment_set_value(adjustment, gtk_adjustment_get_value(adjustment) +
                                                 (event->keyval == GDK_KEY_Page_Down ? page : -page));
        return true;
    }
    return false;
}

void ContractsWindow::onDestroyed() {
    if (keyWidthIdle_) { g_source_remove(keyWidthIdle_); keyWidthIdle_ = 0; }
    if (pageIdle_) { g_source_remove(pageIdle_); pageIdle_ = 0; }
    changing_ = true;
    for (auto& row : rows_)
        if (row.key) gtk_entry_set_text(GTK_ENTRY(row.key), "");
    changing_ = false;
    rows_.clear();
    bindings_.clear();
    window_ = local_ = notice_ = headerKey_ = rowsScroller_ = rowsBox_ = pager_ = error_ = nullptr;
    check_ = repeat_ = cancel_ = save_ = nullptr;
    // A window closed by any path discards the draft; late replies are revoked.
    if (!closing_) closeWindow();
}
}  // namespace phpvpn::tray
