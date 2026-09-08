#pragma once
#include "harness.h"
#include "core/config/config_defaults.h"
#include "core/ipc/ipc_protocol.h"
#include "core/presentation/contracts_model.h"
#include "core/presentation/password_key_model.h"
#include <algorithm>

namespace phpvpn::contracts_test {
inline Ipv4Address ip(std::string_view text) { return *Ipv4Address::parse(text); }
inline constexpr std::string_view id = "12345678-1234-4234-9234-123456789abc";
inline Result<std::size_t> envelopeSize(std::string_view document) {
    auto request = IpcProtocol::buildRequest(IpcCommand::set_config, id, document);
    const auto size = request.size();
    secureWipe(request.data(), request.size());
    return size;
}
inline std::string document(std::string local = "10.99.99.2", std::string cidr = "10.99.99.0/24",
                            bool contractsPresent = false) {
    auto dom = Json::parse(makeDefaultEditorDocument()).value();
    for (auto& [name, value] : dom.objectValue()) if (name == "masterSetup") {
        for (auto& [field, item] : value.objectValue()) {
            if (field == "virtualIp") item = JsonValue::makeString(local);
            if (field == "vpnCidr") item = JsonValue::makeString(cidr);
        }
    }
    if (contractsPresent) {
        for (auto& [name, value] : dom.objectValue()) if (name == "communicationContracts")
            value = JsonValue::makeObject({});
    } else std::erase_if(dom.objectValue(), [](const auto& item) { return item.first == "communicationContracts"; });
    return Json::serialize(dom);
}
inline std::vector<ContractEdit> edits(const ContractsModel& model) {
    std::vector<ContractEdit> result;
    for (const auto& row : model.rows()) result.push_back({row.remoteIp(), row.enabled(), row.keyText()});
    return result;
}
class Crypto final : public CryptoProvider {
public:
    std::unique_ptr<CryptoProvider> real = std::move(createPlatformCryptoProvider()).value();
    bool failHash = false, failRandom = false;
    std::size_t hashCalls = 0, randomCalls = 0, randomSize = 0;
    std::vector<std::uint8_t> lastInput; // synthetic test data only
    Result<Sha256Digest> sha256(std::span<const std::uint8_t> data) override {
        ++hashCalls;
        lastInput.assign(data.begin(), data.end());
        if (failHash) return Error(ErrorCode::crypto_failed, "synthetic provider detail must not escape");
        return real->sha256(data);
    }
    Status randomBytes(std::span<std::uint8_t> out) override {
        ++randomCalls; randomSize = out.size();
        for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<std::uint8_t>(i);
        return failRandom ? fail(ErrorCode::crypto_failed, "synthetic provider detail must not escape") : ok();
    }
    Result<Sha256Digest> hmacSha256(std::span<const std::uint8_t> key, std::span<const std::uint8_t> data) override {
        return real->hmacSha256(key, data);
    }
    Status aesGcmSeal(std::span<const std::uint8_t> key, std::span<const std::uint8_t> iv,
        std::span<const std::uint8_t> aad, std::span<const std::uint8_t> plain,
        std::vector<std::uint8_t>& cipher, GcmTag& tag) override { return real->aesGcmSeal(key, iv, aad, plain, cipher, tag); }
    Result<SecureBuffer> aesGcmOpen(std::span<const std::uint8_t> key, std::span<const std::uint8_t> iv,
        std::span<const std::uint8_t> aad, std::span<const std::uint8_t> cipher,
        std::span<const std::uint8_t> tag) override { return real->aesGcmOpen(key, iv, aad, cipher, tag); }
};
} // namespace phpvpn::contracts_test
