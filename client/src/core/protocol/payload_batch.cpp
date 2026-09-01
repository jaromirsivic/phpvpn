#include "core/protocol/payload_batch.h"

namespace phpvpn {

std::size_t PayloadBatch::encodedSize(std::span<const std::size_t> packetSizes) noexcept {
    std::size_t size = kHeaderBytes + packetSizes.size() * kLengthEntryBytes;
    for (std::size_t packetSize : packetSizes) {
        size += packetSize;
    }
    return size;
}

std::size_t PayloadBatch::encodedSizeForSinglePacket(std::size_t packetSize) noexcept {
    return kHeaderBytes + kLengthEntryBytes + packetSize;
}

Result<std::vector<std::uint8_t>> PayloadBatch::encode(std::span<const PacketBytes> packets) {
    if (packets.empty()) {
        return Error(ErrorCode::wire_invalid_batch, "packetCount must be at least 1");
    }
    if (packets.size() > 0xFFFFFFFFull) {
        return Error(ErrorCode::wire_invalid_batch, "packetCount overflow");
    }

    std::size_t total = kHeaderBytes + packets.size() * kLengthEntryBytes;
    for (const PacketBytes& packet : packets) {
        if (packet.size() < kMinPacketBytes || packet.size() > kMaxPacketBytes) {
            return Error(ErrorCode::wire_invalid_batch, "packet length out of range");
        }
        total += packet.size();
    }

    std::vector<std::uint8_t> out;
    out.reserve(total);

    const auto count = static_cast<std::uint32_t>(packets.size());
    out.push_back(static_cast<std::uint8_t>((count >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((count >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((count >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(count & 0xFFu));

    for (const PacketBytes& packet : packets) {
        const auto length = static_cast<std::uint16_t>(packet.size());
        out.push_back(static_cast<std::uint8_t>((length >> 8) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>(length & 0xFFu));
    }
    for (const PacketBytes& packet : packets) {
        out.insert(out.end(), packet.begin(), packet.end());
    }

    return out;
}

Result<std::vector<PacketBytes>> PayloadBatch::decode(std::span<const std::uint8_t> plaintext) {
    if (plaintext.size() < kHeaderBytes) {
        return Error(ErrorCode::wire_invalid_batch, "batch shorter than the count header");
    }

    const std::uint32_t count = (static_cast<std::uint32_t>(plaintext[0]) << 24) |
                                (static_cast<std::uint32_t>(plaintext[1]) << 16) |
                                (static_cast<std::uint32_t>(plaintext[2]) << 8) |
                                static_cast<std::uint32_t>(plaintext[3]);
    if (count == 0) {
        return Error(ErrorCode::wire_invalid_batch, "packetCount must be at least 1");
    }

    // The length table is checked against the remaining bytes before it is read,
    // so an inflated packetCount can never drive an allocation.
    const std::size_t tableBytes = static_cast<std::size_t>(count) * kLengthEntryBytes;
    if (plaintext.size() < kHeaderBytes + tableBytes) {
        return Error(ErrorCode::wire_invalid_batch, "length table does not fit the batch");
    }

    std::vector<std::size_t> lengths;
    lengths.reserve(count);
    std::size_t payloadBytes = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t offset = kHeaderBytes + static_cast<std::size_t>(i) * kLengthEntryBytes;
        const std::size_t length = (static_cast<std::size_t>(plaintext[offset]) << 8) |
                                   static_cast<std::size_t>(plaintext[offset + 1]);
        if (length < kMinPacketBytes) {
            return Error(ErrorCode::wire_invalid_batch, "packet length below the IPv4 minimum");
        }
        lengths.push_back(length);
        payloadBytes += length;
    }

    if (plaintext.size() != kHeaderBytes + tableBytes + payloadBytes) {
        return Error(ErrorCode::wire_invalid_batch, "batch length does not match the table");
    }

    std::vector<PacketBytes> packets;
    packets.reserve(count);
    std::size_t offset = kHeaderBytes + tableBytes;
    for (std::size_t length : lengths) {
        packets.emplace_back(plaintext.begin() + static_cast<std::ptrdiff_t>(offset),
                             plaintext.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
    }

    return packets;
}

Result<std::vector<PacketBytes>> PayloadBatch::decodeAndValidate(
    std::span<const std::uint8_t> plaintext,
    Ipv4Address expectedInnerSource,
    Ipv4Address expectedInnerDestination) {
    auto decoded = decode(plaintext);
    if (!decoded.ok()) {
        return Error(decoded.error());
    }

    for (const PacketBytes& packet : decoded.value()) {
        Ipv4PacketView view;
        if (!validateIpv4Packet(packet, view)) {
            return Error(ErrorCode::wire_invalid_packet, "IPv4 structure or checksum invalid");
        }
        if (view.source != expectedInnerSource || view.destination != expectedInnerDestination) {
            return Error(ErrorCode::wire_invalid_packet,
                         "inner addresses do not match the routing fields");
        }
    }

    return std::move(decoded).value();
}

}  // namespace phpvpn
