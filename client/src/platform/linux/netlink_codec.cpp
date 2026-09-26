#include "platform/linux/netlink_codec.h"

#include <net/if.h>
#include <sys/socket.h>

#include <linux/if_addr.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace phpvpn::netlink {
namespace {

constexpr std::size_t kAttributeHeader = 4;

constexpr std::size_t align4(std::size_t value) noexcept { return (value + 3u) & ~std::size_t{3}; }

Error malformed(const char* what) {
    return Error(ErrorCode::netconfig_apply_failed, std::string("malformed netlink ") + what);
}

template <class T>
T readRaw(std::span<const std::uint8_t> bytes, std::size_t offset = 0) noexcept {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

// The fixed family header right after the netlink header, and the attributes
// that follow it.
template <class Header>
Result<std::span<const std::uint8_t>> familyPayload(std::span<const std::uint8_t> message,
                                                    std::uint16_t expectedA,
                                                    std::uint16_t expectedB, Header& header,
                                                    const char* what) {
    if (message.size() < NLMSG_HDRLEN) return malformed(what);
    const auto netlinkHeader = readRaw<nlmsghdr>(message);
    if (netlinkHeader.nlmsg_len < NLMSG_HDRLEN || netlinkHeader.nlmsg_len > message.size() ||
        (netlinkHeader.nlmsg_type != expectedA && netlinkHeader.nlmsg_type != expectedB)) {
        return malformed(what);
    }
    const auto payload = message.subspan(NLMSG_HDRLEN, netlinkHeader.nlmsg_len - NLMSG_HDRLEN);
    if (payload.size() < sizeof(Header)) return malformed(what);
    header = readRaw<Header>(payload);
    const std::size_t attributesAt = NLMSG_ALIGN(sizeof(Header));
    if (attributesAt >= payload.size()) return std::span<const std::uint8_t>();
    return payload.subspan(attributesAt);
}

void appendAligned(Bytes& bytes, const void* data, std::size_t size) {
    const auto* begin = static_cast<const std::uint8_t*>(data);
    bytes.insert(bytes.end(), begin, begin + size);
    bytes.resize(align4(bytes.size()), 0);
}

}  // namespace

Result<std::vector<Attribute>> parseAttributes(std::span<const std::uint8_t> bytes) {
    std::vector<Attribute> attributes;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < kAttributeHeader) return malformed("attribute header");
        const auto length = readRaw<std::uint16_t>(bytes, offset);
        const auto type = readRaw<std::uint16_t>(bytes, offset + 2);
        if (length < kAttributeHeader || length > bytes.size() - offset) {
            return malformed("attribute length");
        }
        Attribute attribute;
        attribute.type = static_cast<std::uint16_t>(type & NLA_TYPE_MASK);
        attribute.payload = bytes.subspan(offset + kAttributeHeader, length - kAttributeHeader);
        attributes.push_back(attribute);
        // The padding of the last attribute may be missing.
        offset += std::min<std::size_t>(align4(length), bytes.size() - offset);
    }
    return attributes;
}

const Attribute* findAttribute(const std::vector<Attribute>& attributes, std::uint16_t type) {
    for (const Attribute& attribute : attributes) {
        if (attribute.type == type) return &attribute;
    }
    return nullptr;
}

std::optional<std::uint8_t> attributeU8(const std::vector<Attribute>& attributes,
                                        std::uint16_t type) {
    const Attribute* attribute = findAttribute(attributes, type);
    if (attribute == nullptr || attribute->payload.size() < 1) return std::nullopt;
    return attribute->payload[0];
}

std::optional<std::uint32_t> attributeU32(const std::vector<Attribute>& attributes,
                                          std::uint16_t type) {
    const Attribute* attribute = findAttribute(attributes, type);
    if (attribute == nullptr || attribute->payload.size() < 4) return std::nullopt;
    return readRaw<std::uint32_t>(attribute->payload);
}

std::optional<std::string> attributeString(const std::vector<Attribute>& attributes,
                                           std::uint16_t type) {
    const Attribute* attribute = findAttribute(attributes, type);
    if (attribute == nullptr) return std::nullopt;
    std::string text(reinterpret_cast<const char*>(attribute->payload.data()),
                     attribute->payload.size());
    const auto terminator = text.find('\0');
    if (terminator != std::string::npos) text.resize(terminator);
    return text;
}

std::optional<Ipv4Address> attributeIpv4(const std::vector<Attribute>& attributes,
                                         std::uint16_t type) {
    const Attribute* attribute = findAttribute(attributes, type);
    if (attribute == nullptr || attribute->payload.size() != 4) return std::nullopt;
    return Ipv4Address::fromNetworkBytes(attribute->payload.data());
}

