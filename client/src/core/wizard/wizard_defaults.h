#pragma once
#include "core/config/config_document.h"
#include "core/crypto/crypto_provider.h"
#include "core/protocol/response_parser.h"

namespace phpvpn {
std::string defaultAccessKey(std::string_view ip);
bool supportsDefaultContracts(const Ipv4Cidr& cidr) noexcept;
std::vector<Ipv4Address> defaultContractCandidates(const Ipv4Cidr& cidr);
std::vector<Ipv4Address> missingDefaultContractPeers(const Ipv4Cidr& cidr, Ipv4Address own,
    const std::vector<Ipv4Address>& existing);
Result<std::string> defaultPayloadKey(CryptoProvider& crypto, Ipv4Address first, Ipv4Address second);
std::optional<Ipv4Address> suggestWizardIp(const WizardDocument& document, const std::vector<SeenClient>& clients);
}
