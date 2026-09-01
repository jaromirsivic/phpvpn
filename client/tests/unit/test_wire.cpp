// Wire formats: UUIDv4, the data record, the request builders and the NDJSON
// response reader, protocol.md chapters 6, 8 and 9.

#include <string>
#include <vector>

#include "core/base/hex.h"
#include "core/crypto/canonical.h"
#include "core/crypto/crypto_provider.h"
#include "core/crypto/key_derivation.h"
#include "core/protocol/base64url.h"
#include "core/protocol/data_record.h"
#include "core/protocol/ipv4.h"
#include "core/protocol/ndjson_reader.h"
#include "core/protocol/request_builder.h"
#include "core/protocol/response_parser.h"
#include "core/protocol/uuid.h"
#include "harness.h"

namespace phpvpn {
namespace {

constexpr const char* kRequestId = "8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c30";
constexpr const char* kMessageId = "1f4d8c1a-3b2e-4a51-8c7d-9e0f1a2b3c4d";

CryptoProvider& provider() {
    static std::unique_ptr<CryptoProvider> instance = [] {
        auto created = createPlatformCryptoProvider();
        return created.ok() ? std::move(created).value() : nullptr;
    }();
    return *instance;
}

SecureBuffer keyOfLength(std::uint8_t seed) {
    std::vector<std::uint8_t> bytes(32);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return SecureBuffer(bytes);
}

}  // namespace

PHPVPN_TEST(wire, uuid_v4_validation) {
    PHPVPN_CHECK(Uuid::isValidV4(kRequestId));
    PHPVPN_CHECK(Uuid::isValidV4("00000000-0000-4000-8000-000000000000"));
    PHPVPN_CHECK(Uuid::isValidV4("ffffffff-ffff-4fff-bfff-ffffffffffff"));

    // Uppercase, a wrong version, a wrong variant, a wrong length and a missing
    // separator are all invalid.
    PHPVPN_CHECK(!Uuid::isValidV4("8A1F9A26-6D0C-4C58-9F1D-2F0A7F6B1C30"));
    PHPVPN_CHECK(!Uuid::isValidV4("8a1f9a26-6d0c-3c58-9f1d-2f0a7f6b1c30"));
    PHPVPN_CHECK(!Uuid::isValidV4("8a1f9a26-6d0c-4c58-7f1d-2f0a7f6b1c30"));
    PHPVPN_CHECK(!Uuid::isValidV4("8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c3"));
    PHPVPN_CHECK(!Uuid::isValidV4("8a1f9a266d0c4c589f1d2f0a7f6b1c30"));
}

PHPVPN_TEST(wire, uuid_v4_formatting_sets_version_and_variant) {
    std::vector<std::uint8_t> random(16, 0xFF);
    const std::string text = Uuid::formatV4(random);
    PHPVPN_CHECK(Uuid::isValidV4(text));
    PHPVPN_CHECK_EQ(text[14], '4');
    PHPVPN_CHECK(text[19] == '8' || text[19] == '9' || text[19] == 'a' || text[19] == 'b');
}

PHPVPN_TEST(wire, auth_none_is_compact_and_ordered) {
    PHPVPN_CHECK_EQ(RequestBuilder::buildAuthNone(kRequestId),
                    std::string(R"({"type":"auth-none","requestId":")") + kRequestId + "\"}");
}

PHPVPN_TEST(wire, auth_semi_carries_the_members_in_order) {
    const SecureBuffer key = keyOfLength(1);
    auto body = RequestBuilder::buildAuthSemi(provider(), key, "2024-01-01T00:00:00.000Z",
                                              kRequestId);
    PHPVPN_REQUIRE_OK(body);

    const std::string& text = body.value();
    const std::size_t type = text.find("\"type\"");
    const std::size_t timestamp = text.find("\"timestamp\"");
    const std::size_t requestId = text.find("\"requestId\"");
    const std::size_t tag = text.find("\"tag\"");
    PHPVPN_CHECK(type < timestamp && timestamp < requestId && requestId < tag);
    PHPVPN_CHECK(text.find(' ') == std::string::npos);
    PHPVPN_CHECK(text.find('\n') == std::string::npos);
}

PHPVPN_TEST(wire, auth_full_line_size_matches_the_serialised_line) {
    const SecureBuffer key = keyOfLength(7);
    const std::string source = "10.99.99.2";
    const std::string timestamp = "2024-01-01T00:00:00.000Z";

    auto line = RequestBuilder::buildAuthFull(provider(), key, source, timestamp, kRequestId,
                                              "body");
    PHPVPN_REQUIRE_OK(line);

    // The predicted size includes the terminating LF that the body adds.
    PHPVPN_CHECK_EQ(line.value().size() + 1,
                    RequestBuilder::authFullLineSize(source.size(), timestamp.size(),
                                                     std::string_view(kRequestId).size()));
}

PHPVPN_TEST(wire, data_record_round_trips) {
    const std::string iv = Base64Url::encode(std::vector<std::uint8_t>(12, 0x11));
    const std::string tag = Base64Url::encode(std::vector<std::uint8_t>(16, 0x22));
    const std::string ciphertext = Base64Url::encode(std::vector<std::uint8_t>(48, 0x33));

    const std::string line =
        DataRecord::build(kMessageId, "10.99.99.2", "10.99.99.3", iv, tag, ciphertext);

    PHPVPN_CHECK_EQ(line.size() + 1,
                    DataRecord::encodedSizeWithLineFeed(std::string_view(kMessageId).size(),
                                                        10, 10, iv.size(), tag.size(),
                                                        ciphertext.size()));

    auto parsed = DataRecord::parse(line);
    PHPVPN_REQUIRE_OK(parsed);
    PHPVPN_CHECK_EQ(std::string(parsed.value().messageId), std::string(kMessageId));
    PHPVPN_CHECK_EQ(std::string(parsed.value().src), std::string("10.99.99.2"));
    PHPVPN_CHECK_EQ(std::string(parsed.value().dst), std::string("10.99.99.3"));
    PHPVPN_CHECK_EQ(std::string(parsed.value().iv), iv);
    PHPVPN_CHECK_EQ(std::string(parsed.value().tag), tag);
    PHPVPN_CHECK_EQ(std::string(parsed.value().ciphertext), ciphertext);
}

PHPVPN_TEST(wire, data_record_rejects_reordered_and_padded_records) {
    const std::string iv = Base64Url::encode(std::vector<std::uint8_t>(12, 0x11));
    const std::string tag = Base64Url::encode(std::vector<std::uint8_t>(16, 0x22));
    const std::string ciphertext = Base64Url::encode(std::vector<std::uint8_t>(48, 0x33));
    const std::string valid =
        DataRecord::build(kMessageId, "10.99.99.2", "10.99.99.3", iv, tag, ciphertext);

    // Whitespace anywhere outside a string value is a hard error.
    PHPVPN_CHECK(!DataRecord::parse(std::string("{ ") + valid.substr(1)).ok());
    PHPVPN_CHECK(!DataRecord::parse(valid + " ").ok());

    // A member after `ciphertext` and a swapped order are both refused.
    std::string extra = valid;
    extra.insert(extra.size() - 1, R"(,"extra":"x")");
    PHPVPN_CHECK(!DataRecord::parse(extra).ok());

    const std::string swapped =
        std::string(R"({"type":"data","messageId":")") + kMessageId +
        R"(","dst":"10.99.99.3","src":"10.99.99.2","iv":")" + iv + R"(","tag":")" + tag +
        R"(","ciphertext":")" + ciphertext + R"("})";
    PHPVPN_CHECK(!DataRecord::parse(swapped).ok());
}

