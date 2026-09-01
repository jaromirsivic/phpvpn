#include "core/crypto/canonical.h"

#include "core/base/hex.h"

namespace phpvpn {
namespace {

void appendLine(std::string& out, std::string_view value) {
    out.append(value);
    out.push_back('\n');
}

}  // namespace

std::string sha256Hex(std::span<const std::uint8_t> digest) { return toLowercaseHex(digest); }

std::string authSemiCanonical(std::string_view timestamp, std::string_view requestId) {
    std::string out;
    out.reserve(64 + timestamp.size() + requestId.size());
    appendLine(out, "PHPVPN-AUTH-SEMI");
    appendLine(out, "POST");
    appendLine(out, "info");
    appendLine(out, timestamp);
    out.append(requestId);
    return out;
}

std::string authFullCanonical(std::string_view src,
                              std::string_view timestamp,
                              std::string_view requestId,
                              std::string_view bodySha256Hex) {
    std::string out;
    out.reserve(96 + src.size() + timestamp.size() + requestId.size() + bodySha256Hex.size());
    appendLine(out, "PHPVPN-AUTH-FULL");
    appendLine(out, "POST");
    appendLine(out, "data");
    appendLine(out, src);
    appendLine(out, timestamp);
    appendLine(out, requestId);
    out.append(bodySha256Hex);
    return out;
}

std::string healthAad(std::string_view requestId,
                      std::string_view serverVersion,
                      std::string_view status,
                      std::string_view description,
                      std::string_view timestamp,
                      std::string_view ivBase64Url) {
    std::string out;
    out.reserve(96 + requestId.size() + serverVersion.size() + status.size() +
                description.size() + timestamp.size() + ivBase64Url.size());
    appendLine(out, "PHPVPN-HEALTH-RESPONSE");
    appendLine(out, "health");
    appendLine(out, requestId);
    appendLine(out, serverVersion);
    appendLine(out, status);
    appendLine(out, description);
    appendLine(out, timestamp);
    out.append(ivBase64Url);
    return out;
}

std::string infoAad(std::string_view requestId,
                    std::string_view saltBase64Url,
                    std::string_view ivBase64Url) {
    std::string out;
    out.reserve(48 + requestId.size() + saltBase64Url.size() + ivBase64Url.size());
    appendLine(out, "PHPVPN-INFO");
    appendLine(out, "info");
    appendLine(out, requestId);
    appendLine(out, saltBase64Url);
    out.append(ivBase64Url);
    return out;
}

std::string dataAad(std::string_view messageId,
                    std::string_view src,
                    std::string_view dst,
                    std::string_view ivBase64Url) {
    std::string out;
    out.reserve(48 + messageId.size() + src.size() + dst.size() + ivBase64Url.size());
    appendLine(out, "PHPVPN-DATA");
    appendLine(out, "data");
    appendLine(out, messageId);
    appendLine(out, src);
    appendLine(out, dst);
    out.append(ivBase64Url);
    return out;
}

std::string dataMacCanonical(std::string_view requestId,
                             std::span<const std::pair<std::string, std::string>> records,
                             std::string_view dataResultSha256Hex) {
    std::string out;
    out.reserve(64 + requestId.size() + records.size() * 96 + dataResultSha256Hex.size());
    appendLine(out, "PHPVPN-DATA-RESPONSE");
    appendLine(out, "data");
    appendLine(out, requestId);
    // DECIMAL(recordCount) without a sign and without leading zeros; zero is "0".
    appendLine(out, std::to_string(records.size()));
    for (const auto& record : records) {
        appendLine(out, record.first);
        appendLine(out, record.second);
    }
    out.append(dataResultSha256Hex);
    return out;
}

}  // namespace phpvpn
