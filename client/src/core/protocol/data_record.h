// The `data` NDJSON record of protocol.md chapter 9.3.
//
// The record is a compact UTF-8 JSON object without whitespace outside the
// string values, with the members in exactly this order:
//   type, messageId, src, dst, iv, tag, ciphertext
// Nothing may follow `ciphertext`, whose closing quote is immediately followed
// by `}` and LF.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "core/base/result.h"

namespace phpvpn {

struct DataRecordFields {
    std::string_view messageId;
    std::string_view src;
    std::string_view dst;
    std::string_view iv;          // canonical base64url, 12 bytes
    std::string_view tag;         // canonical base64url, 16 bytes
    std::string_view ciphertext;  // canonical base64url, never empty
};

class DataRecord {
public:
    // Serialises one record without a trailing LF.
    static std::string build(std::string_view messageId,
                             std::string_view src,
                             std::string_view dst,
                             std::string_view ivBase64Url,
                             std::string_view tagBase64Url,
                             std::string_view ciphertextBase64Url);

    // Exact serialised size including the terminating LF, used by the request
    // scheduler to measure the assembled body before it is built.
    static std::size_t encodedSizeWithLineFeed(std::size_t messageIdLength,
                                               std::size_t srcLength,
                                               std::size_t dstLength,
                                               std::size_t ivLength,
                                               std::size_t tagLength,
                                               std::size_t ciphertextLength) noexcept;

    // Parses one record line without the terminating LF. Verifies the member
    // order, the absence of whitespace, canonical base64url and the field
    // lengths, but performs no cryptography and no routing decision.
    static Result<DataRecordFields> parse(std::string_view line);
};

}  // namespace phpvpn
