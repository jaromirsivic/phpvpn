// HTTP boundary of the core.
//
// The transport owns the response size limit and the total request deadline for
// all three actions, protocol.md chapter 11.2 and client.md "Prijem dat". It
// streams the body into a sink so that a `data` response never has to be
// buffered twice.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "core/base/result.h"

namespace phpvpn {

enum class HttpAction { health, info, data };

const char* httpActionName(HttpAction action) noexcept;
const char* httpActionContentType(HttpAction action) noexcept;

struct TransportSettings {
    std::string serverUrl;
    bool trustAnyTlsCertificate = true;
    std::int64_t responseTimeoutSeconds = 30;
    std::int64_t maxResponseBodyBytes = 67108864;
};

struct HttpResponse {
    int statusCode = 0;
    std::string body;
};

// Cancellation token shared with the role that owns the request. Closing it
// aborts DNS resolution, the TCP connect, the TLS handshake and a slow read.
class RequestCancellation {
public:
    virtual ~RequestCancellation() = default;
    virtual void cancel() = 0;
};

class Transport {
public:
    virtual ~Transport() = default;

    virtual Status configure(const TransportSettings& settings) = 0;

    // Performs exactly one POST to `?action=<action>` and returns the complete
    // body. Never follows a redirect and never uses a proxy.
    virtual Result<HttpResponse> post(HttpAction action, std::string_view body) = 0;

    // Aborts the request that is currently in flight, if any.
    virtual void cancelCurrentRequest() = 0;
};

Result<std::unique_ptr<Transport>> createPlatformTransport();

}  // namespace phpvpn
