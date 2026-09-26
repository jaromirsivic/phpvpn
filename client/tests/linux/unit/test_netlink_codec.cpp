// Linux platform tests owned by WP04 (plan-v001 section 9.4).
//
// Pure rtnetlink codec, interface identity and DNS snapshot parsing. Nothing
// here opens a netlink socket or needs privileges; kernel answers are built by
// hand in the exact wire layout.
#include "harness.h"

#include <net/if.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/if_addr.h>
#include <linux/if_link.h>
#include <linux/if_tun.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <cstring>
#include <filesystem>
#include <fstream>

#include "platform/linux/dns_snapshot.h"
#include "platform/linux/interface_identity.h"
#include "platform/linux/netlink_codec.h"

namespace phpvpn {
namespace {

using netlink::Bytes;

template <class T>
T readAt(const Bytes& bytes, std::size_t offset) {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

nlmsghdr headerOf(const Bytes& message) { return readAt<nlmsghdr>(message, 0); }

// Attributes after the netlink header and a fixed family header of `fixed` bytes.
std::vector<netlink::Attribute> attributesOf(const Bytes& message, std::size_t fixed) {
    const std::size_t offset = NLMSG_HDRLEN + NLMSG_ALIGN(fixed);
    auto parsed = netlink::parseAttributes(
        std::span<const std::uint8_t>(message.data() + offset, message.size() - offset));
    return parsed.ok() ? parsed.value() : std::vector<netlink::Attribute>();
}

Ipv4Address ip(const char* text) { return *Ipv4Address::parse(text); }

// Kernel style answers.
Bytes errorMessage(std::uint32_t sequence, int negativeErrno, std::uint16_t flags = 0) {
    netlink::MessageBuilder builder(NLMSG_ERROR, flags);
    nlmsgerr error{};
    error.error = negativeErrno;
    builder.appendFixed(&error, sizeof(error));
    return builder.finish(sequence);
}

Bytes doneMessage(std::uint32_t sequence, int value = 0, std::uint16_t flags = NLM_F_MULTI) {
    netlink::MessageBuilder builder(NLMSG_DONE, flags);
    builder.appendFixed(&value, sizeof(value));
    return builder.finish(sequence);
}

Bytes linkMessage(std::uint32_t sequence, int index, const char* name, const char* alias,
                  std::uint16_t flags = 0, bool tunData = false) {
    netlink::MessageBuilder builder(RTM_NEWLINK, flags);
    ifinfomsg header{};
    header.ifi_index = index;
    header.ifi_flags = IFF_UP;
    builder.appendFixed(&header, sizeof(header));
    builder.appendString(IFLA_IFNAME, name);
    if (alias != nullptr) builder.appendString(IFLA_IFALIAS, alias);
    builder.appendU32(IFLA_MTU, 1500);
    builder.appendU8(IFLA_CARRIER, 0);
    if (tunData) {
        const std::size_t info = builder.beginNested(IFLA_LINKINFO);
        builder.appendString(IFLA_INFO_KIND, "tun");
        const std::size_t data = builder.beginNested(IFLA_INFO_DATA);
        builder.appendU8(IFLA_TUN_TYPE, IFF_TUN);
        builder.appendU8(IFLA_TUN_PI, 0);
        builder.appendU8(IFLA_TUN_PERSIST, 1);
        builder.appendU8(IFLA_TUN_MULTI_QUEUE, 0);
        builder.appendU32(IFLA_TUN_OWNER, 0);
        builder.endNested(data);
        builder.endNested(info);
    }
    return builder.finish(sequence);
}

Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const Bytes& part : parts) out.insert(out.end(), part.begin(), part.end());
    return out;
}

std::span<const std::uint8_t> view(const Bytes& bytes) {
    return std::span<const std::uint8_t>(bytes.data(), bytes.size());
}

std::filesystem::path temporaryFile(const char* name, const std::string& content) {
    const auto path = std::filesystem::temp_directory_path() /
                      (std::string("phpvpn-wp04-") + std::to_string(::getpid()) + "-" + name);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << content;
    return path;
}

}  // namespace

