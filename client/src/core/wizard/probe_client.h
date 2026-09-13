#pragma once
#include <atomic>
#include "core/config/config_document.h"
#include "core/protocol/response_parser.h"
#include "core/runtime/time_sync.h"
#include "platform/iface/clock.h"
#include "platform/iface/transport.h"

namespace phpvpn {
// One logical attempt per call, without retries. This object has no reference
// to VPN queues, counters, tunnel, checkpoints or a runtime NetworkWorker.
class ProbeClient {
public:
    ProbeClient(CryptoProvider& crypto, Transport& transport, Clock& clock);
    Status checkUrl(const WizardContext& context, const WizardFields& fields);
    Result<InfoResponse> checkSecret(const WizardContext& context, const WizardFields& fields);
    Result<std::size_t> checkAccess(const WizardDocument& document, const WizardFields& fields);
    void cancel();
    void captureWizardHttpDiagnostics(bool enabled) { captureDiagnostics_ = enabled; }
    std::optional<HttpDiagnostic> takeFailedHttpDiagnostic() { return std::exchange(failedDiagnostic_, {}); }
    std::optional<int> lastHttpStatus() const { return lastHttpStatus_; } // Read after work completes.
private:
    Status prepare(const WizardContext& context, const WizardFields& fields);
    Result<HealthSyntax> health(bool verify);
    Result<InfoResponse> info();
    Result<HttpResponse> post(HttpAction action, std::string_view body);
    CryptoProvider& crypto_;
    Transport& transport_;
    Clock& clock_;
    KeyDerivation keys_;
    TimeSync time_;
    std::string secret_;
    std::int64_t bodyLimit_ = 0;
    std::atomic<bool> cancelled_{false};
    std::optional<int> lastHttpStatus_;
    bool captureDiagnostics_ = false;
    std::optional<HttpDiagnostic> candidate_, failedDiagnostic_;
    void clearExchange() { lastHttpStatus_.reset(); candidate_.reset(); failedDiagnostic_.reset(); }
    Error diagnosticFailure(Error error) { failedDiagnostic_ = std::move(candidate_); candidate_.reset(); return error; }
};
} // namespace phpvpn
