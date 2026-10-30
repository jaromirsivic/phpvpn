// macOS platform tests of the SystemConfiguration DNS snapshot
// (plan-macos-v001 step 7).
//
// The parser gets CFArray fixtures built here; the live read of
// State:/Network/Global/DNS needs no privileges and only has to succeed on the
// runner, which may have no IPv4 resolver at all.
#include "harness.h"

#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/protocol/ipv4.h"
#include "platform/macos/dns_snapshot.h"

namespace phpvpn {
namespace {

// Builds a CFArray of the given elements; the array retains them, so the
// caller's references are released here.
CFArrayRef arrayOf(std::vector<CFTypeRef> values) {
    const CFArrayRef array = CFArrayCreate(kCFAllocatorDefault, values.data(),
                                           static_cast<CFIndex>(values.size()), &kCFTypeArrayCallBacks);
    for (const CFTypeRef value : values) {
        if (value != nullptr) CFRelease(value);
    }
    return array;
}

CFTypeRef stringOf(const char* text) {
    return CFStringCreateWithCString(kCFAllocatorDefault, text, kCFStringEncodingUTF8);
}

}  // namespace

PHPVPN_TEST(DnsSnapshotMacos, KeepsCanonicalIpv4ServersOnly) {
    int port = 53;
    const CFArrayRef servers = arrayOf({
        stringOf("192.168.1.1"),
        stringOf("fe80::1%en0"),
        stringOf("2001:db8::53"),
        stringOf("8.8.8.8"),
        stringOf("not an address"),
        stringOf("192.168.1.1"),
        stringOf("1.1.1.1"),
        stringOf("010.0.0.1"),
        stringOf("256.1.1.1"),
        stringOf(""),
        CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &port),
    });
    PHPVPN_CHECK(servers != nullptr);
    if (servers == nullptr) return;
    const std::vector<std::string> parsed = parseDnsServerAddresses(servers);
    CFRelease(servers);

    const std::vector<std::string> expected = {"1.1.1.1", "192.168.1.1", "8.8.8.8"};
    PHPVPN_CHECK_EQ(parsed, expected);
}

PHPVPN_TEST(DnsSnapshotMacos, EmptyAndMissingArraysAreEmpty) {
    PHPVPN_CHECK(parseDnsServerAddresses(nullptr).empty());

    const CFArrayRef empty = arrayOf({});
    PHPVPN_CHECK(empty != nullptr);
    if (empty == nullptr) return;
    PHPVPN_CHECK(parseDnsServerAddresses(empty).empty());
    CFRelease(empty);

    const CFArrayRef ipv6Only = arrayOf({stringOf("2001:4860:4860::8888")});
    PHPVPN_CHECK(ipv6Only != nullptr);
    if (ipv6Only == nullptr) return;
    PHPVPN_CHECK(parseDnsServerAddresses(ipv6Only).empty());
    CFRelease(ipv6Only);
}

PHPVPN_TEST(DnsSnapshotMacos, ReadsTheLiveStore) {
    const auto servers = collectDnsServers();
    PHPVPN_REQUIRE_OK(servers);
    PHPVPN_CHECK(std::is_sorted(servers->begin(), servers->end()));
    PHPVPN_CHECK(std::adjacent_find(servers->begin(), servers->end()) == servers->end());
    for (const std::string& server : servers.value()) {
        const auto parsed = Ipv4Address::parse(server);
        PHPVPN_CHECK(parsed.has_value() && parsed->toString() == server);
    }
}

}  // namespace phpvpn