PHPVPN_TEST(NetlinkCodec, AddressAddRequestCarriesNoPrefixRouteAndExclusiveCreate) {
    const netlink::AddressSpec spec{7, ip("10.99.99.2"), 24};
    const Bytes message = netlink::encodeAddressAdd(41, spec);
    const nlmsghdr header = headerOf(message);
    PHPVPN_CHECK_EQ(header.nlmsg_len, message.size());
    PHPVPN_CHECK_EQ(header.nlmsg_type, RTM_NEWADDR);
    PHPVPN_CHECK_EQ(header.nlmsg_flags, NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL);
    PHPVPN_CHECK_EQ(header.nlmsg_seq, 41u);
    const auto address = readAt<ifaddrmsg>(message, NLMSG_HDRLEN);
    PHPVPN_CHECK_EQ(address.ifa_family, AF_INET);
    PHPVPN_CHECK_EQ(address.ifa_prefixlen, 24);
    PHPVPN_CHECK_EQ(address.ifa_index, 7u);
    PHPVPN_CHECK_EQ(address.ifa_scope, RT_SCOPE_UNIVERSE);

    const auto attributes = attributesOf(message, sizeof(ifaddrmsg));
    PHPVPN_CHECK(netlink::attributeIpv4(attributes, IFA_LOCAL) == ip("10.99.99.2"));
    PHPVPN_CHECK(netlink::attributeIpv4(attributes, IFA_ADDRESS) == ip("10.99.99.2"));
    PHPVPN_CHECK(netlink::attributeU32(attributes, IFA_FLAGS) ==
                 std::optional<std::uint32_t>(IFA_F_NOPREFIXROUTE));

    const Bytes removal = netlink::encodeAddressDelete(42, spec);
    PHPVPN_CHECK_EQ(headerOf(removal).nlmsg_type, RTM_DELADDR);
    PHPVPN_CHECK_EQ(headerOf(removal).nlmsg_flags, NLM_F_REQUEST | NLM_F_ACK);
    PHPVPN_CHECK(netlink::attributeIpv4(attributesOf(removal, sizeof(ifaddrmsg)), IFA_LOCAL) ==
                 ip("10.99.99.2"));
}

PHPVPN_TEST(NetlinkCodec, OnLinkRouteUsesLinkScopeAndGatewayRouteUsesUniverse) {
    netlink::RouteSpec spec;
    spec.oif = 9;
    spec.destination = ip("10.99.99.0");
    spec.destinationLength = 24;
    spec.metric = 5;
    const Bytes onLink = netlink::encodeRouteAdd(3, spec);
    PHPVPN_CHECK_EQ(headerOf(onLink).nlmsg_type, RTM_NEWROUTE);
    PHPVPN_CHECK_EQ(headerOf(onLink).nlmsg_flags,
                    NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL);
    const auto route = readAt<rtmsg>(onLink, NLMSG_HDRLEN);
    PHPVPN_CHECK_EQ(route.rtm_family, AF_INET);
    PHPVPN_CHECK_EQ(route.rtm_dst_len, 24);
    PHPVPN_CHECK_EQ(route.rtm_table, RT_TABLE_MAIN);
    PHPVPN_CHECK_EQ(route.rtm_protocol, RTPROT_STATIC);
    PHPVPN_CHECK_EQ(route.rtm_scope, RT_SCOPE_LINK);
    PHPVPN_CHECK_EQ(route.rtm_type, RTN_UNICAST);
    auto attributes = attributesOf(onLink, sizeof(rtmsg));
    PHPVPN_CHECK(netlink::attributeU32(attributes, RTA_TABLE) == std::optional<std::uint32_t>(RT_TABLE_MAIN));
    PHPVPN_CHECK(netlink::attributeIpv4(attributes, RTA_DST) == ip("10.99.99.0"));
    PHPVPN_CHECK(netlink::attributeU32(attributes, RTA_OIF) == std::optional<std::uint32_t>(9));
    PHPVPN_CHECK(netlink::attributeU32(attributes, RTA_PRIORITY) == std::optional<std::uint32_t>(5));
    PHPVPN_CHECK(netlink::findAttribute(attributes, RTA_GATEWAY) == nullptr);

    spec.gateway = ip("192.0.2.1");
    const Bytes viaGateway = netlink::encodeRouteDelete(4, spec);
    PHPVPN_CHECK_EQ(headerOf(viaGateway).nlmsg_type, RTM_DELROUTE);
    PHPVPN_CHECK_EQ(headerOf(viaGateway).nlmsg_flags, NLM_F_REQUEST | NLM_F_ACK);
    PHPVPN_CHECK_EQ(readAt<rtmsg>(viaGateway, NLMSG_HDRLEN).rtm_scope, RT_SCOPE_UNIVERSE);
    attributes = attributesOf(viaGateway, sizeof(rtmsg));
    PHPVPN_CHECK(netlink::attributeIpv4(attributes, RTA_GATEWAY) == ip("192.0.2.1"));

    const Bytes get = netlink::encodeRouteGet(5, ip("198.51.100.7"));
    PHPVPN_CHECK_EQ(readAt<rtmsg>(get, NLMSG_HDRLEN).rtm_dst_len, 32);
    PHPVPN_CHECK(netlink::attributeIpv4(attributesOf(get, sizeof(rtmsg)), RTA_DST) ==
                 ip("198.51.100.7"));
}

