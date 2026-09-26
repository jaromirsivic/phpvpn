#include "server_info_window.h"
#include "gtk_util.h"

#include <algorithm>
#include <memory>

#include "dialogs.h"
#include "style.h"
#include "tray_app.h"
#include "ui_style.h"

namespace phpvpn::tray {
namespace {
constexpr char kCountClass[] = "phpvpn-info-count";
constexpr char kTabsClass[] = "phpvpn-info-tabs";
constexpr char kTableClass[] = "phpvpn-info-table";
constexpr char kPagerClass[] = "phpvpn-pager";
constexpr char kToggleClass[] = "phpvpn-toggle";
constexpr char kFooterClass[] = "phpvpn-info-footer";
constexpr char kHolderClass[] = "phpvpn-info-footer-holder";
constexpr int kMargin = ui::compactMargin;  // footer geometry of the Wizard: 16 / 8 / 36 DIP
enum Column { contractColumn, lastSeenColumn, statusColumn, backgroundColumn, columnCount };

// Window specific rules on top of style.h: count colour, tab headers (selectedStep
// active, primary inactive), the contracts table header, compact pager buttons and
// the accessible sliding Auto refresh checkbox (a real GtkCheckButton).
void installCss() {
    static bool installed = false;
    GdkScreen* screen = gdk_screen_get_default();
    if (installed || !screen) return;
    using ui::Color;
    using style::hex;
    const std::string w = std::string(".") + style::kWindow;
    std::string css;
    css += w + " label." + kCountClass + " { color: " + hex(Color::success) + "; }\n";
    css += "notebook." + std::string(kTabsClass) + " > header { background-color: " + hex(Color::background) + "; border: none; }\n";
    css += "notebook." + std::string(kTabsClass) + " > header > tabs > tab { background-color: " + hex(Color::primary) +
           "; min-height: 28px; padding: 4px 16px; margin: 0 2px 0 0; border: none; }\n";
    css += w + " notebook." + kTabsClass + " > header > tabs > tab label { color: " + hex(Color::primaryText) +
           "; font-size: 16px; }\n";
    css += "notebook." + std::string(kTabsClass) + " > header > tabs > tab:checked { background-color: " +
           hex(Color::selectedStep) + "; }\n";
    css += w + " notebook." + kTabsClass + " > header > tabs > tab:checked label { color: " + hex(Color::text) + "; }\n";
    css += "notebook." + std::string(kTabsClass) + " > stack { background-color: " + hex(Color::surface) + "; }\n";
    css += "treeview." + std::string(kTableClass) + " { font-size: 15px; color: " + hex(Color::text) + "; }\n";
    css += "treeview." + std::string(kTableClass) + " header button { background-color: " + hex(Color::contractsHeader) +
           "; background-image: none; border-radius: 0; min-height: " + std::to_string(ui::headerHeight) +
           "px; padding: 0 12px; }\n";
    css += w + " treeview." + kTableClass + " header button label { color: " + hex(Color::primaryText) +
           "; font-size: 16px; font-weight: 700; }\n";
    css += "." + std::string(kPagerClass) + " button." + style::kPrimaryButton + ", ." + kPagerClass + " button." +
           style::kSecondaryButton + " { min-width: 32px; padding: 0 6px; }\n";
    // A disabled primary button (Refresh while pending, the current-page arrows)
    // keeps a readable label instead of white on the disabled background.
    css += w + " ." + kFooterClass + " button:disabled label, " + w + " ." + kPagerClass +
           " button:disabled label { color: " + hex(Color::disabledText) + "; }\n";
    css += "scrolledwindow." + std::string(kHolderClass) + ", scrolledwindow." + kHolderClass +
           " viewport { background-color: transparent; background-image: none; border: none; }\n";
    css += "checkbutton." + std::string(kToggleClass) + " { min-height: 32px; }\n";
    css += "checkbutton." + std::string(kToggleClass) + " check { -gtk-icon-source: none; min-width: 40px; min-height: 22px;"
           " border-radius: 11px; border: 1px solid " + hex(Color::border) + "; background-color: " +
           hex(Color::disabledBackground) + "; background-image: radial-gradient(circle closest-side, " +
           hex(Color::surface) + " 72%, " + hex(Color::disabledText) + " 78%, transparent 84%);"
           " background-size: 22px 22px; background-repeat: no-repeat; background-position: left center; }\n";
    css += "checkbutton." + std::string(kToggleClass) + " check:checked { border-color: " + hex(Color::primary) +
           "; background-color: " + hex(Color::primary) + "; background-image: radial-gradient(circle closest-side, " +
           hex(Color::surface) + " 72%, transparent 84%); background-position: right center; }\n";
    css += w + " checkbutton:focus, " + w + " treeview:focus, " + w + " textview:focus { outline-color: " +
           hex(Color::focus) + "; outline-style: solid; outline-width: 2px; outline-offset: 1px; }\n";
    GtkCssProvider* provider = gtk_css_provider_new();
    g_signal_connect(provider, "parsing-error", gtkCallback([](GtkCssProvider*, GtkCssSection*, GError* error, gpointer) {
        g_warning("PHPVPN server information stylesheet: %s", error ? error->message : "parse error");
    }), nullptr);
    gtk_css_provider_load_from_data(provider, css.c_str(), static_cast<gssize>(css.size()), nullptr);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_unref(provider);
    installed = true;
}

GtkWidget* label(const char* text, const char* cssClass, bool wrap) {
    GtkWidget* widget = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(widget), 0.0f);
    if (wrap) {
        gtk_label_set_line_wrap(GTK_LABEL(widget), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(widget), PANGO_WRAP_WORD_CHAR);
    }
    if (cssClass) style::addClass(widget, cssClass);
    return widget;
}

GtkWidget* button(std::string_view text, const char* cssClass) {
    GtkWidget* widget = gtk_button_new_with_label(std::string(text).c_str());
    style::addClass(widget, cssClass);
    return widget;
}

int naturalWidth(GtkWidget* widget) {
    int minimum = 0, natural = 0;
    gtk_widget_get_preferred_width(widget, &minimum, &natural);
    return natural;
}

// Boxes keep their children in the accessible order (GtkGrid reports them in
// reverse); wrapping only changes the orientation.
void orient(GtkWidget* box, bool vertical) {
    gtk_orientable_set_orientation(GTK_ORIENTABLE(box), vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
}

const char* rowBackground(ServerClientState state) {
    static const std::string valid = style::hex(ui::Color::serverInfoValid),
                             invalid = style::hex(ui::Color::contractsChanged),
                             offline = style::hex(ui::Color::disabledBackground);
    return state == ServerClientState::valid ? valid.c_str()
         : state == ServerClientState::invalid ? invalid.c_str() : offline.c_str();
}

template <class F>
guint idle(ServerInfoWindow* self, F function) {
    struct Call { ServerInfoWindow* self; F function; };
    return g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, [](gpointer raw) -> gboolean {
        auto* call = static_cast<Call*>(raw);
        call->function(call->self);
        return G_SOURCE_REMOVE;
    }, new Call{self, function}, [](gpointer raw) { delete static_cast<Call*>(raw); });
}
}  // namespace

