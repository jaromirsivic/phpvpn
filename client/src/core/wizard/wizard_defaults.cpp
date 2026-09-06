#include "core/wizard/wizard_defaults.h"
#include <algorithm>
#include "core/base/hex.h"

namespace phpvpn {
std::string defaultAccessKey(std::string_view text) {
    const auto ip = Ipv4Address::parse(text);
    if (!ip) return {};
    const std::uint8_t byte = static_cast<std::uint8_t>(ip->value() & 255);
    const auto pair = toLowercaseHex(std::span<const std::uint8_t>(&byte, 1));
    std::string key;
    for (int i = 0; i < 32; ++i) key += pair;
    return key;
}
bool supportsDefaultContracts(const Ipv4Cidr& cidr) noexcept {
    return cidr.prefixLength() == 24 && (cidr.network().value() >> 24) == 10;
}
Result<std::string> defaultPayloadKey(CryptoProvider& crypto, Ipv4Address first, Ipv4Address second) {
    if (second < first) std::swap(first, second);
    auto hash = crypto.sha256(asBytes(first.toString() + "_" + second.toString()));
    if (!hash) return hash.error();
    return toLowercaseHex(hash.value());
}
std::optional<Ipv4Address> suggestWizardIp(const WizardDocument& document, const std::vector<SeenClient>& clients) {
    std::optional<Ipv4Address> oldest;
    std::int64_t oldestTime = 0;
    for (std::uint32_t octet = 2; octet <= 15; ++octet) {
        const Ipv4Address candidate(0x0a636300 | octet);
        if (!document.context.vpnCidr.isValidClientAddress(candidate) ||
            std::find(document.contractPeers.begin(), document.contractPeers.end(), candidate) != document.contractPeers.end()) continue;
        const auto seen = std::find_if(clients.begin(), clients.end(), [&](const SeenClient& value) { return value.address == candidate; });
        if (seen == clients.end()) return candidate;
        if (!oldest || seen->lastSeenMs < oldestTime) {
            oldest = candidate;
            oldestTime = seen->lastSeenMs;
        }
    }
    return oldest;
}
}
