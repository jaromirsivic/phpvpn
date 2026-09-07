#pragma once
#include <optional>
#include "core/config/config.h"
#include "core/protocol/json.h"
#include "core/crypto/crypto_provider.h"

namespace phpvpn {
inline constexpr std::size_t kMaxConfigFileBytes = 524288;
inline constexpr std::int64_t kMaxHttpTimeoutSeconds = 2147483;
inline constexpr std::string_view kWizardPlaceholderUrl = "http://MODIFY-THIS-URL/phpvpn.php";

enum class ConfigAvailability { missing, unreadable, encoding_error, too_large, invalid, repairable, valid };
const char* configAvailabilityName(ConfigAvailability value) noexcept;
enum class WizardMode { create, edit, repair, reset };
struct WizardFields {
    std::string serverUrl;
    std::string serverSecret;
    std::string virtualIp;
    std::string accessKey;
    bool useDefaultServerSecretInWizard = true;
    bool autoDetectIpAddressInWizard = true;
    bool useDefaultAccessKeyInWizard = true;
    bool operator==(const WizardFields&) const = default;
};
struct WizardContext {
    Ipv4Cidr vpnCidr;
    bool trustAnyTlsCertificate = kDefaultTrustAnyTlsCertificate;
    RuntimeLimits runtime;
};
// Incomplete input never becomes a runtime ClientConfig or uses dummy keys.
struct WizardPreferences {
    std::optional<bool> useDefaultServerSecretInWizard;
    std::optional<bool> autoDetectIpAddressInWizard;
    std::optional<bool> useDefaultAccessKeyInWizard;
};
struct ConfigValues {
    ServerUrl serverUrl;
    bool trustAnyTlsCertificate = kDefaultTrustAnyTlsCertificate;
    std::string serverSecret;
    Ipv4Cidr vpnCidr;
    std::optional<Ipv4Address> virtualIp;
    std::optional<SecureBuffer> accessKey;
    RuntimeLimits runtime;
    std::vector<CommunicationContract> contracts;
    std::string rawJson;
    WizardPreferences wizardPreferences;
};
struct WizardDocument {
    JsonValue dom;
    WizardFields fields;
    WizardContext context;
    std::vector<Ipv4Address> contractPeers;
    bool hasVirtualIp = false;
    bool hasAccessKey = false;
};
// Only a single leading UTF-8 BOM is removed. Invalid Unicode is never repaired.
Result<std::string> normalizeConfigText(std::string_view bytes);
bool isStrictUtf8(std::string_view text) noexcept;
Result<ConfigValues> validateConfigValues(std::string_view json, ConfigWarnings& warnings, bool allowMissingIdentity);
Result<WizardDocument> analyzeWizardDocument(std::string_view json);
Result<std::string> buildWizardDocument(CryptoProvider& crypto, const WizardDocument& original,
    WizardMode mode, const WizardFields& fields, bool createDefaultContracts);
Status validateWizardIdentity(const WizardDocument& document, const WizardFields& fields);
} // namespace phpvpn
