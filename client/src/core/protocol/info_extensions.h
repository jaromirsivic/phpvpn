#pragma once
#include "core/config/config.h"
#include "core/protocol/json.h"
#include <optional>

namespace phpvpn {
inline constexpr std::size_t kMaxInfoRequestBodyBytes = 65536;
inline constexpr std::size_t kMaxMaskedCommunicationContracts = 252;
struct MaskedCommunicationContract {
    Ipv4Address peer;
    std::string hash;
};
struct InfoRequestOptions {
    std::optional<std::vector<MaskedCommunicationContract>> maskedCommunicationContracts;
    std::optional<bool> includeMaskedCommunicationContracts;
};
struct ServerInfoSnapshot {
    std::string infoPlaintext;
    Ipv4Address localAddress;
    std::vector<MaskedCommunicationContract> localMaskedCommunicationContracts;
};
bool validMaskedContractHash(std::string_view hash);
bool validInfoAddress(Ipv4Address address);
JsonValue maskedContractsJson(const std::vector<MaskedCommunicationContract> &contracts);
Result<std::vector<MaskedCommunicationContract>> parseMaskedContracts(const JsonValue &value, Ipv4Address owner);
Result<std::string> canonicalInfoExtensions(const InfoRequestOptions &options, Ipv4Address src);
std::string infoFullCanonical(std::string_view src, std::string_view timestamp, std::string_view requestId,
                              std::string_view hash);
} // namespace phpvpn
