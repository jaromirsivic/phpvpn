#include "ipc_async.h"

#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

#include "core/base/secure_buffer.h"
#include "core/crypto/crypto_provider.h"
#include "core/protocol/json.h"
#include "platform/linux/fd.h"
#include "poll_schedule.h"

namespace phpvpn::tray {
namespace {
std::string freshRequestId() {
    auto crypto = createPlatformCryptoProvider();
    if (!crypto) return {};
    auto id = crypto.value()->randomUuidV4();
    return id ? std::move(id).value() : std::string{};
}
void signal(int fd) {
    if (fd < 0) return;
    const std::uint64_t one = 1;
    // A full counter is still readable, so a failed write cannot lose the cancel.
    [[maybe_unused]] const auto written = ::write(fd, &one, sizeof(one));
}
bool correlated(std::string_view response, std::string_view requestId) {
    const auto document = Json::parse(response);
    if (!document.ok() || !document.value().isObject()) return false;
    const auto* id = document.value().find("requestId");
    return id && id->isString() && id->stringValue() == requestId;
}
void wipeWizardFields(WizardRequest& request) {
    for (auto* value : {&request.fields.serverUrl, &request.fields.serverSecret,
                        &request.fields.virtualIp, &request.fields.accessKey})
        secureWipe(value->data(), value->size());
}
}  // namespace

std::int64_t monotonicMs() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

IpcReply exchangeOnce(const IpcEndpoint& endpoint, std::string_view request, std::int64_t timeoutMs, int cancelFd) {
    IpcReply reply;
    if (request.empty() || request.size() > kIpcMaxMessageBytes || timeoutMs <= 0) {
        reply.error = timeoutMs <= 0 ? ErrorCode::timeout : ErrorCode::invalid_argument;
        return reply;
    }
    const IpcSocketClient client(IpcSocketClientOptions{endpoint.socketPath, endpoint.expectedServerUid});
    auto response = client.exchange(request, timeoutMs, cancelFd, &reply.outcome);
    if (response) reply.response = std::move(response).value();
    else reply.error = response.error().code;
    return reply;
}

std::string commandResponse(const IpcEndpoint& endpoint, IpcCommand command, std::string_view payload,
                            std::int64_t timeoutMs, int cancelFd) {
    const auto id = freshRequestId();
    if (id.empty()) return {};
    auto encoded = IpcProtocol::buildRequest(command, id, payload);
    auto reply = exchangeOnce(endpoint, encoded, timeoutMs, cancelFd);
    secureWipe(encoded.data(), encoded.size());
    if (command != IpcCommand::get_config && command != IpcCommand::get_info &&
        !correlated(reply.response, id)) {
        secureWipe(reply.response.data(), reply.response.size());
        return {};
    }
    return std::move(reply.response);
}

// ---------------------------------------------------------------------------
struct IpcChannel::State {
    std::mutex mutex;
    std::condition_variable finished;
    bool done = false;
    bool abandoned = false;
    IpcChannel* owner = nullptr;
    UniqueFd cancel{eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)};
};

IpcChannel::IpcChannel(IpcEndpoint endpoint, Dispatcher dispatcher)
    : endpoint_(std::move(endpoint)), dispatcher_(std::move(dispatcher)) {}
IpcChannel::~IpcChannel() { stop(); }

bool IpcChannel::start(std::function<std::function<void()>(int cancelFd)> work) {
    if (state_ || !dispatcher_) return false;
    auto state = std::make_shared<State>();
    if (!state->cancel) return false;
    state->owner = this;
    try {
        std::thread([state, work = std::move(work), dispatcher = dispatcher_]() mutable {
            std::function<void()> deliver;
            try { deliver = work(state->cancel.get()); } catch (...) { deliver = {}; }
            work = {};
            bool post = false;
            {
                std::lock_guard lock(state->mutex);
                post = !state->abandoned;
            }
            if (post) {
                try {
                    dispatcher([state, deliver = std::move(deliver)]() mutable {
                        IpcChannel* owner = nullptr;
                        {
                            std::lock_guard lock(state->mutex);
                            if (state->abandoned) return;
                            owner = state->owner;
                            state->owner = nullptr;
                        }
                        // The channel is free before the callback, so it may send again.
                        if (owner && owner->state_ == state) owner->state_.reset();
                        if (deliver) deliver();
                    });
                } catch (...) {}
            }
            {
                std::lock_guard lock(state->mutex);
                state->done = true;
            }
            state->finished.notify_all();
        }).detach();
    } catch (...) {
        return false;
    }
    state_ = std::move(state);
    return true;
}

