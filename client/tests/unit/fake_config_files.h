#pragma once
#include <functional>
#include "core/config/config_store.h"
#include "core/base/hex.h"
namespace phpvpn {
class FakeConfigFiles final : public ConfigFiles {
public:
    explicit FakeConfigFiles(CryptoProvider& provider) : crypto(provider) {}
    CryptoProvider& crypto;
    std::filesystem::path fixed = "C:/test/config.json";
    std::optional<std::string> bytes;
    std::optional<ErrorCode> readError;
    std::optional<ErrorCode> publishError;
    ConfigPublishState outcome = ConfigPublishState::confirmed;
    std::uint64_t identity = 1;
    unsigned reads = 0, writes = 0;
    std::function<void()> beforePublish;
    const std::filesystem::path& path() const noexcept override { return fixed; }
    Result<ConfigFileRead> readBounded(std::size_t limit) override {
        ++reads;
        if (readError) return Error(*readError);
        if (!bytes) return ConfigFileRead{};
        if (bytes->size() > limit) return Error(ErrorCode::config_too_large);
        auto digest = crypto.sha256({reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size()});
        if (!digest) return digest.error();
        return ConfigFileRead{true, *bytes, ConfigFileIdentity{1, identity}, toLowercaseHex(*digest)};
    }
    Result<ConfigFilePublication> publish(std::string_view text, ConfigPublishMode mode, std::string_view id) override {
        ++writes;
        if (beforePublish) beforePublish();
        if (publishError) return Error(*publishError, "PRIVATE-KEY-AND-RAW-JOURNAL");
        if (mode == ConfigPublishMode::createIfMissing && bytes)
            return ConfigFilePublication{ConfigPublishState::alreadyExists, std::string(id), {}, {}};
        if (outcome != ConfigPublishState::confirmed)
            return ConfigFilePublication{outcome, std::string(id), {}, {}};
        bytes = text;
        ++identity;
        auto digest = crypto.sha256({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
        if (!digest) return digest.error();
        return ConfigFilePublication{outcome, std::string(id), ConfigFileIdentity{1, identity}, toLowercaseHex(*digest)};
    }
    Result<ConfigFileRecovery> recover() override { return ConfigFileRecovery{}; }
};
}
