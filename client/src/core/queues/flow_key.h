#pragma once
#include <array>
#include "core/crypto/crypto_provider.h"
#include "core/protocol/ipv4.h"

namespace phpvpn {
struct FlowKey {
    Ipv4Address source, destination;
    std::uint16_t sourcePort = 0, destinationPort = 0;
    std::uint8_t protocol = 0;
    bool operator==(const FlowKey&) const = default;
    std::array<std::uint8_t, 13> serialize() const noexcept;
};
// Requires the view produced by validateIpv4Packet for this immutable packet.
FlowKey flowKeyFromValidated(std::span<const std::uint8_t> packet,
                             const Ipv4PacketView& view) noexcept;
class FlowHasher {
public:
    virtual ~FlowHasher() = default;
    virtual Result<std::uint8_t> bucket(const FlowKey& key) = 0;
};
class HmacFlowHasher final : public FlowHasher {
public:
    HmacFlowHasher(CryptoProvider& crypto, SecureBuffer key)
        : crypto_(crypto), key_(std::move(key)) {}
    static Result<std::unique_ptr<HmacFlowHasher>> create(CryptoProvider& crypto);
    Result<std::uint8_t> bucket(const FlowKey& key) override;
private:
    CryptoProvider& crypto_;
    SecureBuffer key_;
};
} // namespace phpvpn
