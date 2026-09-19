#pragma once
#include <condition_variable>
#include <functional>
#include "platform/iface/tunnel.h"
#include "core/protocol/payload_batch.h"
namespace phpvpn::test {
class RecordingTunnel final : public Tunnel {
public:
    std::function<Status(std::span<const std::uint8_t>)> onSend;
    Result<TunnelReadResult> receive(std::span<std::uint8_t>,std::int64_t) override {
        return TunnelReadResult{TunnelReadStatus::stopped,0};
    }
    Status send(std::span<const std::uint8_t> packet) override {
        auto result=onSend?onSend(packet):ok();
        {
            std::lock_guard lock(mutex_);
            sent_.emplace_back(packet.begin(),packet.end());
            if(!result) ++failures_;
        }
        changed_.notify_all();
        return result;
    }
    void signalStop() override { changed_.notify_all(); }
    std::uint64_t oversizedReceiveCount() const override { return 0; }
    bool awaitCount(std::size_t count) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock,std::chrono::seconds(5),[&]{return sent_.size()>=count;});
    }
    std::vector<PacketBytes> packets() { std::lock_guard lock(mutex_);return sent_; }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<PacketBytes> sent_;
    std::size_t failures_=0;
};
} // namespace phpvpn::test
