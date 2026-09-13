// Builders for the three request bodies, protocol.md chapters 5.2, 8.1 and 9.
//
// Every object is compact, has its members in the exact documented order and
// carries no whitespace, so the bytes that are hashed are the bytes that are
// sent.
#pragma once
#include "core/protocol/info_extensions.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/base/result.h"
#include "core/base/secure_buffer.h"
#include "core/crypto/crypto_provider.h"

namespace phpvpn {

class RequestBuilder {
public:
    static Result<std::string> buildInfoFull(CryptoProvider& crypto, const SecureBuffer& key,
        Ipv4Address src, std::string_view timestamp, std::string_view requestId, InfoRequestOptions options);
    // {"type":"auth-none","requestId":"..."}
    static std::string buildAuthNone(std::string_view requestId);

    // {"type":"auth-semi","timestamp":"...","requestId":"...","tag":"..."}
    static Result<std::string> buildAuthSemi(CryptoProvider& crypto,
                                             const SecureBuffer& semiKey,
                                             std::string_view timestamp,
                                             std::string_view requestId);

    // {"type":"auth-full","src":"...","timestamp":"...","requestId":"...","tag":"..."}
    // `body` holds every byte after the LF that terminates the auth-full line.
    static Result<std::string> buildAuthFull(CryptoProvider& crypto,
                                             const SecureBuffer& fullAuthKey,
                                             std::string_view src,
                                             std::string_view timestamp,
                                             std::string_view requestId,
                                             std::string_view body);

    // Exact serialised size of the auth-full line including its LF. The tag is
    // always 43 characters because it decodes to 32 bytes.
    static std::size_t authFullLineSize(std::size_t srcLength,
                                        std::size_t timestampLength,
                                        std::size_t requestIdLength) noexcept;
};

}  // namespace phpvpn
