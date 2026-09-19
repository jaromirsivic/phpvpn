#include "core/protocol/ipv4.h"

namespace phpvpn {
namespace {

// Parses one decimal octet without leading zeros. "0" is allowed, "00" and "012"
// are not, matching the exact spelling the protocol pins for `src` and `dst`.
bool parseOctet(std::string_view text, std::uint32_t& out) noexcept {
    if (text.empty() || text.size() > 3) {
        return false;
    }
    if (text.size() > 1 && text[0] == '0') {
        return false;
    }
    std::uint32_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + static_cast<std::uint32_t>(c - '0');
    }
    if (value > 255) {
        return false;
    }
    out = value;
    return true;
}

void appendDecimal(std::string& out, std::uint32_t value) {
    char buffer[4];
    int length = 0;
    do {
        buffer[length++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (length > 0) {
        out.push_back(buffer[--length]);
    }
}

}  // namespace

std::optional<Ipv4Address> Ipv4Address::parse(std::string_view text) noexcept {
    std::uint32_t parts[4] = {};
    std::size_t start = 0;
    for (int i = 0; i < 4; ++i) {
        const std::size_t dot = text.find('.', start);
        std::string_view part;
        if (i < 3) {
            if (dot == std::string_view::npos) {
                return std::nullopt;
            }
            part = text.substr(start, dot - start);
            start = dot + 1;
        } else {
            if (dot != std::string_view::npos) {
                return std::nullopt;
            }
            part = text.substr(start);
        }
        if (!parseOctet(part, parts[i])) {
            return std::nullopt;
        }
    }
    return Ipv4Address((parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]);
}

std::string Ipv4Address::toString() const {
    std::string out;
    out.reserve(15);
    appendDecimal(out, (value_ >> 24) & 0xFFu);
    out.push_back('.');
    appendDecimal(out, (value_ >> 16) & 0xFFu);
    out.push_back('.');
    appendDecimal(out, (value_ >> 8) & 0xFFu);
    out.push_back('.');
    appendDecimal(out, value_ & 0xFFu);
    return out;
}

void Ipv4Address::toNetworkBytes(std::uint8_t out[4]) const noexcept {
    out[0] = static_cast<std::uint8_t>((value_ >> 24) & 0xFFu);
    out[1] = static_cast<std::uint8_t>((value_ >> 16) & 0xFFu);
    out[2] = static_cast<std::uint8_t>((value_ >> 8) & 0xFFu);
    out[3] = static_cast<std::uint8_t>(value_ & 0xFFu);
}

Ipv4Address Ipv4Address::fromNetworkBytes(const std::uint8_t bytes[4]) noexcept {
    return Ipv4Address((static_cast<std::uint32_t>(bytes[0]) << 24) |
                       (static_cast<std::uint32_t>(bytes[1]) << 16) |
                       (static_cast<std::uint32_t>(bytes[2]) << 8) |
                       static_cast<std::uint32_t>(bytes[3]));
}

std::optional<Ipv4Cidr> Ipv4Cidr::parse(std::string_view text) noexcept {
    const std::size_t slash = text.find('/');
    if (slash == std::string_view::npos) {
        return std::nullopt;
    }

    const auto network = Ipv4Address::parse(text.substr(0, slash));
    if (!network.has_value()) {
        return std::nullopt;
    }

    const std::string_view lengthText = text.substr(slash + 1);
    if (lengthText.empty() || lengthText.size() > 2) {
        return std::nullopt;
    }
    if (lengthText.size() > 1 && lengthText[0] == '0') {
        return std::nullopt;
    }
    int prefixLength = 0;
    for (char c : lengthText) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        prefixLength = prefixLength * 10 + (c - '0');
    }
    if (prefixLength < kMinPrefixLength || prefixLength > kMaxPrefixLength) {
        return std::nullopt;
    }

    const std::uint32_t mask =
        prefixLength == 0 ? 0u : (0xFFFFFFFFu << (32 - prefixLength));
    if ((network->value() & mask) != network->value()) {
        return std::nullopt;  // non zero host bits are not canonical
    }

    Ipv4Cidr cidr;
    cidr.network_ = *network;
    cidr.prefixLength_ = prefixLength;
    return cidr;
}

Ipv4Address Ipv4Cidr::broadcast() const noexcept {
    const std::uint32_t hostMask = prefixLength_ == 32 ? 0u : (0xFFFFFFFFu >> prefixLength_);
    return Ipv4Address(network_.value() | hostMask);
}

Ipv4Address Ipv4Cidr::firstClientAddress() const noexcept {
    return Ipv4Address(network_.value() + 2);
}

Ipv4Address Ipv4Cidr::lastClientAddress() const noexcept {
    return Ipv4Address(broadcast().value() - 1);
}

std::uint32_t Ipv4Cidr::clientAddressCount() const noexcept {
    const std::uint32_t first = firstClientAddress().value();
    const std::uint32_t last = lastClientAddress().value();
    if (last < first) {
        return 0;
    }
    return last - first + 1;
}

bool Ipv4Cidr::contains(Ipv4Address address) const noexcept {
    const std::uint32_t mask =
        prefixLength_ == 0 ? 0u : (0xFFFFFFFFu << (32 - prefixLength_));
    return (address.value() & mask) == network_.value();
}

bool Ipv4Cidr::isValidClientAddress(Ipv4Address address) const noexcept {
    if (!contains(address)) {
        return false;
    }
    return address.value() >= firstClientAddress().value() &&
           address.value() <= lastClientAddress().value();
}

std::string Ipv4Cidr::toString() const {
    std::string out = network_.toString();
    out.push_back('/');
    appendDecimal(out, static_cast<std::uint32_t>(prefixLength_));
    return out;
}

std::uint16_t ipv4HeaderChecksum(std::span<const std::uint8_t> header) noexcept {
    std::uint32_t sum = 0;
    std::size_t i = 0;
    for (; i + 1 < header.size(); i += 2) {
        sum += (static_cast<std::uint32_t>(header[i]) << 8) | header[i + 1];
    }
    if (i < header.size()) {
        sum += static_cast<std::uint32_t>(header[i]) << 8;
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return static_cast<std::uint16_t>(~sum & 0xFFFFu);
}

bool validateIpv4Packet(std::span<const std::uint8_t> packet, Ipv4PacketView& out) noexcept {
    if (packet.size() < 20) {
        return false;
    }
    if ((packet[0] >> 4) != 4) {
        return false;
    }

    const std::size_t ihl = static_cast<std::size_t>(packet[0] & 0x0Fu);
    if (ihl < 5) {
        return false;
    }
    const std::size_t headerLength = ihl * 4;
    if (headerLength > packet.size()) {
        return false;
    }

    const std::size_t totalLength =
        (static_cast<std::size_t>(packet[2]) << 8) | static_cast<std::size_t>(packet[3]);
    if (totalLength < headerLength) {
        return false;
    }
    // No trailing and no missing bytes: the buffer is exactly one packet.
    if (totalLength != packet.size()) {
        return false;
    }

    // A valid header checksum leaves a zero ones complement sum over the header.
    if (ipv4HeaderChecksum(packet.subspan(0, headerLength)) != 0) {
        return false;
    }

    out.headerLength = headerLength;
    out.totalLength = totalLength;
    out.protocol = packet[9];
    out.fragmentField = static_cast<std::uint16_t>((packet[6] << 8) | packet[7]);
    out.source = Ipv4Address::fromNetworkBytes(packet.data() + 12);
    out.destination = Ipv4Address::fromNetworkBytes(packet.data() + 16);
    return true;
}

}  // namespace phpvpn
