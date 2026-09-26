// Small GLib/GTK helpers shared by the tray windows.
#pragma once
#include <glib-object.h>

#include <functional>
#include <vector>

namespace phpvpn::tray {
// Captureless lambda as a signal handler. G_CALLBACK is a macro and cannot take a
// lambda with commas; GCallback (void (*)(void)) is the generic function type.
template <class F>
GCallback gtkCallback(F function) {
    return reinterpret_cast<GCallback>(+function);
}

// Runs UI actions that may open a modal question from an idle callback. GTK runs a
// button's accessibility "click" action inside the AT-SPI D-Bus call; a nested
// dialog loop there would stall the assistive technology's call and every further
// accessibility request of the tray. Every "clicked"/"activate" handler that can
// reach confirm/showMessage/showError posts its action here. Pending calls are
// removed with the owner.
class DeferredCalls {
public:
    DeferredCalls() = default;
    DeferredCalls(const DeferredCalls&) = delete;
    DeferredCalls& operator=(const DeferredCalls&) = delete;
    ~DeferredCalls() {
        for (auto* entry : pending_) g_source_remove(entry->source);
    }
    void post(std::function<void()> call) {
        auto* entry = new Entry{this, std::move(call), 0};
        entry->source = g_idle_add_full(G_PRIORITY_DEFAULT, [](gpointer raw) -> gboolean {
            auto* current = static_cast<Entry*>(raw);
            std::erase(current->owner->pending_, current);
            auto call = std::move(current->call);
            current->source = 0;
            if (call) call();
            return G_SOURCE_REMOVE;
        }, entry, [](gpointer raw) { delete static_cast<Entry*>(raw); });
        pending_.push_back(entry);
    }

private:
    struct Entry {
        DeferredCalls* owner;
        std::function<void()> call;
        guint source;
    };
    std::vector<Entry*> pending_;
};
}  // namespace phpvpn::tray