PHPVPN_TEST(wire, data_record_rejects_a_non_canonical_iv_or_tag) {
    const std::string ciphertext = Base64Url::encode(std::vector<std::uint8_t>(48, 0x33));
    const std::string shortIv = Base64Url::encode(std::vector<std::uint8_t>(11, 0x11));
    const std::string tag = Base64Url::encode(std::vector<std::uint8_t>(16, 0x22));

    PHPVPN_CHECK(
        !DataRecord::parse(
             DataRecord::build(kMessageId, "10.99.99.2", "10.99.99.3", shortIv, tag, ciphertext))
             .ok());

    const std::string iv = Base64Url::encode(std::vector<std::uint8_t>(12, 0x11));
    const std::string shortTag = Base64Url::encode(std::vector<std::uint8_t>(15, 0x22));
    PHPVPN_CHECK(
        !DataRecord::parse(
             DataRecord::build(kMessageId, "10.99.99.2", "10.99.99.3", iv, shortTag, ciphertext))
             .ok());

    // An empty ciphertext is never valid.
    PHPVPN_CHECK(
        !DataRecord::parse(DataRecord::build(kMessageId, "10.99.99.2", "10.99.99.3", iv, tag, ""))
             .ok());
}

PHPVPN_TEST(wire, ndjson_response_requires_a_verified_data_mac) {
    const SecureBuffer fullAuthKey = keyOfLength(9);

    const std::string dataResult =
        std::string(R"({"type":"data-result","requestId":")") + kRequestId +
        R"(","acceptedMessageIds":[],"deferredMessageIds":[],"acceptedRecords":0,"acceptedBytes":0})";

    auto digest = provider().sha256(asBytes(dataResult));
    PHPVPN_REQUIRE_OK(digest);

    const std::string canonical =
        dataMacCanonical(kRequestId, std::span<const std::pair<std::string, std::string>>(),
                         sha256Hex(digest.value()));
    auto mac = provider().hmacSha256(fullAuthKey.span(), asBytes(canonical));
    PHPVPN_REQUIRE_OK(mac);

    const std::string body = dataResult + "\n" + R"({"type":"data-mac","requestId":")" +
                             kRequestId + R"(","tag":")" + Base64Url::encode(mac.value()) +
                             "\"}\n";

    NdjsonLimits limits;
    auto parsed = NdjsonResponseReader::parseAndVerify(provider(), fullAuthKey, kRequestId, body,
                                                       limits);
    PHPVPN_REQUIRE_OK(parsed);
    PHPVPN_CHECK(parsed.value().records.empty());
    PHPVPN_CHECK(parsed.value().acceptedMessageIds.empty());

    // A single flipped byte in the canonical input invalidates the response.
    std::string tampered = body;
    tampered[tampered.size() - 3] = tampered[tampered.size() - 3] == 'A' ? 'B' : 'A';
    auto rejected = NdjsonResponseReader::parseAndVerify(provider(), fullAuthKey, kRequestId,
                                                         tampered, limits);
    PHPVPN_CHECK(!rejected.ok());
}

