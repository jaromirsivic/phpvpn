#include "core/runtime/tunnel_workers.h"

#include <chrono>
#include <utility>
#include <vector>

#include "core/base/logger.h"

namespace phpvpn {
namespace {

constexpr const char* kInputComponent = "tunnel-in";
constexpr const char* kOutputComponent = "tunnel-out";

// The largest IPv4 packet the protocol allows in a batch. The receive buffer is
// allocated once per worker, so no allocation happens on the packet path.
constexpr std::size_t kReceiveBufferBytes = 65535;

// Bounded wait so the worker notices a stop request even when the ring stays
// empty and no event is signalled.
constexpr std::int64_t kReceiveTimeoutMs = 250;
constexpr std::int64_t kOutputIdleWaitMs = 50;

}  // namespace

TunnelInputWorker::TunnelInputWorker(ClientConfigPtr config,
                                     std::shared_ptr<Tunnel> tunnel,
                                     DirectionQueues& outbound,
                                     DropCounters& drops,
                                     TunnelWorkerCounters& counters)
    : config_(std::move(config)),
      tunnel_(std::move(tunnel)),
      outbound_(outbound),
      drops_(drops),
      counters_(counters) {}

TunnelInputWorker::~TunnelInputWorker() {
    stop();
}

void TunnelInputWorker::start() {
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::thread([this] { run(); });
}

void TunnelInputWorker::stop() {
    if (!running_.exchange(false)) {
        if (thread_.joinable()) {
            thread_.join();
        }
        return;
    }
    if (tunnel_) {
        tunnel_->signalStop();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void TunnelInputWorker::run() {
    std::vector<std::uint8_t> buffer(kReceiveBufferBytes);

    while (running_.load()) {
        auto read = tunnel_->receive(std::span<std::uint8_t>(buffer.data(), buffer.size()),
                                     kReceiveTimeoutMs);
        if (!read.ok()) {
            PHPVPN_LOG_ERROR(kInputComponent, read.error().message());
            break;
        }
        if (read.value().status == TunnelReadStatus::stopped) {
            break;
        }
        if (read.value().status != TunnelReadStatus::packet) {
            continue;
        }

        const std::span<const std::uint8_t> packet(buffer.data(), read.value().size);
        counters_.packetsRead.fetch_add(1);

        Ipv4PacketView view;
        if (!validateIpv4Packet(packet, view)) {
            counters_.packetsDropped.fetch_add(1);
            drops_.record(Ipv4Address(0), DropReason::invalid_packet);
            continue;
        }

        // A packet whose source is not the local VPN address never leaves the
        // client, and a destination without a contract has nowhere to go.
        if (view.source != config_->virtualIp) {
            counters_.packetsDropped.fetch_add(1);
            drops_.record(view.destination, DropReason::invalid_packet);
            continue;
        }
        if (config_->findContract(view.destination) == nullptr) {
            counters_.packetsDropped.fetch_add(1);
            drops_.record(view.destination, DropReason::no_contract);
            continue;
        }

        PacketBytes owned(packet.begin(), packet.end());
        DropReason reason = DropReason::global_limit;
        if (!outbound_.tryEnqueue(view.destination, std::move(owned), reason)) {
            counters_.packetsDropped.fetch_add(1);
            drops_.record(view.destination, reason);
        }
    }

    running_.store(false);
}

TunnelOutputWorker::TunnelOutputWorker(std::shared_ptr<Tunnel> tunnel,
                                       DirectionQueues& inbound,
                                       DropCounters& drops,
                                       TunnelWorkerCounters& counters)
    : tunnel_(std::move(tunnel)), inbound_(inbound), drops_(drops), counters_(counters) {}

TunnelOutputWorker::~TunnelOutputWorker() {
    stop();
}

void TunnelOutputWorker::start() {
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::thread([this] { run(); });
}

void TunnelOutputWorker::stop() {
    if (!running_.exchange(false)) {
        if (thread_.joinable()) {
            thread_.join();
        }
        return;
    }
    if (tunnel_) {
        tunnel_->signalStop();
    }
    notifyDataAvailable();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void TunnelOutputWorker::notifyDataAvailable() {
    {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        wakeFlag_ = true;
    }
    wake_.notify_one();
}

void TunnelOutputWorker::run() {
    while (running_.load()) {
        bool wroteSomething = false;

        // One pass over every non empty queue keeps the peers fair, and the
        // worker owns exactly one packet at a time.
        for (Ipv4Address peer : inbound_.nonEmptyPeers()) {
            if (!running_.load()) {
                break;
            }
            auto packet = inbound_.popFront(peer);
            if (!packet.has_value()) {
                continue;
            }

            auto sent = tunnel_->send(std::span<const std::uint8_t>(packet->data(), packet->size()));
            if (sent.ok()) {
                counters_.packetsWritten.fetch_add(1);
                wroteSomething = true;
            } else {
                counters_.writeFailures.fetch_add(1);
                counters_.packetsDropped.fetch_add(1);
                drops_.record(peer, DropReason::tunnel_stopped);
                if (sent.error().code == ErrorCode::tunnel_io_failed) {
                    PHPVPN_LOG_ERROR(kOutputComponent, sent.error().message());
                    running_.store(false);
                    break;
                }
            }
        }

        if (wroteSomething) {
            continue;
        }

        std::unique_lock<std::mutex> lock(wakeMutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(kOutputIdleWaitMs),
                       [this] { return wakeFlag_ || !running_.load(); });
        wakeFlag_ = false;
    }

    running_.store(false);
}

}  // namespace phpvpn
