#pragma once
#include <atomic>
#include <functional>
#include "core/config/config_store.h"
#include "core/runtime/vpn_engine.h"

namespace phpvpn {
// One request per runtime. The owner cancels and drains its executor before
// destroying this object or either dependency. tick() never reads the file.
class StartupConnect {
public:
    using Post = std::function<bool(std::function<void()>)>;
    StartupConnect(ConfigStore& store, VpnEngine& engine)
        : store_(store), engine_(engine), token_(engine.startupControlToken()), epoch_(engine.connectionIdentity().epoch) {}
    void request(const Post& post) { armed_.store(true); tick(post); }
    void tick(const Post& post);
    void cancel() noexcept { cancelled_.store(true); }
private:
    void execute() noexcept;
    ConfigStore& store_;
    VpnEngine& engine_;
    const std::uint64_t token_;
    const std::uint64_t epoch_;
    std::atomic<bool> armed_{false}, queued_{false}, cancelled_{false};
};
}
