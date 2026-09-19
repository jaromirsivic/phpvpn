// IPv4 addresses, the shared VPN prefix and full packet validation.
//
// client.md chapter "Uplna validace vnitrnich IPv4 paketu" defines the packet
// rules. plan_client.md documented decision 1 defines the set of valid client
// addresses of a general prefix: the network address is reserved, the first host
// address is reserved for a gateway or relay and the last address is broadcast,
// so valid client addresses run from network+2 to broadcast-1.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace phpvpn {
inline constexpr std::size_t kMaxTunnelPacketBytes = 65535;

// Host order IPv4 address with the exact decimal spelling required by the
// protocol, that is four parts without leading zeros.
class Ipv4Address {
public:
    Ipv4Address() = default;
    explicit Ipv4Address(std::uint32_t value) : value_(value) {}

    static std::optional<Ipv4Address> parse(std::string_view text) noexcept;

    std::uint32_t value() const noexcept { return value_; }
    std::string toString() const;

    // Big endian bytes as they appear inside an IPv4 header.
    void toNetworkBytes(std::uint8_t out[4]) const noexcept;
    static Ipv4Address fromNetworkBytes(const std::uint8_t bytes[4]) noexcept;

    friend bool operator==(Ipv4Address a, Ipv4Address b) noexcept { return a.value_ == b.value_; }
    friend bool operator!=(Ipv4Address a, Ipv4Address b) noexcept { return a.value_ != b.value_; }
    friend bool operator<(Ipv4Address a, Ipv4Address b) noexcept { return a.value_ < b.value_; }

private:
    std::uint32_t value_ = 0;
};

class Ipv4Cidr {
public:
    Ipv4Cidr() = default;

    // Accepts only the canonical spelling A.B.C.D/len with zero host bits and a
    // prefix length between 24 and 29. This globally bounds remote peers to
    // 252. /29 still has five client addresses and four remote peers.
    static std::optional<Ipv4Cidr> parse(std::string_view text) noexcept;

    static constexpr int kMinPrefixLength = 24;
    static constexpr int kMaxPrefixLength = 29;

    Ipv4Address network() const noexcept { return network_; }
    int prefixLength() const noexcept { return prefixLength_; }

    Ipv4Address broadcast() const noexcept;
    Ipv4Address firstClientAddress() const noexcept;
    Ipv4Address lastClientAddress() const noexcept;

    // Total number of valid client addresses in this prefix.
    std::uint32_t clientAddressCount() const noexcept;

    bool contains(Ipv4Address address) const noexcept;
    bool isValidClientAddress(Ipv4Address address) const noexcept;

    std::string toString() const;

private:
    Ipv4Address network_;
    int prefixLength_ = 0;
};

// Read only view over one complete IPv4 packet taken from the tunnel or from a
// decrypted batch.
struct Ipv4PacketView {
    Ipv4Address source;
    Ipv4Address destination;
    std::size_t headerLength = 0;
    std::size_t totalLength = 0;
    std::uint8_t protocol = 0;
    std::uint16_t fragmentField = 0;
};

// Full structural validation: at least 20 bytes, version 4, IHL at least 5, the
// header inside the buffer, `total length` equal to the buffer length and a
// valid header checksum. Every field is bounds checked before it is read.
bool validateIpv4Packet(std::span<const std::uint8_t> packet, Ipv4PacketView& out) noexcept;

// Ones complement checksum over the IPv4 header, used by the validator and by
// the tests that build synthetic packets.
std::uint16_t ipv4HeaderChecksum(std::span<const std::uint8_t> header) noexcept;

}  // namespace phpvpn