ServerInfoWindow::ServerInfoWindow(IpcEndpoint endpoint, Dispatcher dispatcher)
    : channel_(std::move(endpoint), std::move(dispatcher)) {}

ServerInfoWindow::~ServerInfoWindow() { endSession(); }

void ServerInfoWindow::open() {
    if (window_) {
        // Re-choosing Show Info never requests; it only switches Auto refresh off.
        session_.stopAutoRefresh();
        updatingSwitch_ = true;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autoRefresh_), FALSE);
        updatingSwitch_ = false;
        syncAutoTimer();
        if (session_.model()) gtk_window_present(GTK_WINDOW(window_));
        return;
    }
    session_.reset();
    build();
    request();
}

void ServerInfoWindow::endSession() {
    channel_.abandon();
    session_.reset();
    if (autoTimer_) g_source_remove(autoTimer_);
    autoTimer_ = 0;
    if (placement_) g_source_remove(placement_);
    placement_ = 0;
    if (window_) gtk_widget_destroy(window_);  // onDestroyed clears the widget pointers
}

void ServerInfoWindow::onDestroyed() {
    channel_.abandon();
    session_.reset();
    if (autoTimer_) g_source_remove(autoTimer_);
    if (placement_) g_source_remove(placement_);
    autoTimer_ = placement_ = 0;
    if (rows_) g_object_unref(rows_);
    rows_ = nullptr;
    window_ = content_ = header_ = heading_ = count_ = tabs_ = table_ = raw_ = pagerBox_ = notice_ = nullptr;
    footer_ = footerPair_ = refresh_ = autoRefresh_ = ok_ = nullptr;
    headerWrapped_ = footerWrapped_ = false;
}

