#include "core/queues/flow_key.h"

namespace phpvpn {
std::array<std::uint8_t, 13> FlowKey::serialize() const noexcept {
    std::array<std::uint8_t, 13> bytes{};
    source.toNetworkBytes(bytes.data());
    destination.toNetworkBytes(bytes.data() + 4);
    bytes[8] = static_cast<std::uint8_t>(sourcePort >> 8);
    bytes[9] = static_cast<std::uint8_t>(sourcePort);
    bytes[10] = static_cast<std::uint8_t>(destinationPort >> 8);
    bytes[11] = static_cast<std::uint8_t>(destinationPort);
    bytes[12] = protocol;
    return bytes;
}
FlowKey flowKeyFromValidated(std::span<const std::uint8_t> packet,
                             const Ipv4PacketView& view) noexcept {
    FlowKey key{view.source, view.destination, 0, 0, view.protocol};
    if ((view.fragmentField & 0x3fff) != 0 || view.totalLength > packet.size() ||
        view.headerLength > view.totalLength) return key;
    const auto transport = packet.subspan(view.headerLength, view.totalLength - view.headerLength);
    if (view.protocol == 6) {
        if (transport.size() < 20) return key;
        const auto length = static_cast<std::size_t>(transport[12] >> 4) * 4;
        if (length < 20 || length > transport.size()) return key;
    } else if (view.protocol == 17) {
        if (transport.size() < 8) return key;
        const auto length = static_cast<std::size_t>((transport[4] << 8) | transport[5]);
        if (length < 8 || length != transport.size()) return key;
    } else return key;
    key.sourcePort = static_cast<std::uint16_t>((transport[0] << 8) | transport[1]);
    key.destinationPort = static_cast<std::uint16_t>((transport[2] << 8) | transport[3]);
    return key;
}
Result<std::unique_ptr<HmacFlowHasher>> HmacFlowHasher::create(CryptoProvider& crypto) {
    SecureBuffer key(32);
    PHPVPN_TRY(crypto.randomBytes(std::span<std::uint8_t>(key.data(), key.size())));
    return std::make_unique<HmacFlowHasher>(crypto, std::move(key));
}
Result<std::uint8_t> HmacFlowHasher::bucket(const FlowKey& key) {
    const auto bytes = key.serialize();
    auto digest = crypto_.hmacSha256(key_.span(), bytes);
    if (!digest) return digest.error();
    return static_cast<std::uint8_t>(digest->back() & 0x0f);
}
} // namespace phpvpn
