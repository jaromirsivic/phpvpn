// Tunnel over one packet descriptor without framing, shared by Linux and
// Android (plan-android-v001, native part).
//
// The Linux /dev/net/tun descriptor with IFF_NO_PI and the Android
// VpnService.Builder.establish() descriptor behave the same way: one read
// returns one IPv4 packet, one write sends one. Only the creation of the
// interface differs, so the driver owns the descriptor and this class only
// moves packets. macOS utun prefixes every packet with the protocol family and
// keeps its own class.
#pragma once

#include <atomic>
#include <cstdint>
#include <shared_mutex>
#include <span>

#include "platform/iface/tunnel.h"
#include "platform/posix/fd.h"

namespace phpvpn {

class FdTunnel final : public Tunnel {
public:
    // `fd` stays owned by the driver and must be non-blocking. `stopEvent` is
    // a non-blocking eventfd, owned by the tunnel.
    FdTunnel(int fd, UniqueFd stopEvent);

    Result<TunnelReadResult> receive(std::span<std::uint8_t> buffer, std::int64_t timeoutMs) override;
    Status send(std::span<const std::uint8_t> packet) override;
    void signalStop() override;
    std::uint64_t oversizedReceiveCount() const override;

    // Called by the driver after signalStop: waits until no receive or send
    // uses the descriptor any more, so the driver can close it safely even
    // while a worker still holds this object.
    void detach();

private:
    std::shared_mutex ioMutex_;
    int fd_ = -1;  // owned by the driver
    UniqueFd stopEvent_;
    std::atomic<bool> stopped_{false};
    std::atomic<std::uint64_t> oversized_{0};
};

}  // namespace phpvpn