void ServerInfoWindow::build() {
    installCss();
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    style::prepareWindow(GTK_WINDOW(window_), std::string(kServerInfoTitle).c_str(), ui::editorClient);
    gtk_widget_set_size_request(window_, 320, 480);
    g_signal_connect(window_, "delete-event", gtkCallback([](GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
        static_cast<ServerInfoWindow*>(data)->endSession();
        return TRUE;
    }), this);
    g_signal_connect(window_, "key-press-event", gtkCallback([](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
        return static_cast<ServerInfoWindow*>(data)->onKey(event);
    }), this);
    g_signal_connect(window_, "destroy", gtkCallback([](GtkWidget*, gpointer data) {
        static_cast<ServerInfoWindow*>(data)->onDestroyed();
    }), this);

    content_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, ui::fieldGap);
    gtk_widget_set_margin_top(content_, kMargin);
    gtk_widget_set_margin_bottom(content_, kMargin);
    gtk_widget_set_margin_start(content_, kMargin);
    gtk_widget_set_margin_end(content_, kMargin);
    gtk_container_add(GTK_CONTAINER(window_), content_);
    g_signal_connect(content_, "size-allocate", gtkCallback([](GtkWidget*, GdkRectangle*, gpointer data) {
        auto* self = static_cast<ServerInfoWindow*>(data);
        // Re-attaching during allocation would recurse into layout; defer it.
        if (!self->placement_)
            self->placement_ = idle(self, [](ServerInfoWindow* owner) { owner->placement_ = 0; owner->placeWrapped(); });
    }), this);

    header_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ui::fieldGap);
    heading_ = label(std::string(kServerInfoTitle).c_str(), style::kHeading, true);
    count_ = label("", style::kHeading, true);
    style::addClass(count_, kCountClass);
    gtk_box_pack_start(GTK_BOX(header_), heading_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_), count_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content_), header_, FALSE, FALSE, 0);

    tabs_ = gtk_notebook_new();
    style::addClass(tabs_, kTabsClass);
    gtk_widget_set_vexpand(tabs_, TRUE);
    // Scrollable tab headers keep the 320 DIP minimum width of the Windows window.
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(tabs_), TRUE);
    gtk_box_pack_start(GTK_BOX(content_), tabs_, TRUE, TRUE, 0);

    // Who is online: table and pager. Paging and tab switches are local only.
    GtkWidget* who = gtk_box_new(GTK_ORIENTATION_VERTICAL, ui::fieldGap);
    gtk_widget_set_margin_top(who, ui::fieldGap);
    rows_ = gtk_list_store_new(columnCount, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    table_ = gtk_tree_view_new_with_model(GTK_TREE_MODEL(rows_));
    style::addClass(table_, kTableClass);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(table_), FALSE);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(table_), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(table_)), GTK_SELECTION_SINGLE);
    const int widths[]{280, 210, 420};
    for (int column = 0; column < 3; ++column) {
        GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
        g_object_set(renderer, "xpad", 12, "foreground", style::hex(ui::Color::text).c_str(), nullptr);
        gtk_cell_renderer_set_fixed_size(renderer, -1, ui::rowHeight);
        GtkTreeViewColumn* view = gtk_tree_view_column_new_with_attributes(
            std::string(kServerInfoColumns[column]).c_str(), renderer, "text", column, "cell-background",
            backgroundColumn, nullptr);
        gtk_tree_view_column_set_sizing(view, GTK_TREE_VIEW_COLUMN_FIXED);
        gtk_tree_view_column_set_fixed_width(view, widths[column]);
        gtk_tree_view_column_set_resizable(view, TRUE);
        gtk_tree_view_column_set_expand(view, column == 2);
        gtk_tree_view_append_column(GTK_TREE_VIEW(table_), view);
    }
    atk_object_set_name(gtk_widget_get_accessible(table_), std::string(kServerInfoWhoTab).c_str());
    GtkWidget* tableScroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tableScroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(tableScroller), GTK_SHADOW_IN);
    gtk_widget_set_vexpand(tableScroller, TRUE);
    gtk_container_add(GTK_CONTAINER(tableScroller), table_);
    gtk_box_pack_start(GTK_BOX(who), tableScroller, TRUE, TRUE, 0);
    pagerBox_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    style::addClass(pagerBox_, kPagerClass);
    // Up to 13 pager buttons must not pin the 320 DIP minimum width either; on a
    // narrow window the pager scrolls horizontally and every page stays reachable.
    GtkWidget* pagerScroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pagerScroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(pagerScroller), TRUE);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(pagerScroller), TRUE);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(pagerScroller), GTK_SHADOW_NONE);
    style::addClass(pagerScroller, kHolderClass);
    gtk_container_add(GTK_CONTAINER(pagerScroller), pagerBox_);
    if (GtkWidget* viewport = gtk_bin_get_child(GTK_BIN(pagerScroller)); GTK_IS_VIEWPORT(viewport))
        gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), GTK_SHADOW_NONE);
    gtk_box_pack_start(GTK_BOX(who), pagerScroller, FALSE, FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs_), who, gtk_label_new(std::string(kServerInfoWhoTab).c_str()));

    // Raw server information: the exact authenticated plaintext, read-only and
    // selectable, both scrollbars, no wrapping or reformatting.
    GtkWidget* rawScroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(rawScroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_margin_top(rawScroller, ui::fieldGap);
    raw_ = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(raw_), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(raw_), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(raw_), GTK_WRAP_NONE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(raw_), TRUE);
    style::addClass(raw_, style::kMono);
    atk_object_set_name(gtk_widget_get_accessible(raw_), std::string(kServerInfoRawTab).c_str());
    gtk_container_add(GTK_CONTAINER(rawScroller), raw_);
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs_), rawScroller, gtk_label_new(std::string(kServerInfoRawTab).c_str()));

    notice_ = label("", style::kValue, true);
    gtk_widget_set_no_show_all(notice_, TRUE);
    gtk_box_pack_start(GTK_BOX(content_), notice_, FALSE, FALSE, 0);

    // Footer: the indivisible Refresh + Auto refresh pair leading, OK trailing.
    footer_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ui::fieldGap);
    style::addClass(footer_, kFooterClass);
    footerPair_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ui::fieldGap);
    refresh_ = button(kServerInfoRefresh, style::kPrimaryButton);
    gtk_widget_set_size_request(refresh_, 104, ui::buttonHeight);
    autoRefresh_ = gtk_check_button_new_with_label(std::string(kServerInfoAutoRefresh).c_str());
    style::addClass(autoRefresh_, kToggleClass);
    gtk_box_pack_start(GTK_BOX(footerPair_), refresh_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(footerPair_), autoRefresh_, FALSE, FALSE, 0);
    ok_ = button(kServerInfoOk, style::kPrimaryButton);
    gtk_widget_set_halign(ok_, GTK_ALIGN_END);
    gtk_widget_set_halign(footerPair_, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(footer_), footerPair_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(footer_), ok_, TRUE, TRUE, 0);
    // A horizontal footer would pin the window above the 320 DIP minimum and so
    // never get the chance to wrap: the holder hides its minimum width (never
    // scrolls), and placeWrapped() puts OK under the pair when it does not fit.
    GtkWidget* holder = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(holder), GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(holder), TRUE);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(holder), TRUE);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(holder), GTK_SHADOW_NONE);
    style::addClass(holder, kHolderClass);
    gtk_container_add(GTK_CONTAINER(holder), footer_);
    if (GtkWidget* viewport = gtk_bin_get_child(GTK_BIN(holder)); GTK_IS_VIEWPORT(viewport))
        gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), GTK_SHADOW_NONE);
    gtk_box_pack_end(GTK_BOX(content_), holder, FALSE, FALSE, 0);

    g_signal_connect(refresh_, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        static_cast<ServerInfoWindow*>(data)->request();
    }), this);
    g_signal_connect(ok_, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        static_cast<ServerInfoWindow*>(data)->endSession();
    }), this);
    g_signal_connect(autoRefresh_, "toggled", gtkCallback([](GtkToggleButton* toggle, gpointer data) {
        auto* self = static_cast<ServerInfoWindow*>(data);
        if (self->updatingSwitch_) return;
        self->session_.setAutoRefresh(gtk_toggle_button_get_active(toggle) != FALSE, monotonicMs());
        self->syncAutoTimer();
    }), this);
    // Every descendant is realised now; the window itself appears only after the
    // first verified snapshot (finish).
    gtk_widget_show_all(content_);
}

