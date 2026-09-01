#include "core/protocol/data_record.h"

#include "core/protocol/base64url.h"
#include "core/protocol/ipv4.h"
#include "core/protocol/json_scanner.h"
#include "core/protocol/uuid.h"

namespace phpvpn {
namespace {

constexpr std::string_view kOrderedKeys[] = {
    "type", "messageId", "src", "dst", "iv", "tag", "ciphertext"};

// The literal fragments of the record. Deriving the fixed overhead from them
// keeps the scheduler measurement in step with the builder automatically.
constexpr std::string_view kPrefixMessageId = R"({"type":"data","messageId":")";
constexpr std::string_view kPrefixSrc = R"(","src":")";
constexpr std::string_view kPrefixDst = R"(","dst":")";
constexpr std::string_view kPrefixIv = R"(","iv":")";
constexpr std::string_view kPrefixTag = R"(","tag":")";
constexpr std::string_view kPrefixCiphertext = R"(","ciphertext":")";
constexpr std::string_view kSuffix = R"("})";

constexpr std::size_t kFixedOverhead = kPrefixMessageId.size() + kPrefixSrc.size() +
                                       kPrefixDst.size() + kPrefixIv.size() + kPrefixTag.size() +
                                       kPrefixCiphertext.size() + kSuffix.size();

}  // namespace

std::string DataRecord::build(std::string_view messageId,
                              std::string_view src,
                              std::string_view dst,
                              std::string_view ivBase64Url,
                              std::string_view tagBase64Url,
                              std::string_view ciphertextBase64Url) {
    std::string out;
    out.reserve(kFixedOverhead + messageId.size() + src.size() + dst.size() +
                ivBase64Url.size() + tagBase64Url.size() + ciphertextBase64Url.size());
    out += kPrefixMessageId;
    out += messageId;
    out += kPrefixSrc;
    out += src;
    out += kPrefixDst;
    out += dst;
    out += kPrefixIv;
    out += ivBase64Url;
    out += kPrefixTag;
    out += tagBase64Url;
    out += kPrefixCiphertext;
    out += ciphertextBase64Url;
    out += kSuffix;
    return out;
}

std::size_t DataRecord::encodedSizeWithLineFeed(std::size_t messageIdLength,
                                                std::size_t srcLength,
                                                std::size_t dstLength,
                                                std::size_t ivLength,
                                                std::size_t tagLength,
                                                std::size_t ciphertextLength) noexcept {
    return kFixedOverhead + messageIdLength + srcLength + dstLength + ivLength + tagLength +
           ciphertextLength + 1;
}

Result<DataRecordFields> DataRecord::parse(std::string_view line) {
    FlatObjectFields values;
    PHPVPN_TRY_ASSIGN(values, JsonScanner::parseOrderedCompactStringObject(line, kOrderedKeys));

    if (values[0] != "data") {
        return Error(ErrorCode::wire_invalid_json, "unexpected record type");
    }

    DataRecordFields fields;
    fields.messageId = values[1];
    fields.src = values[2];
    fields.dst = values[3];
    fields.iv = values[4];
    fields.tag = values[5];
    fields.ciphertext = values[6];

    if (!Uuid::isValidV4(fields.messageId)) {
        return Error(ErrorCode::wire_invalid_uuid, "messageId is not a UUID version 4");
    }
    if (!Ipv4Address::parse(fields.src).has_value() ||
        !Ipv4Address::parse(fields.dst).has_value()) {
        return Error(ErrorCode::wire_invalid_json, "src or dst is not a valid IPv4 address");
    }
    if (!Base64Url::isCanonicalWithByteLength(fields.iv, 12)) {
        return Error(ErrorCode::wire_invalid_base64url, "iv must decode to 12 bytes");
    }
    if (!Base64Url::isCanonicalWithByteLength(fields.tag, 16)) {
        return Error(ErrorCode::wire_invalid_base64url, "tag must decode to 16 bytes");
    }
    if (fields.ciphertext.empty() || !Base64Url::isCanonical(fields.ciphertext)) {
        return Error(ErrorCode::wire_invalid_base64url,
                     "ciphertext must be non empty canonical base64url");
    }

    return fields;
}

}  // namespace phpvpn