// ---- MessageBuilder --------------------------------------------------------

MessageBuilder::MessageBuilder(std::uint16_t type, std::uint16_t flags) {
    nlmsghdr header{};
    header.nlmsg_type = type;
    header.nlmsg_flags = flags;
    appendAligned(bytes_, &header, sizeof(header));
}

void MessageBuilder::appendFixed(const void* data, std::size_t size) {
    appendAligned(bytes_, data, size);
}

void MessageBuilder::appendAttribute(std::uint16_t type, const void* data, std::size_t size) {
    const auto length = static_cast<std::uint16_t>(kAttributeHeader + size);
    std::uint8_t header[kAttributeHeader];
    std::memcpy(header, &length, 2);
    std::memcpy(header + 2, &type, 2);
    bytes_.insert(bytes_.end(), header, header + kAttributeHeader);
    if (size > 0) {
        const auto* begin = static_cast<const std::uint8_t*>(data);
        bytes_.insert(bytes_.end(), begin, begin + size);
    }
    bytes_.resize(align4(bytes_.size()), 0);
}

void MessageBuilder::appendU8(std::uint16_t type, std::uint8_t value) {
    appendAttribute(type, &value, 1);
}

void MessageBuilder::appendU32(std::uint16_t type, std::uint32_t value) {
    appendAttribute(type, &value, 4);
}

void MessageBuilder::appendString(std::uint16_t type, std::string_view value) {
    std::string terminated(value);
    appendAttribute(type, terminated.c_str(), terminated.size() + 1);
}

void MessageBuilder::appendIpv4(std::uint16_t type, Ipv4Address address) {
    std::uint8_t bytes[4];
    address.toNetworkBytes(bytes);
    appendAttribute(type, bytes, 4);
}

std::size_t MessageBuilder::beginNested(std::uint16_t type) {
    const std::size_t offset = bytes_.size();
    const std::uint16_t nestedType = static_cast<std::uint16_t>(type | NLA_F_NESTED);
    appendAttribute(nestedType, nullptr, 0);
    return offset;
}

void MessageBuilder::endNested(std::size_t offset) {
    const auto length = static_cast<std::uint16_t>(bytes_.size() - offset);
    std::memcpy(bytes_.data() + offset, &length, 2);
}

Bytes MessageBuilder::finish(std::uint32_t sequence) const {
    Bytes out = bytes_;
    const auto length = static_cast<std::uint32_t>(out.size());
    std::memcpy(out.data() + offsetof(nlmsghdr, nlmsg_len), &length, 4);
    std::memcpy(out.data() + offsetof(nlmsghdr, nlmsg_seq), &sequence, 4);
    return out;
}

// ---- datagrams and replies -------------------------------------------------

Result<std::vector<Message>> splitMessages(std::span<const std::uint8_t> datagram) {
    std::vector<Message> messages;
    std::size_t offset = 0;
    while (offset < datagram.size()) {
        if (datagram.size() - offset < NLMSG_HDRLEN) return malformed("message header");
        const auto header = readRaw<nlmsghdr>(datagram, offset);
        if (header.nlmsg_len < NLMSG_HDRLEN || header.nlmsg_len > datagram.size() - offset) {
            return malformed("message length");
        }
        Message message;
        message.type = header.nlmsg_type;
        message.flags = header.nlmsg_flags;
        message.sequence = header.nlmsg_seq;
        message.portId = header.nlmsg_pid;
        message.bytes = datagram.subspan(offset, header.nlmsg_len);
        message.payload = message.bytes.subspan(NLMSG_HDRLEN);
        messages.push_back(message);
        offset += std::min<std::size_t>(NLMSG_ALIGN(header.nlmsg_len), datagram.size() - offset);
    }
    return messages;
}

Result<int> parseErrorPayload(std::span<const std::uint8_t> payload) {
    if (payload.size() < sizeof(nlmsgerr)) return malformed("error message");
    const int error = readRaw<int>(payload);
    if (error > 0) return malformed("error code");
    return -error;
}

ReplyCollector::ReplyCollector(std::uint32_t sequence, bool dump)
    : sequence_(sequence), dump_(dump) {}

