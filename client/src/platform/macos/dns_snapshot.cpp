#include "platform/macos/dns_snapshot.h"

#include <SystemConfiguration/SystemConfiguration.h>

#include <algorithm>

#include "core/base/logger.h"
#include "core/protocol/ipv4.h"

namespace phpvpn {
namespace {

constexpr const char* kComponent = "dns-snapshot";
// The longest IPv4 text has 15 characters; a longer element is no IPv4 server
// and simply does not fit.
constexpr CFIndex kMaxAddressBytes = 64;

// Releases one object obtained from a CF Create or Copy function.
class CfReleaser {
public:
    explicit CfReleaser(CFTypeRef object) noexcept : object_(object) {}
    ~CfReleaser() {
        if (object_ != nullptr) CFRelease(object_);
    }
    CfReleaser(const CfReleaser&) = delete;
    CfReleaser& operator=(const CfReleaser&) = delete;

private:
    CFTypeRef object_;
};

}  // namespace

std::vector<std::string> parseDnsServerAddresses(CFArrayRef servers) {
    std::vector<std::string> result;
    if (servers == nullptr || CFGetTypeID(servers) != CFArrayGetTypeID()) return result;
    const CFIndex count = CFArrayGetCount(servers);
    for (CFIndex index = 0; index < count; ++index) {
        // Get rule: the array keeps ownership of its elements.
        const void* element = CFArrayGetValueAtIndex(servers, index);
        if (element == nullptr || CFGetTypeID(element) != CFStringGetTypeID()) continue;
        char text[kMaxAddressBytes] = {};
        if (!CFStringGetCString(static_cast<CFStringRef>(element), text, kMaxAddressBytes,
                                kCFStringEncodingUTF8)) {
            continue;
        }
        const auto address = Ipv4Address::parse(text);
        if (address.has_value()) result.push_back(address->toString());
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

Result<std::vector<std::string>> collectDnsServers() {
    const SCDynamicStoreRef store =
        SCDynamicStoreCreate(kCFAllocatorDefault, CFSTR("phpvpn-dns-snapshot"), nullptr, nullptr);
    if (store == nullptr) {
        return Error(ErrorCode::netconfig_apply_failed,
                     "the system configuration store could not be opened (status " +
                         std::to_string(SCError()) + ")");
    }
    const CfReleaser storeOwner(store);

    const CFPropertyListRef value = SCDynamicStoreCopyValue(store, CFSTR("State:/Network/Global/DNS"));
    if (value == nullptr) {
        // A key that does not exist is a system without a resolver
        // configuration; every other status is a store that could not answer.
        const int status = SCError();
        if (status == static_cast<int>(kSCStatusOK) || status == static_cast<int>(kSCStatusNoKey)) {
            PHPVPN_LOG_DEBUG(kComponent, "no global DNS configuration is published");
            return std::vector<std::string>();
        }
        return Error(ErrorCode::netconfig_apply_failed,
                     "the global DNS configuration could not be read (status " +
                         std::to_string(status) + ")");
    }
    const CfReleaser valueOwner(value);
    if (CFGetTypeID(value) != CFDictionaryGetTypeID()) {
        return Error(ErrorCode::netconfig_apply_failed,
                     "the global DNS configuration is not a dictionary");
    }

    // Get rule: the dictionary keeps ownership of the array.
    const void* servers =
        CFDictionaryGetValue(static_cast<CFDictionaryRef>(value), kSCPropNetDNSServerAddresses);
    if (servers == nullptr) {
        PHPVPN_LOG_DEBUG(kComponent, "the global DNS configuration lists no server");
        return std::vector<std::string>();
    }
    if (CFGetTypeID(servers) != CFArrayGetTypeID()) {
        return Error(ErrorCode::netconfig_apply_failed,
                     "the global DNS server list is not an array");
    }
    return parseDnsServerAddresses(static_cast<CFArrayRef>(servers));
}

}  // namespace phpvpn