void ServerInfoWindow::request() {
    if (!window_ || session_.pending()) return;
    const auto token = session_.begin();
    if (!token) return;
    syncAutoTimer();
    gtk_widget_set_sensitive(refresh_, FALSE);
    setNotice(kServerInfoLoading);
    const bool sent = channel_.request(IpcCommand::get_server_info, {}, [this, value = *token](std::string& response) {
        finish(value, response);
    });
    if (sent) return;
    const auto failure = session_.notSent(monotonicMs());
    gtk_widget_set_sensitive(refresh_, TRUE);
    setNotice(failure.notice);
    if (failure.fatal) {
        const auto generation = session_.generation();
        showError(window(), *failure.fatal, "PHPVPN");
        if (session_.generation() == generation) endSession();
        return;
    }
    syncAutoTimer();
}

void ServerInfoWindow::finish(std::uint64_t token, std::string& response) {
    if (!window_) return;
    const auto result = session_.finish(token, response, selectedRow(), monotonicMs());
    if (result.outcome == ServerInfoSession::Outcome::ignored) return;
    gtk_widget_set_sensitive(refresh_, TRUE);
    syncAutoTimer();
    if (result.outcome == ServerInfoSession::Outcome::fatal) {
        const auto generation = session_.generation();
        showError(window(), result.text, "PHPVPN");
        if (session_.generation() == generation) endSession();
        return;
    }
    if (result.outcome == ServerInfoSession::Outcome::retained) {
        setNotice(result.text);
        return;
    }
    GtkWidget* focus = gtk_window_get_focus(GTK_WINDOW(window_));
    const bool focusOnPager = focus && gtk_widget_is_ancestor(focus, pagerBox_);
    setNotice("");
    if (result.rawChanged) setRaw();
    display();
    if (result.selectedRow) selectRow(*result.selectedRow);
    if (result.outcome == ServerInfoSession::Outcome::first) {
        gtk_widget_show(window_);
        gtk_window_present(GTK_WINDOW(window_));
        gtk_widget_grab_focus(tabs_);  // the first tab has the focus
    } else if (focusOnPager) {
        gtk_widget_grab_focus(table_);  // the focused pager button was rebuilt
    }
}

