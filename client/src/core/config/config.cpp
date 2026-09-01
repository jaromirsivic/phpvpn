#include "core/config/config.h"

namespace phpvpn {

std::uint32_t ClientConfig::maxRemotePeers() const noexcept {
    const std::uint32_t total = vpnCidr.clientAddressCount();
    return total > 0 ? total - 1 : 0;
}

const CommunicationContract* ClientConfig::findContract(Ipv4Address peer) const noexcept {
    for (const CommunicationContract& contract : contracts) {
        if (contract.peer == peer) {
            return &contract;
        }
    }
    return nullptr;
}

std::vector<std::string> ConfigWarnings::messages() const {
    std::vector<std::string> out;
    if (trustsAnyTlsCertificate) {
        out.emplace_back(
            "trustAnyTlsCertificate is enabled, so any TLS certificate is accepted without "
            "verifying the trust chain or the host name. This is an accepted risk.");
    }
    if (usesPlainHttp) {
        out.emplace_back(
            "The server URL uses plain HTTP, so a passive observer sees the routing metadata "
            "and an attacker can replay a captured poll inside its time window.");
    }
    if (serverSecretPredictable) {
        out.emplace_back(
            "The server secret is a publicly predictable value, so the health response does "
            "not prove the identity of the server.");
    }
    return out;
}

}  // namespace phpvpn
