// The two tunnel roles, client.md "Sekvencni I/O a vlastnictvi paketu".
//
// The input worker is the only reader of the virtual interface, the output
// worker is the only writer. Each owns one thread and one packet at a time, and
// neither ever holds a pointer into the driver ring: a received packet is copied
// out before the ring entry is released, and a sent packet is copied in.
//
// A packet is validated fully before it reaches a queue. The outbound direction
// additionally requires that the source is the local VPN address and that a
// communication contract exists for the destination.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

#include "core/config/config.h"
#include "core/queues/packet_queues.h"
#include "platform/iface/tunnel.h"

namespace phpvpn {

struct TunnelWorkerCounters {
    std::atomic<std::uint64_t> packetsRead{0};
    std::atomic<std::uint64_t> packetsWritten{0};
    std::atomic<std::uint64_t> packetsDropped{0};
    std::atomic<std::uint64_t> writeFailures{0};
};

// Reads from the virtual interface into the outbound queue.
class TunnelInputWorker {
public:
    TunnelInputWorker(ClientConfigPtr config,
                      std::shared_ptr<Tunnel> tunnel,
                      DirectionQueues& outbound,
                      DropCounters& drops,
                      TunnelWorkerCounters& counters);
    ~TunnelInputWorker();

    TunnelInputWorker(const TunnelInputWorker&) = delete;
    TunnelInputWorker& operator=(const TunnelInputWorker&) = delete;

    void start();
    void stop();

private:
    void run();

    ClientConfigPtr config_;
    std::shared_ptr<Tunnel> tunnel_;
    DirectionQueues& outbound_;
    DropCounters& drops_;
    TunnelWorkerCounters& counters_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

// Writes packets from the inbound queue into the virtual interface.
class TunnelOutputWorker {
public:
    TunnelOutputWorker(std::shared_ptr<Tunnel> tunnel,
                       DirectionQueues& inbound,
                       DropCounters& drops,
                       TunnelWorkerCounters& counters);
    ~TunnelOutputWorker();

    TunnelOutputWorker(const TunnelOutputWorker&) = delete;
    TunnelOutputWorker& operator=(const TunnelOutputWorker&) = delete;

    void start();
    void stop();

    // Wakes the worker as soon as the network worker queued new payload.
    void notifyDataAvailable();

private:
    void run();

    std::shared_ptr<Tunnel> tunnel_;
    DirectionQueues& inbound_;
    DropCounters& drops_;
    TunnelWorkerCounters& counters_;
    std::atomic<bool> running_{false};
    std::thread thread_;

    std::mutex wakeMutex_;
    std::condition_variable wake_;
    bool wakeFlag_ = false;
};

}  // namespace phpvpn