void ServerInfoWindow::display() {
    if (!session_.model()) return;
    const auto title = session_.title();
    gtk_window_set_title(GTK_WINDOW(window_), title.c_str());
    gtk_label_set_text(GTK_LABEL(count_), session_.countText().c_str());
    gtk_list_store_clear(rows_);
    for (const auto& row : session_.pageRows()) {
        GtkTreeIter iter;
        gtk_list_store_append(rows_, &iter);
        gtk_list_store_set(rows_, &iter, contractColumn, row.contract.c_str(), lastSeenColumn, row.lastSeen.c_str(),
                           statusColumn, row.status.c_str(), backgroundColumn, rowBackground(row.state), -1);
    }
    rebuildPager();
}

void ServerInfoWindow::rebuildPager() {
    GList* children = gtk_container_get_children(GTK_CONTAINER(pagerBox_));
    for (GList* item = children; item; item = item->next) gtk_widget_destroy(GTK_WIDGET(item->data));
    g_list_free(children);
    struct Binding { ServerInfoWindow* owner; int page; };
    for (const auto& entry : session_.pager()) {
        GtkWidget* widget = button(entry.label, entry.current ? style::kPrimaryButton : style::kSecondaryButton);
        gtk_widget_set_sensitive(widget, entry.enabled);
        if (!entry.accessibleName.empty())
            atk_object_set_name(gtk_widget_get_accessible(widget), entry.accessibleName.c_str());
        if (entry.page >= 0)
            g_signal_connect_data(widget, "clicked", gtkCallback([](GtkButton*, gpointer data) {
                auto* binding = static_cast<Binding*>(data);
                binding->owner->showPage(binding->page);
            }), new Binding{this, entry.page}, [](gpointer data, GClosure*) { delete static_cast<Binding*>(data); },
            GConnectFlags(0));
        gtk_box_pack_start(GTK_BOX(pagerBox_), widget, FALSE, FALSE, 0);
        gtk_widget_show(widget);
    }
    gtk_widget_set_visible(pagerBox_, !session_.pager().empty());
}