Result<bool> ReplyCollector::feed(std::span<const std::uint8_t> datagram) {
    if (complete_) return true;
    std::vector<Message> messages;
    PHPVPN_TRY_ASSIGN(messages, splitMessages(datagram));
    for (const Message& message : messages) {
        if (message.sequence != sequence_) continue;
        if ((message.flags & NLM_F_DUMP_INTR) != 0) interrupted_ = true;
        switch (message.type) {
        case NLMSG_NOOP:
            continue;
        case NLMSG_OVERRUN:
            return Error(ErrorCode::netconfig_apply_failed, "the netlink answer overran");
        case NLMSG_ERROR: {
            int error = 0;
            PHPVPN_TRY_ASSIGN(error, parseErrorPayload(message.payload));
            error_ = error;
            complete_ = true;
            return true;
        }
        case NLMSG_DONE:
            // Since Linux 2.6 a dump that failed half way reports a negative
            // errno in the payload of NLMSG_DONE.
            if (message.payload.size() >= sizeof(int)) {
                const int error = readRaw<int>(message.payload);
                if (error < 0) error_ = -error;
            }
            complete_ = true;
            return true;
        default:
            messages_.emplace_back(message.bytes.begin(), message.bytes.end());
            // A plain request without NLM_F_ACK would end here, but every
            // non-dump request of this codec asks for an acknowledgement.
            break;
        }
    }
    (void)dump_;
    return false;
}

// ---- decoders --------------------------------------------------------------

Result<LinkRecord> decodeLink(std::span<const std::uint8_t> message) {
    ifinfomsg header{};
    std::span<const std::uint8_t> attributeBytes;
    PHPVPN_TRY_ASSIGN(attributeBytes,
                      familyPayload(message, RTM_NEWLINK, RTM_DELLINK, header, "link"));
    std::vector<Attribute> attributes;
    PHPVPN_TRY_ASSIGN(attributes, parseAttributes(attributeBytes));

    LinkRecord link;
    link.index = header.ifi_index;
    link.deviceType = header.ifi_type;
    link.flags = header.ifi_flags;
    link.name = attributeString(attributes, IFLA_IFNAME).value_or(std::string());
    link.alias = attributeString(attributes, IFLA_IFALIAS);
    link.mtu = attributeU32(attributes, IFLA_MTU);
    link.carrier = attributeU8(attributes, IFLA_CARRIER);

    if (const Attribute* info = findAttribute(attributes, IFLA_LINKINFO)) {
        std::vector<Attribute> infoAttributes;
        PHPVPN_TRY_ASSIGN(infoAttributes, parseAttributes(info->payload));
        link.kind = attributeString(infoAttributes, IFLA_INFO_KIND).value_or(std::string());
        const Attribute* data = findAttribute(infoAttributes, IFLA_INFO_DATA);
        if (link.kind == "tun" && data != nullptr) {
            std::vector<Attribute> tun;
            PHPVPN_TRY_ASSIGN(tun, parseAttributes(data->payload));
            link.tun.present = true;
            link.tun.type = attributeU8(tun, IFLA_TUN_TYPE).value_or(0);
            link.tun.persist = attributeU8(tun, IFLA_TUN_PERSIST).value_or(0) != 0;
            link.tun.multiQueue = attributeU8(tun, IFLA_TUN_MULTI_QUEUE).value_or(0) != 0;
            link.tun.packetInfo = attributeU8(tun, IFLA_TUN_PI).value_or(0) != 0;
            link.tun.owner = attributeU32(tun, IFLA_TUN_OWNER);
        }
    }
    return link;
}

Result<AddressRecord> decodeAddress(std::span<const std::uint8_t> message) {
    ifaddrmsg header{};
    std::span<const std::uint8_t> attributeBytes;
    PHPVPN_TRY_ASSIGN(attributeBytes,
                      familyPayload(message, RTM_NEWADDR, RTM_DELADDR, header, "address"));
    std::vector<Attribute> attributes;
    PHPVPN_TRY_ASSIGN(attributes, parseAttributes(attributeBytes));

    AddressRecord record;
    record.family = header.ifa_family;
    record.prefixLength = header.ifa_prefixlen;
    record.scope = header.ifa_scope;
    record.index = static_cast<int>(header.ifa_index);
    record.flags = attributeU32(attributes, IFA_FLAGS).value_or(header.ifa_flags);
    if (record.family == AF_INET) {
        record.local = attributeIpv4(attributes, IFA_LOCAL);
        record.address = attributeIpv4(attributes, IFA_ADDRESS);
    }
    return record;
}