PHPVPN_TEST(NetlinkCodec, LinkChangeNestsAddressGenerationModeAndSetsUpFlag) {
    netlink::LinkChange change;
    change.index = 12;
    change.alias = "phpvpn-owner:{00000000-0000-0000-0000-000000000001}";
    change.mtu = 1500;
    change.up = true;
    change.inet6AddrGenMode = IN6_ADDR_GEN_MODE_NONE;
    const Bytes message = netlink::encodeLinkChange(8, change);
    const nlmsghdr header = headerOf(message);
    PHPVPN_CHECK_EQ(header.nlmsg_type, RTM_NEWLINK);
    // Changing an existing index: acknowledged, never created.
    PHPVPN_CHECK_EQ(header.nlmsg_flags, NLM_F_REQUEST | NLM_F_ACK);
    const auto link = readAt<ifinfomsg>(message, NLMSG_HDRLEN);
    PHPVPN_CHECK_EQ(link.ifi_index, 12);
    PHPVPN_CHECK_EQ(link.ifi_flags, static_cast<unsigned>(IFF_UP));
    PHPVPN_CHECK_EQ(link.ifi_change, static_cast<unsigned>(IFF_UP));

    const auto attributes = attributesOf(message, sizeof(ifinfomsg));
    PHPVPN_CHECK(netlink::attributeString(attributes, IFLA_IFALIAS) == change.alias);
    PHPVPN_CHECK(netlink::attributeU32(attributes, IFLA_MTU) == std::optional<std::uint32_t>(1500));
    const auto* spec = netlink::findAttribute(attributes, IFLA_AF_SPEC);
    PHPVPN_CHECK(spec != nullptr);
    if (spec == nullptr) return;
    auto families = netlink::parseAttributes(spec->payload);
    PHPVPN_REQUIRE_OK(families);
    const auto* inet6 = netlink::findAttribute(families.value(), AF_INET6);
    PHPVPN_CHECK(inet6 != nullptr);
    if (inet6 == nullptr) return;
    auto mode = netlink::parseAttributes(inet6->payload);
    PHPVPN_REQUIRE_OK(mode);
    PHPVPN_CHECK(netlink::attributeU8(mode.value(), IFLA_INET6_ADDR_GEN_MODE) ==
                 std::optional<std::uint8_t>(IN6_ADDR_GEN_MODE_NONE));

    const Bytes byName = netlink::encodeLinkGetByName(9, "phpvpn");
    PHPVPN_CHECK_EQ(readAt<ifinfomsg>(byName, NLMSG_HDRLEN).ifi_index, 0);
    PHPVPN_CHECK(netlink::attributeString(attributesOf(byName, sizeof(ifinfomsg)), IFLA_IFNAME) ==
                 std::optional<std::string>("phpvpn"));
    PHPVPN_CHECK_EQ(headerOf(netlink::encodeLinkDump(1)).nlmsg_flags, NLM_F_REQUEST | NLM_F_DUMP);
}