void ServerInfoWindow::showPage(int page) {
    if (!session_.model()) return;
    session_.setPage(page);
    display();
    gtk_widget_grab_focus(table_);
}

void ServerInfoWindow::setNotice(std::string_view text) {
    if (!notice_) return;
    gtk_label_set_text(GTK_LABEL(notice_), std::string(text).c_str());
    gtk_widget_set_visible(notice_, !text.empty());
}

void ServerInfoWindow::setRaw() {
    const auto& plaintext = session_.model()->snapshot.infoPlaintext;
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(raw_));
    // Keep the selection offsets and the first visible line across a replacement.
    GtkTextIter start, end;
    gtk_text_buffer_get_selection_bounds(buffer, &start, &end);
    const int selectionStart = gtk_text_iter_get_offset(&start), selectionEnd = gtk_text_iter_get_offset(&end);
    GdkRectangle visible{};
    gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(raw_), &visible);
    GtkTextIter top;
    gtk_text_view_get_line_at_y(GTK_TEXT_VIEW(raw_), &top, visible.y, nullptr);
    const int firstLine = gtk_text_iter_get_line(&top);
    // The validated plaintext is strict UTF-8 JSON; the check only protects GTK.
    if (g_utf8_validate(plaintext.data(), static_cast<gssize>(plaintext.size()), nullptr)) {
        gtk_text_buffer_set_text(buffer, plaintext.data(), static_cast<gint>(plaintext.size()));
    } else {
        gchar* valid = g_utf8_make_valid(plaintext.data(), static_cast<gssize>(plaintext.size()));
        gtk_text_buffer_set_text(buffer, valid, -1);
        g_free(valid);
    }
    const int characters = gtk_text_buffer_get_char_count(buffer);
    gtk_text_buffer_get_iter_at_offset(buffer, &start, std::min(selectionStart, characters));
    gtk_text_buffer_get_iter_at_offset(buffer, &end, std::min(selectionEnd, characters));
    gtk_text_buffer_select_range(buffer, &end, &start);
    GtkTextIter line;
    gtk_text_buffer_get_iter_at_line(buffer, &line, firstLine);
    GtkTextMark* mark = gtk_text_buffer_create_mark(buffer, nullptr, &line, TRUE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(raw_), mark, 0.0, TRUE, 0.0, 0.0);
    gtk_text_buffer_delete_mark(buffer, mark);
}

