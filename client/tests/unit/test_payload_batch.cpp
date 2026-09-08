// Binary batch encoding and the full inner IPv4 validation, protocol.md chapter
// 9.4 and client.md "Uplna validace vnitrnich IPv4 paketu".

#include <vector>

#include "core/protocol/ipv4.h"
#include "core/protocol/payload_batch.h"
#include "harness.h"

namespace phpvpn {
namespace {

// Builds a minimal but fully valid IPv4 packet with a correct header checksum.
PacketBytes makePacket(Ipv4Address source, Ipv4Address destination, std::size_t payloadBytes = 0) {
    PacketBytes packet(20 + payloadBytes, 0);
    packet[0] = 0x45;  // version 4, IHL 5
    packet[1] = 0x00;
    const std::size_t total = packet.size();
    packet[2] = static_cast<std::uint8_t>((total >> 8) & 0xFF);
    packet[3] = static_cast<std::uint8_t>(total & 0xFF);
    packet[8] = 64;   // TTL
    packet[9] = 253;  // an experimental protocol number
    source.toNetworkBytes(packet.data() + 12);
    destination.toNetworkBytes(packet.data() + 16);

    const std::uint16_t checksum = ipv4HeaderChecksum(std::span<const std::uint8_t>(packet.data(), 20));
    packet[10] = static_cast<std::uint8_t>((checksum >> 8) & 0xFF);
    packet[11] = static_cast<std::uint8_t>(checksum & 0xFF);
    return packet;
}

Ipv4Address address(const char* text) {
    const auto parsed = Ipv4Address::parse(text);
    return parsed.has_value() ? *parsed : Ipv4Address(0);
}

}  // namespace

PHPVPN_TEST(payload, encoded_size_matches_the_encoder) {
    const Ipv4Address source = address("10.99.99.2");
    const Ipv4Address destination = address("10.99.99.3");

    std::vector<PacketBytes> packets;
    packets.push_back(makePacket(source, destination, 0));
    packets.push_back(makePacket(source, destination, 100));
    packets.push_back(makePacket(source, destination, 1480));

    std::vector<std::size_t> sizes;
    for (const PacketBytes& packet : packets) {
        sizes.push_back(packet.size());
    }

    auto encoded = PayloadBatch::encode(packets);
    PHPVPN_REQUIRE_OK(encoded);
    PHPVPN_CHECK_EQ(encoded.value().size(),
                    PayloadBatch::encodedSize(std::span<const std::size_t>(sizes)));
    PHPVPN_CHECK_EQ(encoded.value().size(), std::size_t(4 + 3 * 2 + 20 + 120 + 1500));
}

PHPVPN_TEST(payload, round_trips_a_batch) {
    const Ipv4Address source = address("10.99.99.2");
    const Ipv4Address destination = address("10.99.99.3");

    std::vector<PacketBytes> packets;
    packets.push_back(makePacket(source, destination, 0));
    packets.push_back(makePacket(source, destination, 40));

    auto encoded = PayloadBatch::encode(packets);
    PHPVPN_REQUIRE_OK(encoded);

    auto decoded = PayloadBatch::decodeAndValidate(encoded.value(), source, destination);
    PHPVPN_REQUIRE_OK(decoded);
    PHPVPN_CHECK_EQ(decoded.value().size(), std::size_t(2));
    PHPVPN_CHECK_EQ(decoded.value()[0], packets[0]);
    PHPVPN_CHECK_EQ(decoded.value()[1], packets[1]);
}

PHPVPN_TEST(payload, refuses_an_empty_batch) {
    std::vector<PacketBytes> empty;
    PHPVPN_CHECK(!PayloadBatch::encode(empty).ok());

    const std::vector<std::uint8_t> zeroCount = {0, 0, 0, 0};
    PHPVPN_CHECK(!PayloadBatch::decode(zeroCount).ok());
}

PHPVPN_TEST(payload, refuses_trailing_and_truncated_data) {
    const Ipv4Address source = address("10.99.99.2");
    const Ipv4Address destination = address("10.99.99.3");

    std::vector<PacketBytes> packets;
    packets.push_back(makePacket(source, destination, 0));
    auto encoded = PayloadBatch::encode(packets);
    PHPVPN_REQUIRE_OK(encoded);

    std::vector<std::uint8_t> withTrailer = encoded.value();
    withTrailer.push_back(0);
    PHPVPN_CHECK(!PayloadBatch::decode(withTrailer).ok());

    std::vector<std::uint8_t> truncated = encoded.value();
    truncated.pop_back();
    PHPVPN_CHECK(!PayloadBatch::decode(truncated).ok());

    // A header that promises more entries than the buffer holds must not lead to
    // an allocation from the unverified count.
    std::vector<std::uint8_t> hugeCount = {0xFF, 0xFF, 0xFF, 0xFF};
    PHPVPN_CHECK(!PayloadBatch::decode(hugeCount).ok());
}

PHPVPN_TEST(payload, refuses_a_packet_below_the_minimum_length) {
    // A declared length of 19 is below the 20 byte minimum of the protocol.
    std::vector<std::uint8_t> encoded = {0, 0, 0, 1, 0, 19};
    encoded.resize(encoded.size() + 19, 0);
    PHPVPN_CHECK(!PayloadBatch::decode(encoded).ok());
}

PHPVPN_TEST(payload, validates_the_ipv4_header) {
    const Ipv4Address source = address("10.99.99.2");
    const Ipv4Address destination = address("10.99.99.3");

    Ipv4PacketView view;
    const PacketBytes valid = makePacket(source, destination, 8);
    PHPVPN_CHECK(validateIpv4Packet(valid, view));
    PHPVPN_CHECK_EQ(view.source, source);
    PHPVPN_CHECK_EQ(view.destination, destination);
    PHPVPN_CHECK_EQ(view.headerLength, std::size_t(20));
    PHPVPN_CHECK_EQ(view.totalLength, valid.size());

    // A wrong version, a short IHL, a mismatching total length and a broken
    // checksum are each rejected on their own.
    PacketBytes wrongVersion = valid;
    wrongVersion[0] = 0x65;
    PHPVPN_CHECK(!validateIpv4Packet(wrongVersion, view));

    PacketBytes shortHeader = valid;
    shortHeader[0] = 0x44;
    PHPVPN_CHECK(!validateIpv4Packet(shortHeader, view));

    PacketBytes wrongLength = valid;
    wrongLength[3] = static_cast<std::uint8_t>(wrongLength[3] + 1);
    PHPVPN_CHECK(!validateIpv4Packet(wrongLength, view));

    PacketBytes brokenChecksum = valid;
    brokenChecksum[10] = static_cast<std::uint8_t>(brokenChecksum[10] ^ 0xFF);
    PHPVPN_CHECK(!validateIpv4Packet(brokenChecksum, view));

    PacketBytes tooShort(19, 0);
    PHPVPN_CHECK(!validateIpv4Packet(tooShort, view));
}

PHPVPN_TEST(payload, binds_the_inner_addresses_to_the_record) {
    const Ipv4Address source = address("10.99.99.2");
    const Ipv4Address destination = address("10.99.99.3");
    const Ipv4Address other = address("10.99.99.4");

    std::vector<PacketBytes> packets;
    packets.push_back(makePacket(source, destination));
    auto encoded = PayloadBatch::encode(packets);
    PHPVPN_REQUIRE_OK(encoded);

    PHPVPN_CHECK(PayloadBatch::decodeAndValidate(encoded.value(), source, destination).ok());
    // A single mismatching packet invalidates the whole batch.
    PHPVPN_CHECK(!PayloadBatch::decodeAndValidate(encoded.value(), other, destination).ok());
    PHPVPN_CHECK(!PayloadBatch::decodeAndValidate(encoded.value(), source, other).ok());
}

PHPVPN_TEST(payload, one_invalid_packet_invalidates_the_whole_batch) {
    const Ipv4Address source = address("10.99.99.2");
    const Ipv4Address destination = address("10.99.99.3");

    std::vector<PacketBytes> packets;
    packets.push_back(makePacket(source, destination));
    PacketBytes broken = makePacket(source, destination);
    broken[10] = static_cast<std::uint8_t>(broken[10] ^ 0xFF);
    packets.push_back(broken);

    auto encoded = PayloadBatch::encode(packets);
    PHPVPN_REQUIRE_OK(encoded);
    PHPVPN_CHECK(!PayloadBatch::decodeAndValidate(encoded.value(), source, destination).ok());
}

PHPVPN_TEST(payload, cidr_reserves_the_network_gateway_and_broadcast) {
    const auto cidr = Ipv4Cidr::parse("10.99.99.0/24");
    PHPVPN_CHECK(cidr.has_value());
    if (!cidr.has_value()) {
        return;
    }

    PHPVPN_CHECK_EQ(cidr->firstClientAddress(), address("10.99.99.2"));
    PHPVPN_CHECK_EQ(cidr->lastClientAddress(), address("10.99.99.254"));
    PHPVPN_CHECK_EQ(cidr->clientAddressCount(), std::uint32_t(253));

    PHPVPN_CHECK(!cidr->isValidClientAddress(address("10.99.99.0")));
    PHPVPN_CHECK(!cidr->isValidClientAddress(address("10.99.99.1")));
    PHPVPN_CHECK(cidr->isValidClientAddress(address("10.99.99.2")));
    PHPVPN_CHECK(!cidr->isValidClientAddress(address("10.99.99.255")));
    PHPVPN_CHECK(!cidr->isValidClientAddress(address("10.99.98.5")));
}

PHPVPN_TEST(payload, cidr_rejects_a_non_canonical_spelling) {
    PHPVPN_CHECK(!Ipv4Cidr::parse("10.99.99.1/24").has_value());
    PHPVPN_CHECK(!Ipv4Cidr::parse("10.99.99.0/8").has_value());
    PHPVPN_CHECK(!Ipv4Cidr::parse("10.99.99.0/30").has_value());
    PHPVPN_CHECK(!Ipv4Cidr::parse("010.99.99.0/24").has_value());
    PHPVPN_CHECK(!Ipv4Cidr::parse("10.99.99.0").has_value());
    PHPVPN_CHECK(!Ipv4Cidr::parse("10.99.99.0/024").has_value());
    PHPVPN_CHECK(!Ipv4Cidr::parse("10.99.99.0/99999999999999999999").has_value());
    for (int prefix = 0; prefix <= 33; ++prefix) {
        PHPVPN_CHECK_EQ(Ipv4Cidr::parse("0.0.0.0/" + std::to_string(prefix)).has_value(),
            prefix >= 24 && prefix <= 29);
    }
}

PHPVPN_TEST(payload, cidr_supported_boundaries_keep_reservations_without_overflow) {
    for (int prefix = 24; prefix <= 29; ++prefix) {
        const std::uint32_t addresses = 1u << (32 - prefix);
        // Both ends of uint32_t, including the last aligned /29 network.
        for (const auto network : {0u, 0xffffffffu - addresses + 1}) {
            const auto cidr = Ipv4Cidr::parse(Ipv4Address(network).toString() + "/" + std::to_string(prefix));
            PHPVPN_CHECK(cidr.has_value());
            if (!cidr) return;
            const auto broadcast = network + (addresses - 1);
            PHPVPN_CHECK_EQ(cidr->firstClientAddress().value(), network + 2);
            PHPVPN_CHECK_EQ(cidr->lastClientAddress().value(), broadcast - 1);
            PHPVPN_CHECK_EQ(cidr->clientAddressCount(), addresses - 3);
            PHPVPN_CHECK_EQ(cidr->clientAddressCount() - 1, addresses - 4);
            PHPVPN_CHECK(!cidr->isValidClientAddress(Ipv4Address(network)));
            PHPVPN_CHECK(!cidr->isValidClientAddress(Ipv4Address(network + 1)));
            PHPVPN_CHECK(cidr->isValidClientAddress(Ipv4Address(network + 2)));
            PHPVPN_CHECK(cidr->isValidClientAddress(Ipv4Address(broadcast - 1)));
            PHPVPN_CHECK(!cidr->isValidClientAddress(Ipv4Address(broadcast)));
            PHPVPN_CHECK(!Ipv4Cidr::parse(Ipv4Address(network + 2).toString() + "/" + std::to_string(prefix)));
        }
    }
}

PHPVPN_TEST(payload, address_parsing_pins_the_decimal_spelling) {
    PHPVPN_CHECK(Ipv4Address::parse("0.0.0.0").has_value());
    PHPVPN_CHECK(Ipv4Address::parse("255.255.255.255").has_value());
    PHPVPN_CHECK(!Ipv4Address::parse("01.2.3.4").has_value());
    PHPVPN_CHECK(!Ipv4Address::parse("1.2.3").has_value());
    PHPVPN_CHECK(!Ipv4Address::parse("1.2.3.256").has_value());
    PHPVPN_CHECK(!Ipv4Address::parse("1.2.3.4.5").has_value());
    PHPVPN_CHECK(!Ipv4Address::parse(" 1.2.3.4").has_value());
    PHPVPN_CHECK_EQ(address("10.99.99.2").toString(), std::string("10.99.99.2"));
}

}  // namespace phpvpn
