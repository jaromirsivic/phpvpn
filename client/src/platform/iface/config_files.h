// Fixed-path file boundary. JSON, UTF-8 and revision policy belong to ConfigStore.
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include "core/base/result.h"

namespace phpvpn {
struct ConfigFileIdentity {
    std::uint64_t volume = 0;
    std::uint64_t file = 0;
    bool operator==(const ConfigFileIdentity&) const = default;
};
struct ConfigFileRead {
    bool found = false;
    std::string bytes;
    std::optional<ConfigFileIdentity> identity;
    std::string sha256; // Exact complete input bytes only, never a partial hash.
};
enum class ConfigPublishMode { replace, createIfMissing };
enum class ConfigPublishState { confirmed, alreadyExists, unknown };
struct ConfigFilePublication {
    ConfigPublishState state = ConfigPublishState::unknown;
    std::string commitId;
    std::optional<ConfigFileIdentity> identity;
    std::string sha256;
};
enum class ConfigRecoveryState { clean, confirmed, notPublished, unknown };
struct ConfigFileRecovery {
    ConfigRecoveryState state = ConfigRecoveryState::clean;
    std::string commitId;
    std::optional<ConfigFileIdentity> identity;
    std::string sha256;
};

class ConfigFiles {
public:
    virtual ~ConfigFiles() = default;
    virtual const std::filesystem::path& path() const noexcept = 0;
    // Reads at most maxBytes + one detection byte, including concurrent growth.
    // Missing is a value, unreadable and too_large are distinct errors.
    virtual Result<ConfigFileRead> readBounded(std::size_t maxBytes) = 0;
    // Caller has already validated the whole document. Errors before publication
    // are definite failures. An ambiguous result is an explicit unknown value.
    // No operation accepts another path. createIfMissing never replaces a target.
    virtual Result<ConfigFilePublication> publish(std::string_view bytes,
        ConfigPublishMode mode, std::string_view commitId) = 0;
    // Never completes an unpublished commit. Does not remove an unrelated target.
    virtual Result<ConfigFileRecovery> recover() = 0;
};
}  // namespace phpvpn
