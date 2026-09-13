// Immutable configuration snapshot, client.md "Konfiguracni soubor".
//
// The service loads and strictly validates the file before every connection
// attempt. plan_client.md decision 9 splits the stored generation from the
// active generation: `set-config` only replaces the stored one, the active
// snapshot changes on `connect`.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/base/result.h"
#include "core/base/secure_buffer.h"
#include "core/protocol/ipv4.h"

namespace phpvpn {

// Shared defaults.
inline constexpr std::string_view kDefaultServerSecret = "CHANGE_THIS_SERVER_SECRET";
inline constexpr const char* kDefaultVpnCidr = "10.99.99.0/24";
inline constexpr bool kDefaultTrustAnyTlsCertificate = true;
inline constexpr std::int64_t kDefaultMaxOutboundQueueBytes = 67108864;
inline constexpr std::int64_t kDefaultMaxInboundQueueBytes = 67108864;
inline constexpr std::int64_t kDefaultMinPerPeerQueueBytes = 131072;
inline constexpr std::int64_t kDefaultMaxResponseBodyBytes = 67108864;
inline constexpr std::int64_t kDefaultMaxNdjsonLineBytes = 67108864;
inline constexpr std::int64_t kDefaultMaxDataRecordsPerResponse = 65535;
inline constexpr std::int64_t kDefaultResponseTimeoutSeconds = 30;
inline constexpr std::int64_t kDefaultTimeSyncIntervalSeconds = 300;

inline constexpr char kDefaultHttpHeaders[] = "Cache-Control: no-cache\r\nPragma: no-cache\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:155.0) Gecko/20100101 Firefox/155.0";

// Shared complete template for service startup and explicit editor creation/reset.
inline constexpr std::string_view kDefaultConfigDocument = R"json({
  "masterSetup": {
    "serverSecret": "CHANGE_THIS_SERVER_SECRET",
    "serverUrl": "http://127.0.0.1/phpvpn.php",
    "httpHeaders": "Cache-Control: no-cache\r\nPragma: no-cache\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:155.0) Gecko/20100101 Firefox/155.0",
    "trustAnyTlsCertificate": true,
    "useDefaultServerSecretInWizard": true,
    "vpnCidr": "10.99.99.0/24",
    "virtualIp": "10.99.99.2",
    "autoDetectIpAddressInWizard": true,
    "accessKey": "0202020202020202020202020202020202020202020202020202020202020202",
    "useDefaultAccessKeyInWizard": true
  },
  "communicationContracts": {
    "10.99.99.3": {
      "payloadKey": "918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d"
    }
  }
}
)json";

// Absolute local polling bounds of client.md "Pollingovy interval".
inline constexpr std::int64_t kMinPollIntervalMs = 20;
inline constexpr std::int64_t kMaxPollIntervalMs = 5000;

struct RuntimeLimits {
    std::int64_t maxOutboundQueueBytes = kDefaultMaxOutboundQueueBytes;
    std::int64_t maxInboundQueueBytes = kDefaultMaxInboundQueueBytes;
    std::int64_t minPerPeerQueueBytes = kDefaultMinPerPeerQueueBytes;
    std::int64_t maxResponseBodyBytes = kDefaultMaxResponseBodyBytes;
    std::int64_t maxNdjsonLineBytes = kDefaultMaxNdjsonLineBytes;
    std::int64_t maxDataRecordsPerResponse = kDefaultMaxDataRecordsPerResponse;
    std::int64_t responseTimeoutSeconds = kDefaultResponseTimeoutSeconds;
    std::int64_t timeSyncIntervalSeconds = kDefaultTimeSyncIntervalSeconds;
};

// Parsed server URL. plan_client.md decision 6 forbids userinfo, a query and a
// fragment, because `?action=<action>` is appended verbatim.
struct ServerUrl {
    bool secure = false;  // https
    std::string host;
    std::uint16_t port = 0;
    std::string path;  // begins with '/'
    std::string original;
};

struct CommunicationContract {
    Ipv4Address peer;
    SecureBuffer payloadKey;  // exactly 32 bytes
};

class ClientConfig {
public:
    ServerUrl serverUrl;
    bool trustAnyTlsCertificate = kDefaultTrustAnyTlsCertificate;
    std::string serverSecret;  // exact configured bytes, normalised on use
    Ipv4Cidr vpnCidr;
    Ipv4Address virtualIp;
    SecureBuffer accessKey;  // exactly 32 bytes
    RuntimeLimits runtime;
    std::vector<CommunicationContract> contracts;

    // Number of remote peers the prefix can hold once the local address is
    // excluded. Drives the admission reservation of both directions.
    std::uint32_t maxRemotePeers() const noexcept;

    const CommunicationContract* findContract(Ipv4Address peer) const noexcept;

    // Raw document as it was parsed, so `get-config` can return the exact JSON
    // the administrator stored without reformatting the secrets.
    std::string rawJson;

    // Increases with every successful `set-config`, plan_client.md decision 9.
    std::uint64_t generation = 0;
    std::string httpHeaders;
};

using ClientConfigPtr = std::shared_ptr<const ClientConfig>;

// Non fatal observations that `get-diagnostics` reports as safe warnings.
struct ConfigWarnings {
    bool trustsAnyTlsCertificate = false;
    bool usesPlainHttp = false;
    bool serverSecretPredictable = false;

    std::vector<std::string> messages() const;
};

// Parses and fully validates a configuration document. Never places a key value
// into the error detail.
Result<ClientConfigPtr> parseAndValidateConfig(std::string_view json, ConfigWarnings& warnings);

// URL parsing exposed for the unit tests.
Result<ServerUrl> parseServerUrl(std::string_view text);

}  // namespace phpvpn
