#include "core/protocol/response_parser.h"
#include <set>

#include "core/crypto/canonical.h"
#include "core/protocol/base64url.h"
#include "core/protocol/iso8601.h"
#include "core/protocol/json.h"
#include "core/protocol/json_scanner.h"
#include "core/protocol/uuid.h"

namespace phpvpn {
namespace {

// The seven members of a successful `health` response. Order is not semantic
// here, so the flat scanner accepts any order but no unknown or duplicate name.
constexpr std::string_view kHealthKeys[] = {
    "serverVersion", "status", "description", "timestamp", "requestId", "iv", "tag"};

constexpr std::string_view kInfoKeys[] = {"type", "requestId", "salt", "iv", "ciphertext", "tag"};

constexpr std::string_view kErrorKeys[] = {"type", "code"};

Result<std::int64_t> requireInteger(const JsonValue& object, std::string_view key) {
    const JsonValue* value = object.find(key);
    if (value == nullptr || !value->isInteger()) {
        return Error(ErrorCode::response_invalid,
                     std::string("info limits are missing ") + std::string(key));
    }
    if (value->integerValue() < 0) {
        return Error(ErrorCode::response_invalid, "an info limit is negative");
    }
    return value->integerValue();
}

}  // namespace

Result<HealthSyntax> ResponseParser::parseHealthSyntax(std::string_view expectedRequestId, std::string_view body) {
    FlatObjectFields fields;
    PHPVPN_TRY_ASSIGN(fields, JsonScanner::parseFlatStringObject(body, kHealthKeys));

    const std::string_view serverVersion = fields[0];
    const std::string_view status = fields[1];
    const std::string_view description = fields[2];
    const std::string_view timestamp = fields[3];
    const std::string_view requestId = fields[4];
    const std::string_view ivText = fields[5];
    const std::string_view tagText = fields[6];

    // The response must belong to the attempt that is running right now.
    if (!Uuid::isValidV4(requestId)) {
        return Error(ErrorCode::response_invalid, "health requestId is not a UUID version 4");
    }
    if (requestId != expectedRequestId) {
        return Error(ErrorCode::response_request_id_mismatch,
                     "health response belongs to another request");
    }
    if (status != "running") {
        return Error(ErrorCode::response_invalid, "health status is not running");
    }

    const auto timestampMs = Iso8601::parse(timestamp);
    if (!timestampMs.has_value()) {
        return Error(ErrorCode::wire_invalid_timestamp, "health timestamp is not a valid UTC value");
    }
    if (!Base64Url::isCanonicalWithByteLength(ivText, kGcmIvSize)) {
        return Error(ErrorCode::wire_invalid_base64url, "health iv must decode to 12 bytes");
    }
    if (!Base64Url::isCanonicalWithByteLength(tagText, kGcmTagSize)) {
        return Error(ErrorCode::wire_invalid_base64url, "health tag must decode to 16 bytes");
    }

    std::vector<std::uint8_t> iv;
    std::vector<std::uint8_t> tag;
    if (!Base64Url::decode(ivText, iv) || !Base64Url::decode(tagText, tag)) {
        return Error(ErrorCode::wire_invalid_base64url, "health iv or tag is not canonical");
    }

    HealthSyntax syntax;
    syntax.fields = {std::string(serverVersion), std::string(status), std::string(description), std::string(timestamp), *timestampMs};
    syntax.requestId = requestId;
    syntax.ivText = ivText;
    syntax.iv = std::move(iv);
    syntax.tag = std::move(tag);
    return syntax;
}

Result<HealthResponse> ResponseParser::verifyHealth(CryptoProvider& crypto, KeyDerivation& keys,
    std::string_view serverSecret, const HealthSyntax& syntax) {
    SecureBuffer key;
    PHPVPN_TRY_ASSIGN(key, keys.healthKey(serverSecret, syntax.requestId));
    const auto& fields = syntax.fields;
    const auto aad = healthAad(syntax.requestId, fields.serverVersion, fields.status,
                              fields.description, fields.timestamp, syntax.ivText);
    std::vector<std::uint8_t> ciphertext;
    GcmTag computed{};
    PHPVPN_TRY(crypto.aesGcmSeal(key.span(), syntax.iv, asBytes(aad), {}, ciphertext, computed));
    if (!constantTimeEquals(computed, syntax.tag)) return Error(ErrorCode::crypto_tag_mismatch, "health tag did not verify");
    return syntax.fields;
}

Result<HealthResponse> ResponseParser::parseHealth(CryptoProvider& crypto, KeyDerivation& keys,
    std::string_view serverSecret, std::string_view expectedRequestId, std::string_view body,
    const std::function<void()>& onSyntaxUsable) {
    HealthSyntax syntax;
    PHPVPN_TRY_ASSIGN(syntax, parseHealthSyntax(expectedRequestId, body));
    if (onSyntaxUsable) onSyntaxUsable();
    return verifyHealth(crypto, keys, serverSecret, syntax);
}

Result<InfoResponse> ResponseParser::parseInfo(CryptoProvider& crypto,
                                               KeyDerivation& keys,
                                               std::string_view serverSecret,
                                               std::string_view expectedRequestId,
                                               std::string_view body, std::optional<Ipv4Address> extendedSrc) {
    FlatObjectFields fields;
    PHPVPN_TRY_ASSIGN(fields, JsonScanner::parseFlatStringObject(body, kInfoKeys));

    if (fields[0] != "info") {
        return Error(ErrorCode::response_invalid, "unexpected info response type");
    }
    const std::string_view requestId = fields[1];
    const std::string_view saltText = fields[2];
    const std::string_view ivText = fields[3];
    const std::string_view ciphertextText = fields[4];
    const std::string_view tagText = fields[5];

    if (!Uuid::isValidV4(requestId)) {
        return Error(ErrorCode::response_invalid, "info requestId is not a UUID version 4");
    }
    if (requestId != expectedRequestId) {
        return Error(ErrorCode::response_request_id_mismatch,
                     "info response belongs to another request");
    }
    if (!Base64Url::isCanonicalWithByteLength(saltText, 32)) {
        return Error(ErrorCode::wire_invalid_base64url, "info salt must decode to 32 bytes");
    }
    if (!Base64Url::isCanonicalWithByteLength(ivText, kGcmIvSize)) {
        return Error(ErrorCode::wire_invalid_base64url, "info iv must decode to 12 bytes");
    }
    if (!Base64Url::isCanonicalWithByteLength(tagText, kGcmTagSize)) {
        return Error(ErrorCode::wire_invalid_base64url, "info tag must decode to 16 bytes");
    }
    // Bound allocation and GCM work before decoding an untrusted response.
    if (ciphertextText.size() > (262144u * 4u + 2u) / 3u) {
        return Error(ErrorCode::response_invalid, "info ciphertext exceeds the plaintext limit");
    }
    if (ciphertextText.empty() || !Base64Url::isCanonical(ciphertextText)) {
        return Error(ErrorCode::wire_invalid_base64url, "info ciphertext is not canonical");
    }

    std::vector<std::uint8_t> salt;
    std::vector<std::uint8_t> iv;
    std::vector<std::uint8_t> ciphertext;
    std::vector<std::uint8_t> tag;
    if (!Base64Url::decode(saltText, salt) || !Base64Url::decode(ivText, iv) ||
        !Base64Url::decode(ciphertextText, ciphertext) || !Base64Url::decode(tagText, tag)) {
        return Error(ErrorCode::wire_invalid_base64url, "an info field is not canonical");
    }

    SecureBuffer infoKey;
    PHPVPN_TRY_ASSIGN(infoKey, keys.infoKey(serverSecret, salt, requestId));

    const std::string aad = infoAad(requestId, saltText, ivText);

    SecureBuffer plaintext;
    PHPVPN_TRY_ASSIGN(plaintext,
                      crypto.aesGcmOpen(infoKey.span(), iv, asBytes(aad), ciphertext, tag));

    return parseInfoPlaintext(toString(plaintext.span()), extendedSrc);
}

Result<InfoResponse> ResponseParser::parseInfoPlaintext(std::string_view plaintextText, std::optional<Ipv4Address> extendedSrc) {
    if (plaintextText.size() > 262144) return Error(ErrorCode::response_invalid);
    JsonValue document;
    PHPVPN_TRY_ASSIGN(document, Json::parse(plaintextText));
    if (!document.isObject()) {
        return Error(ErrorCode::response_invalid, "the info plaintext is not a JSON object");
    }

    InfoResponse response;
    response.plaintext = plaintextText;

    const JsonValue* serverVersion = document.find("serverVersion");
    if (serverVersion == nullptr || !serverVersion->isString()) {
        return Error(ErrorCode::response_invalid, "the info plaintext has no serverVersion");
    }
    response.serverVersion = serverVersion->stringValue();

    const JsonValue* generatedAt = document.find("generatedAt");
    if (generatedAt == nullptr || !generatedAt->isString() ||
        !Iso8601::isValid(generatedAt->stringValue())) {
        return Error(ErrorCode::response_invalid, "the info plaintext has no valid generatedAt");
    }
    response.generatedAt = generatedAt->stringValue();

    const JsonValue* limits = document.find("limits");
    if (limits == nullptr || !limits->isObject()) {
        return Error(ErrorCode::response_invalid, "the info plaintext has no limits object");
    }
    PHPVPN_TRY_ASSIGN(response.limits.maxDataRecordsPerRequest,
                      requireInteger(*limits, "maxDataRecordsPerRequest"));
    PHPVPN_TRY_ASSIGN(response.limits.maxRequestBodyBytes,
                      requireInteger(*limits, "maxRequestBodyBytes"));
    PHPVPN_TRY_ASSIGN(response.limits.maxQueueBytesPerClient,
                      requireInteger(*limits, "maxQueueBytesPerClient"));
    PHPVPN_TRY_ASSIGN(response.limits.queueTtlSeconds, requireInteger(*limits, "queueTtlSeconds"));
    PHPVPN_TRY_ASSIGN(response.limits.minDataPollIntervalMs,
                      requireInteger(*limits, "minDataPollIntervalMs"));
    PHPVPN_TRY_ASSIGN(response.limits.maxDataPollIntervalMs,
                      requireInteger(*limits, "maxDataPollIntervalMs"));

    if (response.limits.maxDataRecordsPerRequest <= 0 || response.limits.maxRequestBodyBytes <= 0) {
        return Error(ErrorCode::response_invalid, "the info limits are not usable");
    }

    const auto* clients = document.find("clients");
    if (!clients || !clients->isArray()) return Error(ErrorCode::response_invalid, "info clients must be an array");
    if (extendedSrc && (clients->arrayValue().empty() || clients->arrayValue().size() > 253 || !validInfoAddress(*extendedSrc))) return Error(ErrorCode::response_invalid, "unverifiable info extension");
    std::set<std::uint32_t> seen;
    for (const auto& client : clients->arrayValue()) {
        if (!client.isObject()) return Error(ErrorCode::response_invalid, "info client must be an object");
        const auto* address = client.find("address");
        const auto* lastSeen = client.find("lastSeen");
        if (!address || !address->isString() || !lastSeen || !lastSeen->isString())
            return Error(ErrorCode::response_invalid, "info client has invalid fields");
        const auto ip = Ipv4Address::parse(address->stringValue());
        const auto timestamp = Iso8601::parse(lastSeen->stringValue());
        if (!ip || !timestamp || !seen.insert(ip->value()).second)
            return Error(ErrorCode::response_invalid, "info client address or timestamp is invalid or duplicated");
        SeenClient seenClient{*ip, *timestamp, {}};
        if (extendedSrc) {
            if (!validInfoAddress(*ip) || (ip->value() >> 8) != (extendedSrc->value() >> 8)) return Error(ErrorCode::response_invalid);
            const auto* contracts = client.find("maskedCommunicationContracts");
            if (!contracts) return Error(ErrorCode::response_invalid, "missing info extension");
            PHPVPN_TRY_ASSIGN(seenClient.maskedCommunicationContracts, parseMaskedContracts(*contracts, *ip));
            if (seenClient.maskedCommunicationContracts.size() > (*ip == *extendedSrc ? 0u : 1u)) return Error(ErrorCode::response_invalid);
            for (const auto& c : seenClient.maskedCommunicationContracts) if (c.peer != *extendedSrc) return Error(ErrorCode::response_invalid, "info privacy filter mismatch");
        }
        response.clients.push_back(std::move(seenClient));
    }
    return response;
}

ErrorCode ResponseParser::mapServerError(int statusCode, std::string_view body) {
    std::string_view code;
    auto fields = JsonScanner::parseFlatStringObject(body, kErrorKeys);
    if (fields.ok() && fields.value()[0] == "error") {
        code = fields.value()[1];
    }

    if (statusCode == 503) {
        // All three 503 codes share the same client reaction: back off.
        return ErrorCode::server_unavailable;
    }

    switch (statusCode) {
        case 400:
            return ErrorCode::server_bad_request;
        case 401:
            return ErrorCode::server_unauthorized;
        case 404:
            return ErrorCode::server_not_found;
        case 405:
            return ErrorCode::server_method_not_allowed;
        case 413:
            return ErrorCode::server_payload_too_large;
        case 415:
            return ErrorCode::server_unsupported_media_type;
        case 500:
            return ErrorCode::server_internal_error;
        default:
            break;
    }

    if (code == "unauthorized") {
        return ErrorCode::server_unauthorized;
    }
    return ErrorCode::server_unexpected_status;
}

}  // namespace phpvpn