PHPVPN_TEST(NetlinkCodec, AttributeParserRejectsMalformedLengths) {
    const std::uint8_t tooShort[] = {3, 0, 1, 0};
    PHPVPN_CHECK(!netlink::parseAttributes(tooShort).ok());
    const std::uint8_t pastEnd[] = {12, 0, 1, 0, 0, 0, 0, 0};
    PHPVPN_CHECK(!netlink::parseAttributes(pastEnd).ok());
    const std::uint8_t truncatedHeader[] = {8, 0, 1, 0, 1, 2, 3, 4, 8, 0};
    PHPVPN_CHECK(!netlink::parseAttributes(truncatedHeader).ok());
    // Last attribute without its padding and a nested flag on the type.
    const std::uint8_t valid[] = {5, 0, 1, 0, 9, 0, 0, 0, 8, 0, 2 | 0, 0x80, 1, 0, 0, 0};
    auto parsed = netlink::parseAttributes(valid);
    PHPVPN_REQUIRE_OK(parsed);
    PHPVPN_CHECK_EQ(parsed->size(), 2u);
    PHPVPN_CHECK(netlink::attributeU8(parsed.value(), 1) == std::optional<std::uint8_t>(9));
    PHPVPN_CHECK(netlink::findAttribute(parsed.value(), 2) != nullptr);
    const std::uint8_t unpadded[] = {5, 0, 1, 0, 7};
    auto tail = netlink::parseAttributes(unpadded);
    PHPVPN_REQUIRE_OK(tail);
    PHPVPN_CHECK_EQ(tail->size(), 1u);
}

PHPVPN_TEST(NetlinkCodec, AcknowledgementAndKernelErrorComplete) {
    netlink::ReplyCollector ack(10, false);
    auto fed = ack.feed(view(errorMessage(10, 0)));
    PHPVPN_REQUIRE_OK(fed);
    PHPVPN_CHECK(fed.value());
    PHPVPN_CHECK_EQ(ack.error(), 0);

    netlink::ReplyCollector exists(11, false);
    fed = exists.feed(view(errorMessage(11, -EEXIST)));
    PHPVPN_REQUIRE_OK(fed);
    PHPVPN_CHECK(fed.value());
    PHPVPN_CHECK_EQ(exists.error(), EEXIST);
    PHPVPN_CHECK(exists.messages().empty());

    // A positive code is not a valid kernel answer, nor a truncated nlmsgerr.
    netlink::ReplyCollector invalid(12, false);
    PHPVPN_CHECK(!invalid.feed(view(errorMessage(12, 5))).ok());
    Bytes truncated = errorMessage(13, -EPERM);
    truncated.resize(NLMSG_HDRLEN + 4);
    const auto length = static_cast<std::uint32_t>(truncated.size());
    std::memcpy(truncated.data(), &length, 4);
    netlink::ReplyCollector shortError(13, false);
    PHPVPN_CHECK(!shortError.feed(view(truncated)).ok());
}

PHPVPN_TEST(NetlinkCodec, GetWithAcknowledgementKeepsDataAndIgnoresStaleSequence) {
    netlink::ReplyCollector collector(20, false);
    const Bytes datagram = concat({linkMessage(19, 1, "stale", nullptr), errorMessage(19, 0),
                                   linkMessage(20, 4, "phpvpn", nullptr), errorMessage(20, 0)});
    auto fed = collector.feed(view(datagram));
    PHPVPN_REQUIRE_OK(fed);
    PHPVPN_CHECK(fed.value());
    PHPVPN_CHECK_EQ(collector.error(), 0);
    PHPVPN_CHECK_EQ(collector.messages().size(), 1u);
    auto link = netlink::decodeLink(collector.messages().front());
    PHPVPN_REQUIRE_OK(link);
    PHPVPN_CHECK_EQ(link->index, 4);
    PHPVPN_CHECK_EQ(link->name, std::string("phpvpn"));
    PHPVPN_CHECK(!link->alias.has_value());
}

