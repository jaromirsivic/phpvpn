#pragma once
#include <string>
#include <string_view>
#include "core/base/result.h"

namespace phpvpn {
// Only these curated validation descriptions may cross IPC. Internal Error
// details are otherwise never exposed by the error serializers.
bool isHttpHeadersErrorDescription(std::string_view description);
std::string httpHeadersErrorDescription(const Error& error);
Result<std::string> normalizeHttpHeaders(std::string_view input);
// normalizedHeaders must have passed normalizeHttpHeaders.
std::string buildApplicationRequestHeaders(std::string_view contentType, std::string_view normalizedHeaders);
}
