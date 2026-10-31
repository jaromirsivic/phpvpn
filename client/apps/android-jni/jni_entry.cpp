// JNI entry of libphpvpn-android.so (plan-android-v001 steps 0 and 4).
//
// Every native method is registered from JNI_OnLoad with RegisterNatives, so
// the Kotlin side never depends on mangled export names and only JNI_OnLoad is
// exported. Strings cross the boundary as UTF-8 byte arrays, never as jstring:
// modified UTF-8 would corrupt configuration text.
//
// phpvpn.android.bridge.NativeBridge (runtime):
//   nativeVersion(): ByteArray
//   nativeStart(noBackupFilesDir: ByteArray, packageName: ByteArray): Int
//   nativeStop()
//   nativeDispatch(request: ByteArray, timeoutMs: Int): ByteArray?
//   nativeStatus(): ByteArray?
// phpvpn.android.bridge.VpnHosts (WP03, jni_vpn_bridge.cpp) and
// phpvpn.android.bridge.jni.ModelNatives (jni_models.cpp).
//
// Threads: nativeStart, nativeStop and nativeStatus are serialized by one
// lifecycle mutex. nativeDispatch blocks the calling thread until the reply or
// the caller's timeout and may be called from any thread except the main
// thread (JniIpcTransport runs it on a limited Dispatchers.IO dispatcher); the
// native code deliberately does not check which thread calls it.
#include <jni.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>

#include "android_host.h"
#include "core/base/logger.h"
#include "core/ipc/ipc_protocol.h"
#include "inprocess_ipc_server.h"
#include "jni_arrays.h"
#include "jni_models.h"
#include "jni_vpn_bridge.h"
#include "platform/android/log_sink_android.h"
#include "platform/android/vpn_context.h"

#if defined(PHPVPN_ANDROID_MISSING_DEPS)
#error "libphpvpn-android.so needs PHPVPN_ANDROID_DEPS (prebuilt OpenSSL and curl, client/tools/android/build-deps.sh)"
#endif

namespace {

using namespace phpvpn;

constexpr const char* kComponent = "android";

// Results of nativeStart, mirrored by NativeBridge.START_* in Kotlin.
constexpr jint kStartOk = 0;
constexpr jint kStartAlreadyStarted = 1;
constexpr jint kStartInvalidArgument = 2;
constexpr jint kStartFailed = 3;

// Correlation id of the nativeStatus snapshot (NativeBridge.STATUS_REQUEST_ID),
// a fixed UUIDv4 so the get-status parser of WP06 accepts it unchanged.
constexpr const char* kStatusRequestId = "00000000-0000-4000-8000-0000000005a7";

// Bounds of the nativeStart arguments: an absolute Linux path and a package
// name (at most 255 characters, Android's own limit).
constexpr std::size_t kMaxPathBytes = 4096;
constexpr std::size_t kMaxPackageBytes = 255;

struct Runtime {
    // Serializes nativeStart, nativeStop and nativeStatus.
    std::mutex lifecycle;
    std::unique_ptr<ServiceRuntime> service;
    // The endpoint of the running server, copied by nativeDispatch under its
    // own short lock so a dispatch never waits for a start or a stop.
    std::mutex endpointMutex;
    std::shared_ptr<InProcessIpcEndpoint> endpoint;
};

Runtime& runtime() {
    static Runtime instance;
    return instance;
}

std::shared_ptr<InProcessIpcEndpoint> currentEndpoint() {
    std::lock_guard lock(runtime().endpointMutex);
    return runtime().endpoint;
}

void publishEndpoint(std::shared_ptr<InProcessIpcEndpoint> endpoint) {
    std::lock_guard lock(runtime().endpointMutex);
    runtime().endpoint = std::move(endpoint);
}

bool validDirectory(const std::string& text) {
    if (text.empty() || text.find('\0') != std::string::npos) return false;
    const std::filesystem::path path(text);
    return path.is_absolute() && path.lexically_normal() == path && text.back() != '/';
}

bool validPackage(const std::string& text) {
    if (text.empty()) return false;
    for (const char c : text) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_';
        if (!allowed) return false;
    }
    return true;
}

// The version from PHPVPN_CLIENT_VERSION, the single source of truth in
// client/CMakeLists.txt (the Gradle versionName reads the same line).
jbyteArray nativeVersion(JNIEnv* env, jclass) {
    static constexpr char kVersion[] = PHPVPN_CLIENT_VERSION;
    return jni::newBytes(env, std::string_view(kVersion, sizeof(kVersion) - 1));
}

void stopLocked() {
    // New dispatches return null at once; a running one wakes when the
    // server drains its job.
    publishEndpoint(nullptr);
    if (runtime().service) {
        runtime().service->stop();
        runtime().service.reset();
    }
    // Point 17: the context lives exactly as long as the runtime.
    AndroidVpnContext::uninstall();
}

