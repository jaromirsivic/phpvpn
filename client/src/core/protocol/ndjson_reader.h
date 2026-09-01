// Reader and verifier of the `data` response, protocol.md chapter 9.5.
//
// The response is zero or more `data` lines, exactly one `data-result` and
// exactly one final `data-mac`. Verification has a fixed order: first the
// structure and the line order, then `data-result` with a matching requestId,
// then `data-mac` over the canonical string. Only after the constant time
// comparison succeeds may `acceptedMessageIds` be used and the individual
// records be decrypted.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/base/result.h"
#include "core/base/secure_buffer.h"
#include "core/crypto/crypto_provider.h"

namespace phpvpn {

struct DataResponseRecord {
    std::string messageId;
    std::string src;
    std::string dst;
    std::string ivText;   // exact base64url text as received
    std::string tagText;  // exact base64url text, also used for data-mac
    std::vector<std::uint8_t> iv;
    std::vector<std::uint8_t> tag;
    std::vector<std::uint8_t> ciphertext;
};

struct DataResponse {
    std::string requestId;
    std::vector<DataResponseRecord> records;
    std::vector<std::string> acceptedMessageIds;
    std::vector<std::string> deferredMessageIds;
    std::int64_t acceptedRecords = 0;
    std::int64_t acceptedBytes = 0;

    // Peak decoded ciphertext bytes of this response, reported as a diagnostic
    // counter by plan_client.md chapter 10.
    std::size_t decodedCiphertextBytes = 0;
};

struct NdjsonLimits {
    std::int64_t maxNdjsonLineBytes = 67108864;
    std::int64_t maxDataRecordsPerResponse = 65535;
};

class NdjsonResponseReader {
public:
    // Parses the complete body and verifies `data-mac`. Any failure invalidates
    // the whole response, so the caller removes nothing from its outbound queue
    // and forwards no payload.
    static Result<DataResponse> parseAndVerify(CryptoProvider& crypto,
                                               const SecureBuffer& fullAuthKey,
                                               std::string_view expectedRequestId,
                                               std::string_view body,
                                               const NdjsonLimits& limits);
};

}  // namespace phpvpn