PHPVPN_TEST(NetlinkCodec, MultipartDumpSpansDatagramsUntilDone) {
    netlink::ReplyCollector collector(30, true);
    auto fed = collector.feed(view(concat({linkMessage(30, 1, "lo", nullptr, NLM_F_MULTI),
                                           linkMessage(30, 2, "eth0", nullptr, NLM_F_MULTI)})));
    PHPVPN_REQUIRE_OK(fed);
    PHPVPN_CHECK(!fed.value());
    PHPVPN_CHECK(!collector.complete());
    fed = collector.feed(view(concat({linkMessage(30, 3, "phpvpn", "x", NLM_F_MULTI),
                                      doneMessage(30)})));
    PHPVPN_REQUIRE_OK(fed);
    PHPVPN_CHECK(fed.value());
    PHPVPN_CHECK_EQ(collector.error(), 0);
    PHPVPN_CHECK(!collector.interrupted());
    PHPVPN_CHECK_EQ(collector.messages().size(), 3u);

    // Data after completion is ignored.
    fed = collector.feed(view(linkMessage(30, 9, "late", nullptr, NLM_F_MULTI)));
    PHPVPN_REQUIRE_OK(fed);
    PHPVPN_CHECK_EQ(collector.messages().size(), 3u);
}

PHPVPN_TEST(NetlinkCodec, InterruptedDumpAndFailedDumpAreReported) {
    netlink::ReplyCollector interrupted(40, true);
    auto fed = interrupted.feed(view(concat(
        {linkMessage(40, 1, "lo", nullptr, NLM_F_MULTI | NLM_F_DUMP_INTR), doneMessage(40)})));
    PHPVPN_REQUIRE_OK(fed);
    PHPVPN_CHECK(fed.value());
    PHPVPN_CHECK(interrupted.interrupted());

    // The flag may also arrive on NLMSG_DONE only.
    netlink::ReplyCollector lateFlag(41, true);
    fed = lateFlag.feed(view(doneMessage(41, 0, NLM_F_MULTI | NLM_F_DUMP_INTR)));
    PHPVPN_REQUIRE_OK(fed);
    PHPVPN_CHECK(lateFlag.interrupted());

    netlink::ReplyCollector failed(42, true);
    fed = failed.feed(view(doneMessage(42, -EINTR)));
    PHPVPN_REQUIRE_OK(fed);
    PHPVPN_CHECK(fed.value());
    PHPVPN_CHECK_EQ(failed.error(), EINTR);

    netlink::ReplyCollector overrun(43, true);
    netlink::MessageBuilder builder(NLMSG_OVERRUN, 0);
    PHPVPN_CHECK(!overrun.feed(view(builder.finish(43))).ok());
}

PHPVPN_TEST(NetlinkCodec, MalformedDatagramsAreErrors) {
    Bytes message = linkMessage(50, 1, "lo", nullptr);
    netlink::ReplyCollector tooLong(50, false);
    Bytes truncated(message.begin(), message.end() - 8);
    PHPVPN_CHECK(!tooLong.feed(view(truncated)).ok());

    netlink::ReplyCollector shortTail(50, false);
    Bytes withTail = concat({message, Bytes{1, 2, 3, 4, 5, 6, 7, 8}});
    PHPVPN_CHECK(!shortTail.feed(view(withTail)).ok());

    Bytes zeroLength = message;
    const std::uint32_t zero = 0;
    std::memcpy(zeroLength.data(), &zero, 4);
    netlink::ReplyCollector invalid(50, false);
    PHPVPN_CHECK(!invalid.feed(view(zeroLength)).ok());

    auto split = netlink::splitMessages(view(concat({message, errorMessage(50, 0)})));
    PHPVPN_REQUIRE_OK(split);
    PHPVPN_CHECK_EQ(split->size(), 2u);
    PHPVPN_CHECK_EQ(split->at(1).type, NLMSG_ERROR);
}

