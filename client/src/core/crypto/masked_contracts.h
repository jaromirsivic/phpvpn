#pragma once
#include "core/crypto/crypto_provider.h"
#include "core/protocol/info_extensions.h"
namespace phpvpn {
std::string canonicalContractId(Ipv4Address a, Ipv4Address b);
Result<std::vector<MaskedCommunicationContract>> maskedCommunicationContracts(CryptoProvider &crypto,
                                                                              const ClientConfig &config);
} // namespace phpvpn