Result<RouteRecord> decodeRoute(std::span<const std::uint8_t> message) {
    rtmsg header{};
    std::span<const std::uint8_t> attributeBytes;
    PHPVPN_TRY_ASSIGN(attributeBytes,
                      familyPayload(message, RTM_NEWROUTE, RTM_DELROUTE, header, "route"));
    std::vector<Attribute> attributes;
    PHPVPN_TRY_ASSIGN(attributes, parseAttributes(attributeBytes));

    RouteRecord record;
    record.family = header.rtm_family;
    record.destinationLength = header.rtm_dst_len;
    record.protocol = header.rtm_protocol;
    record.scope = header.rtm_scope;
    record.type = header.rtm_type;
    record.flags = header.rtm_flags;
    record.table = attributeU32(attributes, RTA_TABLE).value_or(header.rtm_table);
    record.priority = attributeU32(attributes, RTA_PRIORITY).value_or(0);
    if (record.family != AF_INET) return record;
    if (record.destinationLength > 32) return malformed("route prefix");
    record.destination = attributeIpv4(attributes, RTA_DST).value_or(Ipv4Address(0));

    if (const Attribute* multipath = findAttribute(attributes, RTA_MULTIPATH)) {
        const auto bytes = multipath->payload;
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            if (bytes.size() - offset < sizeof(rtnexthop)) return malformed("multipath entry");
            const auto hop = readRaw<rtnexthop>(bytes, offset);
            if (hop.rtnh_len < sizeof(rtnexthop) || hop.rtnh_len > bytes.size() - offset) {
                return malformed("multipath length");
            }
            const std::size_t hopEnd = offset + hop.rtnh_len;
            const std::size_t hopAttributesAt =
                std::min<std::size_t>(offset + RTNH_ALIGN(sizeof(rtnexthop)), hopEnd);
            std::vector<Attribute> hopAttributes;
            PHPVPN_TRY_ASSIGN(hopAttributes, parseAttributes(bytes.subspan(
                                                 hopAttributesAt, hopEnd - hopAttributesAt)));
            RouteNextHop nextHop;
            nextHop.oif = hop.rtnh_ifindex;
            nextHop.gateway = attributeIpv4(hopAttributes, RTA_GATEWAY);
            record.nextHops.push_back(nextHop);
            offset += std::min<std::size_t>(RTNH_ALIGN(hop.rtnh_len), bytes.size() - offset);
        }
    } else {
        RouteNextHop nextHop;
        nextHop.oif = static_cast<int>(attributeU32(attributes, RTA_OIF).value_or(0));
        nextHop.gateway = attributeIpv4(attributes, RTA_GATEWAY);
        record.nextHops.push_back(nextHop);
    }
    return record;
}

// ---- requests ----------------------------------------------------------------

Bytes encodeLinkGetByIndex(std::uint32_t sequence, int index) {
    MessageBuilder builder(RTM_GETLINK, NLM_F_REQUEST | NLM_F_ACK);
    ifinfomsg header{};
    header.ifi_family = AF_UNSPEC;
    header.ifi_index = index;
    builder.appendFixed(&header, sizeof(header));
    return builder.finish(sequence);
}

Bytes encodeLinkGetByName(std::uint32_t sequence, std::string_view name) {
    MessageBuilder builder(RTM_GETLINK, NLM_F_REQUEST | NLM_F_ACK);
    ifinfomsg header{};
    header.ifi_family = AF_UNSPEC;
    builder.appendFixed(&header, sizeof(header));
    builder.appendString(IFLA_IFNAME, name);
    return builder.finish(sequence);
}

Bytes encodeLinkDump(std::uint32_t sequence) {
    MessageBuilder builder(RTM_GETLINK, NLM_F_REQUEST | NLM_F_DUMP);
    ifinfomsg header{};
    header.ifi_family = AF_UNSPEC;
    builder.appendFixed(&header, sizeof(header));
    return builder.finish(sequence);
}

Bytes encodeLinkDelete(std::uint32_t sequence, int index) {
    MessageBuilder builder(RTM_DELLINK, NLM_F_REQUEST | NLM_F_ACK);
    ifinfomsg header{};
    header.ifi_family = AF_UNSPEC;
    header.ifi_index = index;
    builder.appendFixed(&header, sizeof(header));
    return builder.finish(sequence);
}

Bytes encodeLinkChange(std::uint32_t sequence, const LinkChange& change) {
    MessageBuilder builder(RTM_NEWLINK, NLM_F_REQUEST | NLM_F_ACK);
    ifinfomsg header{};
    header.ifi_family = AF_UNSPEC;
    header.ifi_index = change.index;
    if (change.up.has_value()) {
        header.ifi_flags = *change.up ? static_cast<unsigned>(IFF_UP) : 0u;
        header.ifi_change = IFF_UP;
    }
    builder.appendFixed(&header, sizeof(header));
    if (change.alias.has_value()) builder.appendString(IFLA_IFALIAS, *change.alias);
    if (change.mtu.has_value()) builder.appendU32(IFLA_MTU, *change.mtu);
    if (change.inet6AddrGenMode.has_value()) {
        const std::size_t spec = builder.beginNested(IFLA_AF_SPEC);
        const std::size_t inet6 = builder.beginNested(AF_INET6);
        builder.appendU8(IFLA_INET6_ADDR_GEN_MODE, *change.inet6AddrGenMode);
        builder.endNested(inet6);
        builder.endNested(spec);
    }
    return builder.finish(sequence);
}

