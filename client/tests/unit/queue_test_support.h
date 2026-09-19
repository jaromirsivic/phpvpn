#pragma once
#include <atomic>
#include <bit>
#include "core/queues/packet_queues.h"

namespace phpvpn::test {
class QueueClock final : public Clock {
public:
    std::atomic<std::uint64_t> now{0};
    std::atomic<std::int64_t> wall{0};
    std::int64_t monotonicMs() override { return std::bit_cast<std::int64_t>(now.load()); }
    std::int64_t systemUtcMs() override { return wall.load(); }
};
class FixedFlowHasher final : public FlowHasher {
public:
    std::atomic<unsigned> calls{0};
    std::uint8_t index = 0;
    bool fail = false;
    Result<std::uint8_t> bucket(const FlowKey&) override {
        ++calls;
        if (fail) return Error(ErrorCode::crypto_failed, "injected flow hash failure");
        return index;
    }
};
struct QueueContext { QueueClock clock; FixedFlowHasher hasher; DropCounters drops; };
inline void checksum(PacketBytes& packet) {
    packet[10] = packet[11] = 0;
    const auto value = ipv4HeaderChecksum(std::span<const std::uint8_t>(packet.data(), (packet[0] & 15) * 4));
    packet[10] = static_cast<std::uint8_t>(value >> 8);
    packet[11] = static_cast<std::uint8_t>(value);
}
inline PacketBytes flowPacket(std::size_t size = 100, std::uint16_t flow = 1,
    std::uint16_t id = 0, std::uint8_t protocol = 17, std::uint16_t fragments = 0,
    std::size_t headerBytes = 20) {
    if (size < headerBytes || size > kMaxTunnelPacketBytes) return PacketBytes(size, 0);
    PacketBytes bytes(size, 0);
    bytes[0] = static_cast<std::uint8_t>(0x40 | (headerBytes / 4));
    bytes[2] = static_cast<std::uint8_t>(size >> 8); bytes[3] = static_cast<std::uint8_t>(size);
    bytes[4] = static_cast<std::uint8_t>(id >> 8); bytes[5] = static_cast<std::uint8_t>(id);
    bytes[6] = static_cast<std::uint8_t>(fragments >> 8); bytes[7] = static_cast<std::uint8_t>(fragments);
    bytes[8] = 64; bytes[9] = protocol;
    Ipv4Address(0x0a636302).toNetworkBytes(bytes.data() + 12);
    Ipv4Address(0x0a636303).toNetworkBytes(bytes.data() + 16);
    if (size >= headerBytes + 4) {
        bytes[headerBytes] = static_cast<std::uint8_t>(flow >> 8);
        bytes[headerBytes + 1] = static_cast<std::uint8_t>(flow);
        bytes[headerBytes + 2] = 0x01; bytes[headerBytes + 3] = 0xbb;
    }
    if (protocol == 17 && size >= headerBytes + 8) {
        bytes[headerBytes + 4] = static_cast<std::uint8_t>((size - headerBytes) >> 8);
        bytes[headerBytes + 5] = static_cast<std::uint8_t>(size - headerBytes);
    }
    if (protocol == 6 && size >= headerBytes + 20) bytes[headerBytes + 12] = 0x50;
    checksum(bytes);
    return bytes;
}
inline FlowKey keyOf(const PacketBytes& bytes) {
    Ipv4PacketView view;
    if (!validateIpv4Packet(bytes, view)) return {};
    return flowKeyFromValidated(bytes, view);
}
inline std::uint16_t packetId(const PacketBytes& bytes) {
    return static_cast<std::uint16_t>((bytes[4] << 8) | bytes[5]);
}
} // namespace phpvpn::test
