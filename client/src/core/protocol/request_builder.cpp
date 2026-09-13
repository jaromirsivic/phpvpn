#include "core/protocol/request_builder.h"

#include "core/base/hex.h"
#include "core/crypto/canonical.h"
#include "core/protocol/base64url.h"

#include <algorithm>
namespace phpvpn {
Result<std::string> RequestBuilder::buildInfoFull(CryptoProvider& crypto, const SecureBuffer& key,
    Ipv4Address src, std::string_view timestamp, std::string_view requestId, InfoRequestOptions options) {
    if (options.maskedCommunicationContracts) std::sort(options.maskedCommunicationContracts->begin(), options.maskedCommunicationContracts->end(), [](const auto& a, const auto& b) { return a.peer < b.peer; });
    std::string extensions;
    PHPVPN_TRY_ASSIGN(extensions, canonicalInfoExtensions(options, src));
    Sha256Digest hash{};
    PHPVPN_TRY_ASSIGN(hash, crypto.sha256(asBytes(extensions)));
    const auto canonical = infoFullCanonical(src.toString(), timestamp, requestId, sha256Hex(hash));
    Sha256Digest tag{};
    PHPVPN_TRY_ASSIGN(tag, crypto.hmacSha256(key.span(), asBytes(canonical)));
    JsonValue fields;
    PHPVPN_TRY_ASSIGN(fields, Json::parse(extensions));
    JsonObject root{{"type", JsonValue::makeString("auth-full")}, {"src", JsonValue::makeString(src.toString())},
        {"timestamp", JsonValue::makeString(std::string(timestamp))}, {"requestId", JsonValue::makeString(std::string(requestId))}, {"tag", JsonValue::makeString(Base64Url::encode(tag))}};
    for (auto& field : fields.objectValue()) root.push_back(std::move(field));
    auto out = Json::serialize(JsonValue::makeObject(std::move(root)));
    if (out.size() > kMaxInfoRequestBodyBytes) return Error(ErrorCode::response_invalid);
    return out;
}
namespace {

constexpr std::string_view kAuthFullPrefixSrc = R"({"type":"auth-full","src":")";
constexpr std::string_view kAuthFullPrefixTimestamp = R"(","timestamp":")";
constexpr std::string_view kAuthFullPrefixRequestId = R"(","requestId":")";
constexpr std::string_view kAuthFullPrefixTag = R"(","tag":")";
constexpr std::string_view kAuthFullSuffix = R"("})";

// An HMAC-SHA-256 tag is 32 bytes, so its canonical base64url text is always
// 43 characters long.
constexpr std::size_t kHmacTagTextLength = 43;

}  // namespace

std::string RequestBuilder::buildAuthNone(std::string_view requestId) {
    std::string out;
    out.reserve(48 + requestId.size());
    out += R"({"type":"auth-none","requestId":")";
    out += requestId;
    out += R"("})";
    return out;
}

Result<std::string> RequestBuilder::buildAuthSemi(CryptoProvider& crypto,
                                                  const SecureBuffer& semiKey,
                                                  std::string_view timestamp,
                                                  std::string_view requestId) {
    const std::string canonical = authSemiCanonical(timestamp, requestId);
    Sha256Digest tag{};
    PHPVPN_TRY_ASSIGN(tag, crypto.hmacSha256(semiKey.span(), asBytes(canonical)));
    const std::string tagText =
        Base64Url::encode(std::span<const std::uint8_t>(tag.data(), tag.size()));

    std::string out;
    out.reserve(72 + timestamp.size() + requestId.size() + tagText.size());
    out += R"({"type":"auth-semi","timestamp":")";
    out += timestamp;
    out += R"(","requestId":")";
    out += requestId;
    out += R"(","tag":")";
    out += tagText;
    out += R"("})";
    return out;
}

Result<std::string> RequestBuilder::buildAuthFull(CryptoProvider& crypto,
                                                  const SecureBuffer& fullAuthKey,
                                                  std::string_view src,
                                                  std::string_view timestamp,
                                                  std::string_view requestId,
                                                  std::string_view body) {
    Sha256Digest bodyDigest{};
    PHPVPN_TRY_ASSIGN(bodyDigest, crypto.sha256(asBytes(body)));
    const std::string bodyHex =
        sha256Hex(std::span<const std::uint8_t>(bodyDigest.data(), bodyDigest.size()));

    const std::string canonical = authFullCanonical(src, timestamp, requestId, bodyHex);
    Sha256Digest tag{};
    PHPVPN_TRY_ASSIGN(tag, crypto.hmacSha256(fullAuthKey.span(), asBytes(canonical)));
    const std::string tagText =
        Base64Url::encode(std::span<const std::uint8_t>(tag.data(), tag.size()));

    std::string out;
    out.reserve(authFullLineSize(src.size(), timestamp.size(), requestId.size()));
    out += kAuthFullPrefixSrc;
    out += src;
    out += kAuthFullPrefixTimestamp;
    out += timestamp;
    out += kAuthFullPrefixRequestId;
    out += requestId;
    out += kAuthFullPrefixTag;
    out += tagText;
    out += kAuthFullSuffix;
    return out;
}

std::size_t RequestBuilder::authFullLineSize(std::size_t srcLength,
                                             std::size_t timestampLength,
                                             std::size_t requestIdLength) noexcept {
    return kAuthFullPrefixSrc.size() + srcLength + kAuthFullPrefixTimestamp.size() +
           timestampLength + kAuthFullPrefixRequestId.size() + requestIdLength +
           kAuthFullPrefixTag.size() + kHmacTagTextLength + kAuthFullSuffix.size() + 1;
}

}  // namespace phpvpn