PHPVPN_TEST(NetlinkCodec, DecodesLinkWithTunDetails) {
    const Bytes message = linkMessage(1, 17, "phpvpn", "phpvpn-owner:{x}", 0, true);
    auto link = netlink::decodeLink(message);
    PHPVPN_REQUIRE_OK(link);
    PHPVPN_CHECK_EQ(link->index, 17);
    PHPVPN_CHECK_EQ(link->name, std::string("phpvpn"));
    PHPVPN_CHECK(link->alias == std::optional<std::string>("phpvpn-owner:{x}"));
    PHPVPN_CHECK(link->mtu == std::optional<std::uint32_t>(1500));
    PHPVPN_CHECK(link->carrier == std::optional<std::uint8_t>(0));
    PHPVPN_CHECK((link->flags & IFF_UP) != 0);
    PHPVPN_CHECK_EQ(link->kind, std::string("tun"));
    PHPVPN_CHECK(link->tun.present);
    PHPVPN_CHECK_EQ(link->tun.type, IFF_TUN);
    PHPVPN_CHECK(link->tun.persist);
    PHPVPN_CHECK(!link->tun.multiQueue);
    PHPVPN_CHECK(!link->tun.packetInfo);
    PHPVPN_CHECK(link->tun.owner == std::optional<std::uint32_t>(0));

    // Another message type is not a link.
    PHPVPN_CHECK(!netlink::decodeLink(errorMessage(1, 0)).ok());
}

PHPVPN_TEST(NetlinkCodec, DecodesAddressFlagsAndRoutesWithMultipath) {
    netlink::MessageBuilder address(RTM_NEWADDR, NLM_F_MULTI);
    ifaddrmsg addressHeader{};
    addressHeader.ifa_family = AF_INET;
    addressHeader.ifa_prefixlen = 24;
    addressHeader.ifa_index = 5;
    address.appendFixed(&addressHeader, sizeof(addressHeader));
    address.appendIpv4(IFA_ADDRESS, ip("10.99.99.2"));
    address.appendIpv4(IFA_LOCAL, ip("10.99.99.2"));
    address.appendU32(IFA_FLAGS, IFA_F_NOPREFIXROUTE | IFA_F_PERMANENT);
    auto decodedAddress = netlink::decodeAddress(address.finish(1));
    PHPVPN_REQUIRE_OK(decodedAddress);
    PHPVPN_CHECK_EQ(decodedAddress->index, 5);
    PHPVPN_CHECK_EQ(decodedAddress->prefixLength, 24);
    PHPVPN_CHECK(decodedAddress->local == ip("10.99.99.2"));
    PHPVPN_CHECK((decodedAddress->flags & IFA_F_NOPREFIXROUTE) != 0);

    netlink::MessageBuilder route(RTM_NEWROUTE, NLM_F_MULTI);
    rtmsg routeHeader{};
    routeHeader.rtm_family = AF_INET;
    routeHeader.rtm_dst_len = 0;
    routeHeader.rtm_table = RT_TABLE_MAIN;
    routeHeader.rtm_type = RTN_UNICAST;
    route.appendFixed(&routeHeader, sizeof(routeHeader));
    route.appendU32(RTA_TABLE, 1000);  // tables above 255 only exist as RTA_TABLE
    route.appendU32(RTA_PRIORITY, 100);
    // Two paths: rtnexthop followed by its own RTA_GATEWAY.
    Bytes paths;
    for (int path = 0; path < 2; ++path) {
        rtnexthop hop{};
        hop.rtnh_len = sizeof(rtnexthop) + 8;
        hop.rtnh_ifindex = 3 + path;
        const auto* raw = reinterpret_cast<const std::uint8_t*>(&hop);
        paths.insert(paths.end(), raw, raw + sizeof(hop));
        const std::uint16_t length = 8;
        const std::uint16_t type = RTA_GATEWAY;
        paths.insert(paths.end(), reinterpret_cast<const std::uint8_t*>(&length),
                     reinterpret_cast<const std::uint8_t*>(&length) + 2);
        paths.insert(paths.end(), reinterpret_cast<const std::uint8_t*>(&type),
                     reinterpret_cast<const std::uint8_t*>(&type) + 2);
        std::uint8_t gateway[4];
        ip(path == 0 ? "192.0.2.1" : "192.0.2.2").toNetworkBytes(gateway);
        paths.insert(paths.end(), gateway, gateway + 4);
    }
    route.appendAttribute(RTA_MULTIPATH, paths.data(), paths.size());
    auto decodedRoute = netlink::decodeRoute(route.finish(2));
    PHPVPN_REQUIRE_OK(decodedRoute);
    PHPVPN_CHECK_EQ(decodedRoute->table, 1000u);
    PHPVPN_CHECK_EQ(decodedRoute->priority, 100u);
    PHPVPN_CHECK_EQ(decodedRoute->destinationLength, 0);
    PHPVPN_CHECK_EQ(decodedRoute->nextHops.size(), 2u);
    if (decodedRoute->nextHops.size() == 2) {
        PHPVPN_CHECK_EQ(decodedRoute->nextHops[0].oif, 3);
        PHPVPN_CHECK(decodedRoute->nextHops[0].gateway == ip("192.0.2.1"));
        PHPVPN_CHECK_EQ(decodedRoute->nextHops[1].oif, 4);
        PHPVPN_CHECK(decodedRoute->nextHops[1].gateway == ip("192.0.2.2"));
    }

    // The session route decodes back to exactly what was encoded.
    netlink::RouteSpec spec;
    spec.oif = 9;
    spec.destination = ip("10.99.99.0");
    spec.destinationLength = 24;
    spec.metric = 5;
    Bytes encoded = netlink::encodeRouteAdd(3, spec);
    auto roundTrip = netlink::decodeRoute(encoded);
    PHPVPN_REQUIRE_OK(roundTrip);
    PHPVPN_CHECK(roundTrip->destination == ip("10.99.99.0"));
    PHPVPN_CHECK_EQ(roundTrip->priority, 5u);
    PHPVPN_CHECK_EQ(roundTrip->nextHops.size(), 1u);
    PHPVPN_CHECK_EQ(roundTrip->nextHops.front().oif, 9);
    PHPVPN_CHECK(!roundTrip->nextHops.front().gateway.has_value());
}