bool IpcChannel::requestRaw(std::string request, std::int64_t timeoutMs, std::function<void(IpcReply&)> done) {
    if (request.empty()) return false;
    auto endpoint = endpoint_;
    const bool started = start([endpoint, request = std::move(request), timeoutMs, done = std::move(done)](int cancel) mutable {
        auto reply = exchangeOnce(endpoint, request, timeoutMs, cancel);
        secureWipe(request.data(), request.size());
        return std::function<void()>([reply = std::move(reply), done = std::move(done)]() mutable {
            if (done) done(reply);
            secureWipe(reply.response.data(), reply.response.size());
        });
    });
    return started;
}

bool IpcChannel::request(IpcCommand command, std::string payload, std::function<void(std::string&)> done,
                         std::int64_t timeoutMs) {
    if (command == IpcCommand::set_config) return false;
    auto endpoint = endpoint_;
    return start([endpoint, command, payload = std::move(payload), timeoutMs, done = std::move(done)](int cancel) mutable {
        auto response = commandResponse(endpoint, command, payload, timeoutMs, cancel);
        secureWipe(payload.data(), payload.size());
        return std::function<void()>([response = std::move(response), done = std::move(done)]() mutable {
            if (done) done(response);
            secureWipe(response.data(), response.size());
        });
    });
}

bool IpcChannel::requestSave(const SaveOperation& operation, std::string payload,
                             std::function<void(const SaveResult&)> done) {
    if (!operation.windowGeneration || !operation.operationGeneration || !operation.deadlineMs) return false;
    auto endpoint = endpoint_;
    return start([endpoint, operation, payload = std::move(payload), done = std::move(done)](int cancel) mutable {
        SaveResult result{operation, SaveOutcome::notSent, ErrorCode::internal, {}};
        std::string id, encoded;
        IpcReply reply;
        try {
            id = freshRequestId();
            const auto now = monotonicMs();
            const auto deadline = static_cast<std::int64_t>(operation.deadlineMs);
            if (id.empty()) result.error = ErrorCode::internal;
            else if (now >= deadline) result.error = ErrorCode::timeout;
            else {
                encoded = IpcProtocol::buildRequest(IpcCommand::set_config, id, payload);
                // The worker ends by the original deadline; nothing extends it.
                reply = exchangeOnce(endpoint, encoded, std::min<std::int64_t>(kCommandTimeoutMs, deadline - now), cancel);
                result = classifySaveResponse(operation, id, reply.response, reply.outcome.requestSent,
                                              static_cast<std::uint64_t>(monotonicMs()));
                if (!reply.outcome.requestSent && reply.error == ErrorCode::timeout) result.error = ErrorCode::timeout;
            }
        } catch (...) {
            result = {operation, reply.outcome.requestSent ? SaveOutcome::unknown : SaveOutcome::notSent,
                      ErrorCode::internal, {}};
        }
        for (auto* text : {&payload, &encoded, &reply.response}) secureWipe(text->data(), text->size());
        return std::function<void()>([result = std::move(result), done = std::move(done)] {
            if (done) done(result);
        });
    });
}

