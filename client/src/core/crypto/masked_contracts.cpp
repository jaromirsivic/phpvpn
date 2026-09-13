#include "core/crypto/masked_contracts.h"
#include "core/crypto/canonical.h"
#include <algorithm>
namespace phpvpn {
std::string canonicalContractId(Ipv4Address a, Ipv4Address b) {
    if (b < a)
        std::swap(a, b);
    return a.toString() + "_" + b.toString();
}
Result<std::vector<MaskedCommunicationContract>> maskedCommunicationContracts(CryptoProvider &crypto,
                                                                              const ClientConfig &config) {
    std::vector<MaskedCommunicationContract> result;
    for (const auto &c : config.contracts) {
        if (c.payloadKey.size() != 32 || c.peer == config.virtualIp || !config.vpnCidr.isValidClientAddress(c.peer))
            return Error(ErrorCode::response_invalid);
        auto keyHex = sha256Hex(c.payloadKey.span());
        std::string input = canonicalContractId(config.virtualIp, c.peer) + "_" + keyHex + "_" + config.serverSecret;
        secureWipe(keyHex.data(), keyHex.size());
        auto digest = crypto.sha256(asBytes(input));
        secureWipe(input.data(), input.size());
        if (!digest.ok())
            return digest.error();
        result.push_back({c.peer, sha256Hex(digest.value())});
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.peer < b.peer; });
    auto checked = parseMaskedContracts(maskedContractsJson(result), config.virtualIp);
    if (!checked.ok())
        return checked.error();
    return result;
}
} // namespace phpvpn
