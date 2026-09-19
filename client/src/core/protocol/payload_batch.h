// Binary batch of IPv4 packets, protocol.md chapter 9.4.
//
//   packetCount                    : unsigned 32-bit big endian, at least 1
//   packetLength[0..count-1]       : unsigned 16-bit big endian, 20 to 65535
//   packetBytes[0..count-1]        : exactly packetLength[i] bytes
//
// The sum of the four header bytes, all two byte lengths and all packet bytes
// must equal the plaintext length exactly. Neither trailing data nor a partial
// entry is allowed, and the decoder never allocates from an unverified value.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/base/result.h"
#include "core/protocol/ipv4.h"

namespace phpvpn {

// One decoded packet expressed as a copy, because the plaintext buffer is
// wiped as soon as validation finishes.
using PacketBytes = std::vector<std::uint8_t>;

class PayloadBatch {
public:
    static constexpr std::size_t kHeaderBytes = 4;
    static constexpr std::size_t kLengthEntryBytes = 2;
    static constexpr std::size_t kMinPacketBytes = 20;
    static constexpr std::size_t kMaxPacketBytes = kMaxTunnelPacketBytes;

    // Exact serialised size of a batch holding packets of the given sizes.
    static std::size_t encodedSize(std::span<const std::size_t> packetSizes) noexcept;
    static std::size_t encodedSizeForSinglePacket(std::size_t packetSize) noexcept;

    // Serialises the packets in order. Fails when the batch is empty or any
    // packet is outside the normative length range.
    static Result<std::vector<std::uint8_t>> encode(std::span<const PacketBytes> packets);

    static Result<std::vector<std::uint8_t>> encodeViews(
        std::span<const std::span<const std::uint8_t>> packets);

    // Structural decode only. The caller still has to run the address checks of
    // client.md before anything reaches a queue.
    static Result<std::vector<PacketBytes>> decode(std::span<const std::uint8_t> plaintext);

    // Structural decode plus the full IPv4 validation and the inner address
    // binding required by chapter 9.4 steps 5 and 6. A single failing packet
    // invalidates the whole batch, so nothing is returned partially.
    static Result<std::vector<PacketBytes>> decodeAndValidate(
        std::span<const std::uint8_t> plaintext,
        Ipv4Address expectedInnerSource,
        Ipv4Address expectedInnerDestination);
};

}  // namespace phpvpn