bool IpcChannel::requestWizard(WizardRequest request, std::function<void(WizardResult&)> done, std::int64_t timeoutMs) {
    auto endpoint = endpoint_;
    return start([endpoint, request = std::move(request), timeoutMs, done = std::move(done)](int cancel) mutable {
        request.requestId = freshRequestId();
        Result<WizardReply> reply = Error(ErrorCode::internal);
        if (!request.requestId.empty()) {
            auto encoded = WizardIpc::buildRequest(request);
            if (encoded) {
                auto raw = exchangeOnce(endpoint, encoded.value(), timeoutMs, cancel);
                secureWipe(encoded.value().data(), encoded.value().size());
                reply = raw.response.empty() ? Result<WizardReply>(Error(ErrorCode::ipc_unavailable))
                                             : WizardIpc::parseReply(request.command, request.requestId, raw.response);
                secureWipe(raw.response.data(), raw.response.size());
            } else {
                reply = Error(encoded.error());
            }
        }
        auto result = std::make_shared<WizardResult>(WizardResult{std::move(request), std::move(reply)});
        return std::function<void()>([result, done = std::move(done)]() mutable {
            if (done) done(*result);
            wipeWizardFields(result->request);
        });
    });
}

void IpcChannel::abandon() {
    const auto state = std::move(state_);
    if (!state) return;
    std::lock_guard lock(state->mutex);
    state->abandoned = true;
    state->owner = nullptr;
    signal(state->cancel.get());
}

void IpcChannel::stop() {
    const auto state = state_;
    abandon();
    if (!state) return;
    std::unique_lock lock(state->mutex);
    state->finished.wait(lock, [&] { return state->done; });
}

// ---------------------------------------------------------------------------
struct StatusChannel::State {
    std::mutex mutex;
    std::condition_variable finished;
    bool done = true;
    bool abandoned = false;
    StatusChannel* owner = nullptr;
    ReadRequestQueue queue;
    UniqueFd cancel{eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)};
};

StatusChannel::StatusChannel(IpcEndpoint endpoint, Dispatcher dispatcher, std::function<void(const TrayStatus&)> onStatus)
    : endpoint_(std::move(endpoint)), dispatcher_(std::move(dispatcher)), onStatus_(std::move(onStatus)) {}
StatusChannel::~StatusChannel() { stop(); }

void StatusChannel::request() {
    if (!dispatcher_) return;
    if (!state_) {
        state_ = std::make_shared<State>();
        state_->owner = this;
    }
    const auto state = state_;
    std::lock_guard lock(state->mutex);
    if (state->abandoned || !state->cancel || !state->queue.request()) return;
    state->done = false;
    try {
        std::thread([state, endpoint = endpoint_, dispatcher = dispatcher_] {
            for (;;) {
                TrayStatus snapshot;
                try {
                    snapshot = parseStatusResponse(commandResponse(endpoint, IpcCommand::get_status, {},
                                                                   kStatusRequestTimeoutMs, state->cancel.get()));
                } catch (...) { snapshot = {}; }
                {
                    std::lock_guard guard(state->mutex);
                    if (!state->abandoned) {
                        try {
                            dispatcher([state, snapshot = std::move(snapshot)] {
                                StatusChannel* owner = nullptr;
                                {
                                    std::lock_guard inner(state->mutex);
                                    if (state->abandoned) return;
                                    owner = state->owner;
                                }
                                if (owner && owner->onStatus_) owner->onStatus_(snapshot);
                            });
                        } catch (...) { state->queue.stop(); state->done = true; break; }
                    }
                    if (state->abandoned || !state->queue.finish()) { state->done = true; break; }
                }
            }
            state->finished.notify_all();
        }).detach();
    } catch (...) { state->done = true; state->queue.stop(); }
}

void StatusChannel::abandon() {
    const auto state = std::move(state_);
    if (!state) return;
    std::lock_guard lock(state->mutex);
    state->abandoned = true;
    state->owner = nullptr;
    state->queue.stop();
    signal(state->cancel.get());
}

void StatusChannel::stop() {
    const auto state = state_;
    abandon();
    if (!state) return;
    std::unique_lock lock(state->mutex);
    state->finished.wait(lock, [&] { return state->done; });
}
}  // namespace phpvpn::tray
