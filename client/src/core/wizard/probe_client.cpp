#include "core/wizard/probe_client.h"
#include "core/base/hex.h"
#include "core/protocol/ndjson_reader.h"
#include "core/protocol/request_builder.h"
#include <algorithm>

namespace phpvpn {
ProbeClient::ProbeClient(CryptoProvider& crypto, Transport& transport, Clock& clock)
    : crypto_(crypto), transport_(transport), clock_(clock), keys_(crypto) {}
void ProbeClient::cancel() { cancelled_.store(true); transport_.cancelCurrentRequest(); }
Status ProbeClient::prepare(const WizardContext& context, const WizardFields& fields) {
    time_.reset();
    clearExchange();
    if (cancelled_.load()) return Error(ErrorCode::cancelled);
    auto url = parseServerUrl(fields.serverUrl);
    if (!url) return url.error();
    auto host = url->host;
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    });
    if (host == "modify-this-url") return Error(ErrorCode::config_invalid_url, "replace the placeholder with the complete server URL");
    if (!isStrictUtf8(fields.serverSecret)) return Error(ErrorCode::config_encoding_error);
    secret_ = normalizeServerSecret(fields.serverSecret);
    bodyLimit_ = context.runtime.maxResponseBodyBytes;
    return transport_.configure({fields.serverUrl, context.trustAnyTlsCertificate,
        context.runtime.responseTimeoutSeconds, bodyLimit_, captureDiagnostics_});
}
Result<HttpResponse> ProbeClient::post(HttpAction action, std::string_view body) {
    clearExchange();
    if (cancelled_.load()) return Error(ErrorCode::cancelled);
    auto response = transport_.post(action, body);
    if (cancelled_.load()) return Error(ErrorCode::cancelled);
    if (captureDiagnostics_ && action != HttpAction::data && (response.ok() || response.responseProcessingFailed))
        candidate_ = std::move(response.diagnostic);
    if (candidate_) lastHttpStatus_ = response.receivedHttpStatus;
    if (!response.ok()) return diagnosticFailure(response.error());
    lastHttpStatus_ = response.value().statusCode;
    if (response.value().statusCode != 200)
        return diagnosticFailure(Error(ResponseParser::mapServerError(response.value().statusCode, response.value().body)));
    if (response.value().body.size() > static_cast<std::uint64_t>(bodyLimit_))
        return diagnosticFailure(Error(ErrorCode::transport_response_too_large));
    auto type = response.value().contentType.substr(0, response.value().contentType.find(';'));
    while (!type.empty() && (type.back() == ' ' || type.back() == '\t')) type.pop_back();
    const auto first = type.find_first_not_of(" \t");
    type.erase(0, first == std::string::npos ? type.size() : first);
    std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    });
    const std::string_view expected = httpActionContentType(action);
    if (type != expected.substr(0, expected.find(';'))) return diagnosticFailure(Error(ErrorCode::response_invalid, "unexpected response content type"));
    return std::move(response).value();
}
Result<HealthSyntax> ProbeClient::health(bool verify) {
    clearExchange();
    std::string id;
    PHPVPN_TRY_ASSIGN(id, crypto_.randomUuidV4());
    const auto sent = clock_.monotonicMs();
    auto response = post(HttpAction::health, RequestBuilder::buildAuthNone(id));
    const auto received = clock_.monotonicMs();
    const auto wall = clock_.systemUtcMs();
    if (!response) return response.error();
    auto syntax = ResponseParser::parseHealthSyntax(id, response->body);
    if (!syntax) return diagnosticFailure(syntax.error());
    if (verify) {
        auto health = ResponseParser::verifyHealth(crypto_, keys_, secret_, *syntax);
        if (!health) return diagnosticFailure(health.error());
        auto sample = TimeSync::makeSnapshot(health->timestampMs, sent, received, wall);
        if (!sample) return diagnosticFailure(sample.error());
        time_.publish(*sample);
    }
    candidate_.reset();
    return syntax;
}
Result<InfoResponse> ProbeClient::info() {
    clearExchange();
    std::string id, timestamp, body;
    PHPVPN_TRY_ASSIGN(id, crypto_.randomUuidV4());
    PHPVPN_TRY_ASSIGN(timestamp, time_.requestTimestamp(clock_.monotonicMs()));
    SecureBuffer semi;
    PHPVPN_TRY_ASSIGN(semi, keys_.semiKey(secret_));
    PHPVPN_TRY_ASSIGN(body, RequestBuilder::buildAuthSemi(crypto_, semi, timestamp, id));
    auto response = post(HttpAction::info, body);
    if (!response) return response.error();
    auto result = ResponseParser::parseInfo(crypto_, keys_, secret_, id, response->body);
    if (!result) return diagnosticFailure(result.error());
    candidate_.reset();
    return result;
}
Status ProbeClient::checkUrl(const WizardContext& context, const WizardFields& fields) {
    PHPVPN_TRY(prepare(context, fields));
    PHPVPN_TRY(health(false));
    return ok();
}
Result<InfoResponse> ProbeClient::checkSecret(const WizardContext& context, const WizardFields& fields) {
    PHPVPN_TRY(prepare(context, fields));
    PHPVPN_TRY(health(true));
    return info();
}
Result<std::size_t> ProbeClient::checkAccess(const WizardDocument& document, const WizardFields& fields) {
    captureDiagnostics_ = false;
    clearExchange();
    // Reject local identity/contract conflicts before a request can consume data.
    PHPVPN_TRY(validateWizardIdentity(document, fields));
    PHPVPN_TRY(prepare(document.context, fields));
    PHPVPN_TRY(health(true));
    auto verifiedInfo = info();
    if (!verifiedInfo) return verifiedInfo.error();
    std::vector<std::uint8_t> accessKey;
    if (!decodeLowercaseHex(fields.accessKey, accessKey)) return Error(ErrorCode::config_invalid_key);
    SecureBuffer full;
    PHPVPN_TRY_ASSIGN(full, keys_.fullAuthKey(secret_, accessKey, fields.virtualIp));
    std::fill(accessKey.begin(), accessKey.end(), std::uint8_t{0});
    std::string id, timestamp, body;
    PHPVPN_TRY_ASSIGN(id, crypto_.randomUuidV4());
    PHPVPN_TRY_ASSIGN(timestamp, time_.requestTimestamp(clock_.monotonicMs()));
    PHPVPN_TRY_ASSIGN(body, RequestBuilder::buildAuthFull(crypto_, full, fields.virtualIp, timestamp, id, {}));
    body += '\n';
    if (body.size() > static_cast<std::uint64_t>(verifiedInfo->limits.maxRequestBodyBytes))
        return Error(ErrorCode::wire_body_too_large);
    auto response = post(HttpAction::data, body);
    if (!response) return response.error();
    const NdjsonLimits limits{document.context.runtime.maxNdjsonLineBytes,
        document.context.runtime.maxDataRecordsPerResponse};
    auto parsed = NdjsonResponseReader::parseAndVerify(crypto_, full, id, response->body, limits);
    if (!parsed) return parsed.error();
    if (!parsed->acceptedMessageIds.empty() || !parsed->deferredMessageIds.empty() ||
        parsed->acceptedRecords != 0 || parsed->acceptedBytes != 0)
        return Error(ErrorCode::response_invalid, "an empty request must have an empty acceptance result");
    for (const auto& record : parsed->records) {
        const auto src = Ipv4Address::parse(record.src);
        const auto dst = Ipv4Address::parse(record.dst);
        if (!src || !dst || !document.context.vpnCidr.isValidClientAddress(*src) ||
            !document.context.vpnCidr.isValidClientAddress(*dst) || record.dst != fields.virtualIp || src == dst)
            return Error(ErrorCode::response_invalid, "a returned data record has invalid routing fields");
    }
    // The MAC covers the authenticated record structure, not payload plaintext.
    // Destruction discards every ciphertext without attempting payload GCM.
    return parsed->records.size();
}
} // namespace phpvpn