PHPVPN_TEST(InterfaceIdentity, ComposesTagAndIfindex) {
    const std::uint64_t identity = composeInterfaceIdentity(0x2319aedcu, 42);
    PHPVPN_CHECK_EQ(identity, 0x2319aedc0000002aull);
    PHPVPN_CHECK_EQ(identityOwnerTag(identity), 0x2319aedcu);
    PHPVPN_CHECK_EQ(identityIfindex(identity), 42);
    // Foreign interfaces carry tag 0, so their identity is the bare ifindex.
    PHPVPN_CHECK_EQ(composeInterfaceIdentity(0, 42), 42ull);
    PHPVPN_CHECK_NE(composeInterfaceIdentity(0, 42), identity);
}

PHPVPN_TEST(InterfaceIdentity, TagIsSha256PrefixOfCanonicalGuid) {
    auto guid = canonicalInstallationGuid(L"{3F6C2A91-5B7E-4D0A-9C1E-8A2B7D4E6F10}");
    PHPVPN_REQUIRE_OK(guid);
    PHPVPN_CHECK_EQ(guid.value(), std::string("{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}"));
    // printf '%s' '{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}' | sha256sum -> 2319aedc...
    PHPVPN_CHECK_EQ(ownerTagForGuid(guid.value()), 0x2319aedcu);
    PHPVPN_CHECK_EQ(ownershipMarker(guid.value()),
                    std::string("phpvpn-owner:{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}"));

    PHPVPN_CHECK(!canonicalInstallationGuid(L"3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10").ok());
    PHPVPN_CHECK(!canonicalInstallationGuid(L"{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f1g}").ok());
    PHPVPN_CHECK(!canonicalInstallationGuid(L"{3f6c2a91x5b7e-4d0a-9c1e-8a2b7d4e6f10}").ok());
    PHPVPN_CHECK(!canonicalInstallationGuid(L"{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}x").ok());
}