namespace {

Bytes encodeAddress(std::uint16_t type, std::uint16_t flags, std::uint32_t sequence,
                    const AddressSpec& spec, bool withFlags) {
    MessageBuilder builder(type, flags);
    ifaddrmsg header{};
    header.ifa_family = AF_INET;
    header.ifa_prefixlen = spec.prefixLength;
    header.ifa_scope = RT_SCOPE_UNIVERSE;
    header.ifa_index = static_cast<std::uint32_t>(spec.index);
    builder.appendFixed(&header, sizeof(header));
    builder.appendIpv4(IFA_LOCAL, spec.address);
    builder.appendIpv4(IFA_ADDRESS, spec.address);
    // Without the flag the kernel would add its own prefix route with metric 0
    // and the explicit route of the session would fail with EEXIST.
    if (withFlags) builder.appendU32(IFA_FLAGS, IFA_F_NOPREFIXROUTE);
    return builder.finish(sequence);
}

Bytes encodeRoute(std::uint16_t type, std::uint16_t flags, std::uint32_t sequence,
                  const RouteSpec& spec) {
    MessageBuilder builder(type, flags);
    const bool onLink = spec.gateway.value() == 0;
    rtmsg header{};
    header.rtm_family = AF_INET;
    header.rtm_dst_len = spec.destinationLength;
    header.rtm_table = RT_TABLE_MAIN;
    header.rtm_protocol = RTPROT_STATIC;
    header.rtm_scope = onLink ? RT_SCOPE_LINK : RT_SCOPE_UNIVERSE;
    header.rtm_type = RTN_UNICAST;
    builder.appendFixed(&header, sizeof(header));
    builder.appendU32(RTA_TABLE, RT_TABLE_MAIN);
    if (spec.destinationLength > 0) builder.appendIpv4(RTA_DST, spec.destination);
    if (!onLink) builder.appendIpv4(RTA_GATEWAY, spec.gateway);
    builder.appendU32(RTA_OIF, static_cast<std::uint32_t>(spec.oif));
    builder.appendU32(RTA_PRIORITY, spec.metric);
    return builder.finish(sequence);
}

}  // namespace

Bytes encodeAddressAdd(std::uint32_t sequence, const AddressSpec& spec) {
    return encodeAddress(RTM_NEWADDR, NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL,
                         sequence, spec, true);
}

Bytes encodeAddressDelete(std::uint32_t sequence, const AddressSpec& spec) {
    return encodeAddress(RTM_DELADDR, NLM_F_REQUEST | NLM_F_ACK, sequence, spec, false);
}

Bytes encodeAddressDump(std::uint32_t sequence) {
    MessageBuilder builder(RTM_GETADDR, NLM_F_REQUEST | NLM_F_DUMP);
    ifaddrmsg header{};
    header.ifa_family = AF_INET;
    builder.appendFixed(&header, sizeof(header));
    return builder.finish(sequence);
}

Bytes encodeRouteAdd(std::uint32_t sequence, const RouteSpec& spec) {
    return encodeRoute(RTM_NEWROUTE, NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL,
                       sequence, spec);
}

Bytes encodeRouteDelete(std::uint32_t sequence, const RouteSpec& spec) {
    return encodeRoute(RTM_DELROUTE, NLM_F_REQUEST | NLM_F_ACK, sequence, spec);
}

Bytes encodeRouteDump(std::uint32_t sequence) {
    MessageBuilder builder(RTM_GETROUTE, NLM_F_REQUEST | NLM_F_DUMP);
    rtmsg header{};
    header.rtm_family = AF_INET;
    builder.appendFixed(&header, sizeof(header));
    return builder.finish(sequence);
}

Bytes encodeRouteGet(std::uint32_t sequence, Ipv4Address destination) {
    MessageBuilder builder(RTM_GETROUTE, NLM_F_REQUEST | NLM_F_ACK);
    rtmsg header{};
    header.rtm_family = AF_INET;
    header.rtm_dst_len = 32;
    builder.appendFixed(&header, sizeof(header));
    builder.appendIpv4(RTA_DST, destination);
    return builder.finish(sequence);
}

}  // namespace phpvpn::netlink