void ServerInfoWindow::syncAutoTimer() {
    if (autoTimer_) g_source_remove(autoTimer_);
    autoTimer_ = 0;
    const auto deadline = session_.autoDeadline();
    if (!window_ || !deadline) return;
    const auto remaining = std::max<std::int64_t>(0, *deadline - monotonicMs());
    autoTimer_ = g_timeout_add(static_cast<guint>(remaining), [](gpointer data) -> gboolean {
        auto* self = static_cast<ServerInfoWindow*>(data);
        self->autoTimer_ = 0;
        self->onAutoTimer();
        return G_SOURCE_REMOVE;
    }, this);
}

void ServerInfoWindow::onAutoTimer() {
    // The same path as Refresh, on both tabs; an early tick keeps its deadline.
    if (session_.autoDue(monotonicMs())) request();
    else syncAutoTimer();
}

void ServerInfoWindow::placeWrapped() {
    if (!content_) return;
    const int width = gtk_widget_get_allocated_width(content_);
    if (width <= 1) return;
    const bool header = naturalWidth(heading_) + ui::fieldGap + naturalWidth(count_) > width;
    if (header != headerWrapped_) {
        headerWrapped_ = header;
        orient(header_, header);
    }
    const bool footer = naturalWidth(footerPair_) + ui::fieldGap + naturalWidth(ok_) > width;
    if (footer != footerWrapped_) {
        footerWrapped_ = footer;
        orient(footer_, footer);  // OK below the Refresh / Auto refresh pair, still trailing
    }
}

bool ServerInfoWindow::onKey(GdkEventKey* event) {
    GtkWidget* focus = gtk_window_get_focus(GTK_WINDOW(window_));
    if (event->keyval == GDK_KEY_Escape) {
        endSession();
        return true;
    }
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter || event->keyval == GDK_KEY_ISO_Enter) {
        // Enter activates the focused button (natively), does nothing special in
        // the raw editor and closes the window everywhere else.
        if (focus && (focus == raw_ || GTK_IS_BUTTON(focus))) return false;
        endSession();
        return true;
    }
    if (event->keyval == GDK_KEY_End && (event->state & GDK_CONTROL_MASK) && focus == table_ && session_.model()) {
        showPage(session_.pageCount() - 1);
        const auto rows = session_.pageRows().size();
        if (rows) selectRow(rows - 1);
        return true;
    }
    return false;
}

std::optional<std::size_t> ServerInfoWindow::selectedRow() const {
    if (!table_) return std::nullopt;
    GtkTreeModel* model = nullptr;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(table_)), &model, &iter))
        return std::nullopt;
    GtkTreePath* path = gtk_tree_model_get_path(model, &iter);
    const int index = path ? gtk_tree_path_get_indices(path)[0] : -1;
    if (path) gtk_tree_path_free(path);
    return index >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(index)) : std::nullopt;
}

void ServerInfoWindow::selectRow(std::size_t row) {
    GtkTreePath* path = gtk_tree_path_new_from_indices(static_cast<int>(row), -1);
    gtk_tree_view_set_cursor(GTK_TREE_VIEW(table_), path, nullptr, FALSE);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(table_), path, nullptr, FALSE, 0.0f, 0.0f);
    gtk_tree_path_free(path);
}

void installServerInfoWindow(TrayApp& app) {
    auto window = std::make_shared<ServerInfoWindow>(app.endpoint(), app.dispatcher());
    ServerInfoWindow* raw = window.get();
    app.registerParticipant({raw, [raw] { return raw->window(); }});
    app.hooks().openServerInfo = [window] { window->open(); };
}
}  // namespace phpvpn::tray
