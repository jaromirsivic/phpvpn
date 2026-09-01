// Verification of the `health` and `info` responses and of the server error
// body, protocol.md chapters 5.2, 8.3 and 13.
//
// Nothing from a response is used before its authentication tag verified: the
// health timestamp only after the GMAC, the info plaintext only after the GCM
// tag over the whole ciphertext.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/base/result.h"
#include "core/base/secure_buffer.h"
#include "core/crypto/crypto_provider.h"
#include "core/crypto/key_derivation.h"

namespace phpvpn {

struct HealthResponse {
    std::string serverVersion;
    std::string status;
    std::string description;
    std::string timestamp;
    std::int64_t timestampMs = 0;
};

// Server limits taken from the verified `info` plaintext.
struct ServerLimits {
    std::int64_t maxDataRecordsPerRequest = 16;
    std::int64_t maxRequestBodyBytes = 4198400;
    std::int64_t maxQueueBytesPerClient = 16777216;
    std::int64_t queueTtlSeconds = 60;
    std::int64_t minDataPollIntervalMs = 80;
    std::int64_t maxDataPollIntervalMs = 1000;
};

struct InfoResponse {
    std::string serverVersion;
    std::string generatedAt;
    ServerLimits limits;
    // Exact plaintext bytes, forwarded verbatim by the IPC command `get-info`.
    std::string plaintext;
};

class ResponseParser {
public:
    // Validates the whole `health` object, derives the per requestId key and
    // verifies the 16 byte GMAC over the AAD with an empty plaintext.
    static Result<HealthResponse> parseHealth(CryptoProvider& crypto,
                                              KeyDerivation& keys,
                                              std::string_view serverSecret,
                                              std::string_view expectedRequestId,
                                              std::string_view body);

    // Validates the envelope, derives the key from the transmitted salt and
    // decrypts the plaintext. Unknown members of the plaintext are ignored, as
    // protocol.md chapter 14 requires.
    static Result<InfoResponse> parseInfo(CryptoProvider& crypto,
                                          KeyDerivation& keys,
                                          std::string_view serverSecret,
                                          std::string_view expectedRequestId,
                                          std::string_view body);

    // Maps `{"type":"error","code":"..."}` onto a stable client error code. A
    // body that cannot be parsed still yields the status based error.
    static ErrorCode mapServerError(int statusCode, std::string_view body);
};

}  // namespace phpvpn