// Creates the AndroidVpnContext, hands it the registered hosts and starts the
// shared ServiceRuntime. The startup Connect is never armed here: on Android
// autoConnectOnStartup means "after boot" and the BootReceiver (WP05) decides
// (plan life cycle step 7).
jint nativeStart(JNIEnv* env, jclass, jbyteArray directoryBytes, jbyteArray packageBytes) {
    auto directory = jni::readBytes(env, directoryBytes, kMaxPathBytes);
    auto package = jni::readBytes(env, packageBytes, kMaxPackageBytes);
    if (!directory || !package || !validDirectory(*directory) || !validPackage(*package)) return kStartInvalidArgument;

    std::lock_guard lock(runtime().lifecycle);
    if (runtime().service) return kStartAlreadyStarted;

    // logcat from the first record on, also when the storage is unusable.
    installLogcatSink();

    AndroidVpnContext::Options options;
    options.packageName = *package;
    auto context = std::make_shared<AndroidVpnContext>(std::move(options));
    attachRegisteredVpnHosts(*context);
    AndroidVpnContext::install(context);

    auto endpoint = std::make_shared<std::shared_ptr<InProcessIpcEndpoint>>();
    runtime().service = std::make_unique<ServiceRuntime>();
    const Status started = runtime().service->start(androidPlatformHost(std::filesystem::path(*directory), endpoint));
    if (!started.ok()) {
        PHPVPN_LOG_ERROR(kComponent, std::string("the native runtime did not start: ") + started.error().name());
        stopLocked();
        return kStartFailed;
    }
    publishEndpoint(*endpoint);
    PHPVPN_LOG_INFO(kComponent, "the native runtime is running; the startup Connect is left to the boot receiver");
    return kStartOk;
}

void nativeStop(JNIEnv*, jclass) {
    std::lock_guard lock(runtime().lifecycle);
    stopLocked();
}

// One IPC v1 exchange. The size is checked before anything is copied; null on
// the caller's timeout, when the runtime is not running and for a request the
// desktop would answer by closing the connection.
jbyteArray nativeDispatch(JNIEnv* env, jclass, jbyteArray request, jint timeoutMs) {
    if (request == nullptr) return nullptr;
    auto endpoint = currentEndpoint();
    const jsize length = env->GetArrayLength(request);
    if (length <= 0) return nullptr;
    if (static_cast<std::size_t>(length) > kIpcMaxMessageBytes) {
        if (endpoint) endpoint->countRejected();
        return nullptr;
    }
    if (!endpoint) return nullptr;
    auto text = jni::readBytes(env, request, kIpcMaxMessageBytes);
    if (!text) return nullptr;
    auto job = endpoint->submit(std::move(*text));
    if (!job) return nullptr;
    if (!endpoint->wait(job, std::chrono::milliseconds(timeoutMs))) return nullptr;
    // The job wipes its request and response when the last owner drops it.
    return jni::newBytes(env, job->response);
}

// The engine status as the get-status reply, correlated with
// NativeBridge.STATUS_REQUEST_ID, for the service notification (WP05).
jbyteArray nativeStatus(JNIEnv* env, jclass) {
    std::string reply;
    {
        std::lock_guard lock(runtime().lifecycle);
        if (!runtime().service) return nullptr;
        const auto snapshot = runtime().service->status();
        if (!snapshot) return nullptr;
        reply = IpcProtocol::buildStatusResponse(kStatusRequestId, *snapshot);
    }
    return jni::newBytes(env, reply);
}

const JNINativeMethod kBridgeMethods[] = {
    {const_cast<char*>("nativeVersion"), const_cast<char*>("()[B"), reinterpret_cast<void*>(&nativeVersion)},
    {const_cast<char*>("nativeStart"), const_cast<char*>("([B[B)I"), reinterpret_cast<void*>(&nativeStart)},
    {const_cast<char*>("nativeStop"), const_cast<char*>("()V"), reinterpret_cast<void*>(&nativeStop)},
    {const_cast<char*>("nativeDispatch"), const_cast<char*>("([BI)[B"), reinterpret_cast<void*>(&nativeDispatch)},
    {const_cast<char*>("nativeStatus"), const_cast<char*>("()[B"), reinterpret_cast<void*>(&nativeStatus)},
};

}  // namespace

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || env == nullptr) {
        return JNI_ERR;
    }
    jclass bridge = env->FindClass("phpvpn/android/bridge/NativeBridge");
    if (bridge == nullptr) {
        return JNI_ERR;  // NoClassDefFoundError is pending
    }
    const jint registered = env->RegisterNatives(bridge, kBridgeMethods, static_cast<jint>(std::size(kBridgeMethods)));
    env->DeleteLocalRef(bridge);
    return registered == JNI_OK && phpvpn::registerVpnHostNatives(env) && phpvpn::registerModelNatives(env)
        ? JNI_VERSION_1_6
        : JNI_ERR;
}