PHPVPN_TEST(wire, ndjson_response_rejects_a_foreign_request_id) {
    const SecureBuffer fullAuthKey = keyOfLength(9);
    const std::string body =
        std::string(R"({"type":"data-result","requestId":")") + kMessageId +
        R"(","acceptedMessageIds":[],"deferredMessageIds":[],"acceptedRecords":0,"acceptedBytes":0})"
        "\n" +
        R"({"type":"data-mac","requestId":")" + kMessageId + R"(","tag":"AAAA"})" + "\n";

    NdjsonLimits limits;
    auto parsed = NdjsonResponseReader::parseAndVerify(provider(), fullAuthKey, kRequestId, body,
                                                       limits);
    PHPVPN_CHECK(!parsed.ok());
}

PHPVPN_TEST(wire, ndjson_response_rejects_a_missing_or_misplaced_trailer) {
    const SecureBuffer fullAuthKey = keyOfLength(9);
    NdjsonLimits limits;

    const std::string onlyResult =
        std::string(R"({"type":"data-result","requestId":")") + kRequestId +
        R"(","acceptedMessageIds":[],"deferredMessageIds":[],"acceptedRecords":0,"acceptedBytes":0})"
        "\n";
    PHPVPN_CHECK(!NdjsonResponseReader::parseAndVerify(provider(), fullAuthKey, kRequestId,
                                                       onlyResult, limits)
                      .ok());

    const std::string swapped =
        std::string(R"({"type":"data-mac","requestId":")") + kRequestId + R"(","tag":"AAAA"})" +
        "\n" + R"({"type":"data-result","requestId":")" + kRequestId +
        R"(","acceptedMessageIds":[],"deferredMessageIds":[],"acceptedRecords":0,"acceptedBytes":0})"
        "\n";
    PHPVPN_CHECK(
        !NdjsonResponseReader::parseAndVerify(provider(), fullAuthKey, kRequestId, swapped, limits)
             .ok());

    PHPVPN_CHECK(
        !NdjsonResponseReader::parseAndVerify(provider(), fullAuthKey, kRequestId, "", limits).ok());
}

PHPVPN_TEST(wire, server_errors_map_onto_stable_codes) {
    PHPVPN_CHECK_EQ(ResponseParser::mapServerError(400, ""), ErrorCode::server_bad_request);
    PHPVPN_CHECK_EQ(ResponseParser::mapServerError(401, ""), ErrorCode::server_unauthorized);
    PHPVPN_CHECK_EQ(ResponseParser::mapServerError(404, ""), ErrorCode::server_not_found);
    PHPVPN_CHECK_EQ(ResponseParser::mapServerError(405, ""), ErrorCode::server_method_not_allowed);
    PHPVPN_CHECK_EQ(ResponseParser::mapServerError(413, ""), ErrorCode::server_payload_too_large);
    PHPVPN_CHECK_EQ(ResponseParser::mapServerError(415, ""),
                    ErrorCode::server_unsupported_media_type);
    PHPVPN_CHECK_EQ(ResponseParser::mapServerError(500, ""), ErrorCode::server_internal_error);
    PHPVPN_CHECK_EQ(ResponseParser::mapServerError(503, ""), ErrorCode::server_unavailable);
    PHPVPN_CHECK_EQ(ResponseParser::mapServerError(418, ""), ErrorCode::server_unexpected_status);
}

}  // namespace phpvpn