PHPVPN_TEST(InterfaceIdentity, AliasTagRequiresTheExactMarker) {
    const std::string marker = "phpvpn-owner:{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}";
    PHPVPN_CHECK_EQ(ownerTagForAlias(marker), 0x2319aedcu);
    PHPVPN_CHECK_EQ(ownerTagForAlias(""), 0u);
    PHPVPN_CHECK_EQ(ownerTagForAlias("phpvpn-owner:"), 0u);
    PHPVPN_CHECK_EQ(ownerTagForAlias(marker + " "), 0u);
    PHPVPN_CHECK_EQ(ownerTagForAlias(" " + marker), 0u);
    PHPVPN_CHECK_EQ(ownerTagForAlias("phpvpn-owner:{3F6C2A91-5B7E-4D0A-9C1E-8A2B7D4E6F10}"), 0u);
    PHPVPN_CHECK_EQ(ownerTagForAlias("PHPVPN-OWNER:{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}"), 0u);
    // Another canonical GUID yields another, non-zero tag.
    const std::uint32_t other = ownerTagForAlias("phpvpn-owner:{00000000-0000-0000-0000-000000000001}");
    PHPVPN_CHECK_NE(other, 0u);
    PHPVPN_CHECK_NE(other, 0x2319aedcu);
}

PHPVPN_TEST(DnsSnapshot, ParsesIpv4NameserversOnly) {
    const auto servers = parseResolvConfNameservers(
        "# comment nameserver 9.9.9.9\n"
        "; nameserver 8.8.4.4\n"
        "search example.org\n"
        "nameserver 127.0.0.53\n"
        "  nameserver\t192.0.2.53   # trailing\r\n"
        "nameserver ::1\n"
        "nameserver 010.1.1.1\n"
        "nameserver 1.2.3\n"
        "nameservers 5.5.5.5\n"
        "nameserver 127.0.0.53\n"
        "nameserver 203.0.113.9");
    const std::vector<std::string> expected = {"127.0.0.53", "192.0.2.53", "127.0.0.53",
                                               "203.0.113.9"};
    PHPVPN_CHECK(servers == expected);
}

PHPVPN_TEST(DnsSnapshot, ReadsBoundedRegularFilesAndSortsWithoutDuplicates) {
    const auto path = temporaryFile("resolv.conf",
                                    "nameserver 192.0.2.9\nnameserver 10.0.0.1\nnameserver 192.0.2.9\n");
    DnsSnapshotOptions options;
    options.resolvConfPath = path;
    options.queryResolved = false;
    auto servers = collectDnsServers(options);
    PHPVPN_REQUIRE_OK(servers);
    const std::vector<std::string> expected = {"10.0.0.1", "192.0.2.9"};
    PHPVPN_CHECK(servers.value() == expected);

    // A missing file means no nameserver, a directory or an oversized file fails.
    options.resolvConfPath = path.string() + ".missing";
    auto missing = collectDnsServers(options);
    PHPVPN_REQUIRE_OK(missing);
    PHPVPN_CHECK(missing->empty());
    options.resolvConfPath = std::filesystem::temp_directory_path();
    PHPVPN_REQUIRE_ERROR(collectDnsServers(options), ErrorCode::netconfig_apply_failed);
    const auto large = temporaryFile("resolv-large.conf", std::string(70 * 1024, '#'));
    PHPVPN_REQUIRE_ERROR(readResolvConfNameservers(large), ErrorCode::netconfig_apply_failed);

    // A FIFO neither blocks the open nor counts as a file.
    const auto fifo = std::filesystem::temp_directory_path() /
                      ("phpvpn-wp04-" + std::to_string(::getpid()) + "-fifo");
    std::filesystem::remove(fifo);
    PHPVPN_CHECK_EQ(::mkfifo(fifo.c_str(), 0600), 0);
    PHPVPN_REQUIRE_ERROR(readResolvConfNameservers(fifo), ErrorCode::netconfig_apply_failed);

    std::filesystem::remove(path);
    std::filesystem::remove(large);
    std::filesystem::remove(fifo);
}

}  // namespace phpvpn
