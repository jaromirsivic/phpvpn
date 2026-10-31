// JNI VPN hosts, see jni_vpn_bridge.h (plan-android-v001 point 18, WP03).
#include "jni_vpn_bridge.h"

#include <iterator>
#include <mutex>
#include <string>
#include <utility>

#include "core/base/logger.h"
#include "platform/android/netconfig_android.h"

namespace phpvpn {
namespace {

constexpr const char* kComponent = "android-jni";
constexpr const char* kNetworkHostClass = "phpvpn/android/bridge/NetworkHost";
constexpr const char* kTunnelHostClass = "phpvpn/android/bridge/TunnelHost";
constexpr const char* kVpnHostsClass = "phpvpn/android/bridge/VpnHosts";

struct Bridge {
    JavaVM* vm = nullptr;
    jclass networkHostClass = nullptr;  // global references
    jclass tunnelHostClass = nullptr;
    jmethodID snapshotNetworks = nullptr;
    jmethodID isConsentGranted = nullptr;
    jmethodID establish = nullptr;
    jmethodID lastEstablishError = nullptr;
    jmethodID waitVpnNetworkGone = nullptr;
};

std::mutex& bridgeMutex() {
    static std::mutex mutex;
    return mutex;
}

// Written once in JNI_OnLoad, read afterwards; never destroyed, a late host
// destructor on an exiting thread may still need it.
Bridge& bridge() {
    static Bridge* instance = new Bridge;
    return *instance;
}

// Detaches the native thread it belongs to when that thread ends. Only threads
// this file attached own one, so a Java thread is never detached.
class ThreadAttachment {
public:
    ~ThreadAttachment() {
        if (vm_ != nullptr) vm_->DetachCurrentThread();
    }
    void attached(JavaVM* vm) noexcept { vm_ = vm; }

private:
    JavaVM* vm_ = nullptr;
};

JNIEnv* threadEnv() {
    JavaVM* vm = bridge().vm;
    if (vm == nullptr) return nullptr;
    JNIEnv* env = nullptr;
    const jint state = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (state == JNI_OK) return env;
    if (state != JNI_EDETACHED) return nullptr;
    JavaVMAttachArgs arguments{JNI_VERSION_1_6, "phpvpn-native", nullptr};
    if (vm->AttachCurrentThread(&env, &arguments) != JNI_OK) return nullptr;
    // Runs before the pthread key destructor of ART at thread exit.
    thread_local ThreadAttachment attachment;
    attachment.attached(vm);
    return env;
}

// One local reference frame per host call: an attached native thread has no
// Java frame that would ever release them.
class LocalFrame {
public:
    explicit LocalFrame(JNIEnv* env) : env_(env), pushed_(env->PushLocalFrame(8) == JNI_OK) {}
    ~LocalFrame() {
        if (pushed_) env_->PopLocalFrame(nullptr);
    }
    bool ok() const noexcept { return pushed_; }
    LocalFrame(const LocalFrame&) = delete;
    LocalFrame& operator=(const LocalFrame&) = delete;

private:
    JNIEnv* env_;
    bool pushed_;
};

// A Java exception becomes an error code; its text never reaches the log.
bool clearedException(JNIEnv* env) {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionClear();
    return true;
}

void deleteGlobal(jobject object) {
    if (object == nullptr) return;
    if (JNIEnv* env = threadEnv()) env->DeleteGlobalRef(object);
}

class JniNetworkHost final : public AndroidNetworkHost {
public:
    explicit JniNetworkHost(jobject host) : host_(host) {}
    ~JniNetworkHost() override { deleteGlobal(host_); }
    JniNetworkHost(const JniNetworkHost&) = delete;
    JniNetworkHost& operator=(const JniNetworkHost&) = delete;

    Result<std::string> snapshotJson() override {
        JNIEnv* env = threadEnv();
        if (env == nullptr) return Error(ErrorCode::netconfig_apply_failed, "the Java VM is not reachable");
        LocalFrame frame(env);
        if (!frame.ok()) {
            env->ExceptionClear();
            return Error(ErrorCode::netconfig_apply_failed, "no JNI frame is available");
        }
        auto* array = static_cast<jbyteArray>(env->CallObjectMethod(host_, bridge().snapshotNetworks));
        if (clearedException(env) || array == nullptr) {
            PHPVPN_LOG_WARN(kComponent, "the network host failed to report the networks");
            return Error(ErrorCode::netconfig_apply_failed, "the network host failed");
        }
        const jsize size = env->GetArrayLength(array);
        if (size < 0 || static_cast<std::size_t>(size) > kMaxAndroidSnapshotBytes) {
            return Error(ErrorCode::netconfig_apply_failed, "the network snapshot is too large");
        }
        std::string json(static_cast<std::size_t>(size), '\0');
        if (size > 0) env->GetByteArrayRegion(array, 0, size, reinterpret_cast<jbyte*>(json.data()));
        if (clearedException(env)) return Error(ErrorCode::netconfig_apply_failed, "the network snapshot could not be read");
        return json;
    }

    Result<bool> isConsentGranted() override {
        JNIEnv* env = threadEnv();
        if (env == nullptr) return Error(ErrorCode::tunnel_adapter_failed, "the Java VM is not reachable");
        LocalFrame frame(env);
        if (!frame.ok()) {
            env->ExceptionClear();
            return Error(ErrorCode::tunnel_adapter_failed, "no JNI frame is available");
        }
        const jboolean granted = env->CallBooleanMethod(host_, bridge().isConsentGranted);
        if (clearedException(env)) {
            PHPVPN_LOG_WARN(kComponent, "the network host failed to report the VPN consent");
            return Error(ErrorCode::tunnel_adapter_failed, "the VPN consent could not be queried");
        }
        return granted == JNI_TRUE;
    }

private:
    jobject host_;
};

class JniTunnelHost final : public AndroidTunnelHost {
public:
    explicit JniTunnelHost(jobject host) : host_(host) {}
    ~JniTunnelHost() override { deleteGlobal(host_); }
    JniTunnelHost(const JniTunnelHost&) = delete;
    JniTunnelHost& operator=(const JniTunnelHost&) = delete;

    Result<int> establish(const VpnSpec& spec) override {
        JNIEnv* env = threadEnv();
        if (env == nullptr) return Error(ErrorCode::tunnel_adapter_failed, "the Java VM is not reachable");
        LocalFrame frame(env);
        if (!frame.ok()) {
            env->ExceptionClear();
            return Error(ErrorCode::tunnel_adapter_failed, "no JNI frame is available");
        }
        const std::string json = serializeVpnSpec(spec);
        const auto size = static_cast<jsize>(json.size());
        jbyteArray bytes = env->NewByteArray(size);
        if (bytes == nullptr) {
            env->ExceptionClear();
            return Error(ErrorCode::tunnel_adapter_failed, "the VPN specification could not be passed");
        }
        env->SetByteArrayRegion(bytes, 0, size, reinterpret_cast<const jbyte*>(json.data()));
        const jint descriptor = env->CallIntMethod(host_, bridge().establish, bytes);
        if (clearedException(env)) {
            PHPVPN_LOG_WARN(kComponent, "the VPN service failed while establishing the interface");
            return Error(ErrorCode::tunnel_adapter_failed, "the VPN service failed");
        }
        if (descriptor >= 0) return static_cast<int>(descriptor);
        const jint reason = env->CallIntMethod(host_, bridge().lastEstablishError);
        if (clearedException(env)) return Error(ErrorCode::tunnel_adapter_failed, "the VPN service failed");
        switch (reason) {
            case kEstablishNotPrepared:
                return Error(ErrorCode::tunnel_permission_missing, "the VPN consent is missing");
            case kEstablishRejected:
                return Error(ErrorCode::tunnel_adapter_failed, "the system rejected the VPN interface parameters");
            default:
                return Error(ErrorCode::tunnel_adapter_failed, "the VPN interface could not be established");
        }
    }

    Result<bool> waitVpnNetworkGone(int timeoutMs) override {
        JNIEnv* env = threadEnv();
        if (env == nullptr) return Error(ErrorCode::tunnel_adapter_failed, "the Java VM is not reachable");
        LocalFrame frame(env);
        if (!frame.ok()) {
            env->ExceptionClear();
            return Error(ErrorCode::tunnel_adapter_failed, "no JNI frame is available");
        }
        const jboolean gone = env->CallBooleanMethod(host_, bridge().waitVpnNetworkGone, static_cast<jint>(timeoutMs));
        if (clearedException(env)) return Error(ErrorCode::tunnel_adapter_failed, "the VPN service failed");
        return gone == JNI_TRUE;
    }

private:
    jobject host_;
};

// Hosts registered from Kotlin, kept for a context installed later.
struct Registry {
    std::mutex mutex;
    std::shared_ptr<AndroidNetworkHost> network;
    std::shared_ptr<AndroidTunnelHost> tunnel;
};

Registry& registry() {
    static Registry* instance = new Registry;
    return *instance;
}

// The previous host is released outside the lock: its destructor deletes a
// global reference.
void nativeRegisterNetworkHost(JNIEnv* env, jclass, jobject host) {
    auto made = makeJniNetworkHost(env, host);
    std::shared_ptr<AndroidNetworkHost> previous;
    {
        std::lock_guard<std::mutex> lock(registry().mutex);
        previous = std::exchange(registry().network, made);
    }
    if (auto context = AndroidVpnContext::current()) context->setNetworkHost(made);
}

void nativeRegisterTunnelHost(JNIEnv* env, jclass, jobject host) {
    auto made = makeJniTunnelHost(env, host);
    std::shared_ptr<AndroidTunnelHost> previous;
    {
        std::lock_guard<std::mutex> lock(registry().mutex);
        previous = std::exchange(registry().tunnel, made);
    }
    if (auto context = AndroidVpnContext::current()) context->setTunnelHost(made);
}

void nativeUnregisterTunnelHost(JNIEnv*, jclass) {
    std::shared_ptr<AndroidTunnelHost> previous;
    {
        std::lock_guard<std::mutex> lock(registry().mutex);
        previous = std::move(registry().tunnel);
        registry().tunnel.reset();
    }
    if (auto context = AndroidVpnContext::current()) context->setTunnelHost(nullptr);
}

const JNINativeMethod kVpnHostMethods[] = {
    {const_cast<char*>("registerNetworkHost"), const_cast<char*>("(Lphpvpn/android/bridge/NetworkHost;)V"),
     reinterpret_cast<void*>(&nativeRegisterNetworkHost)},
    {const_cast<char*>("registerTunnelHost"), const_cast<char*>("(Lphpvpn/android/bridge/TunnelHost;)V"),
     reinterpret_cast<void*>(&nativeRegisterTunnelHost)},
    {const_cast<char*>("unregisterTunnelHost"), const_cast<char*>("()V"),
     reinterpret_cast<void*>(&nativeUnregisterTunnelHost)},
};

jclass globalClass(JNIEnv* env, const char* name) {
    jclass local = env->FindClass(name);
    if (local == nullptr) return nullptr;  // NoClassDefFoundError is pending
    auto* global = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    return global;
}

}  // namespace

bool initializeVpnHostBridge(JNIEnv* env) {
    std::lock_guard<std::mutex> lock(bridgeMutex());
    Bridge& state = bridge();
    if (state.vm != nullptr) return true;
    JavaVM* vm = nullptr;
    if (env->GetJavaVM(&vm) != JNI_OK || vm == nullptr) return false;
    jclass network = globalClass(env, kNetworkHostClass);
    if (network == nullptr) return false;
    jclass tunnel = globalClass(env, kTunnelHostClass);
    if (tunnel == nullptr) {
        env->DeleteGlobalRef(network);
        return false;
    }
    Bridge resolved;
    resolved.networkHostClass = network;
    resolved.tunnelHostClass = tunnel;
    resolved.snapshotNetworks = env->GetMethodID(network, "snapshotNetworks", "()[B");
    resolved.isConsentGranted = resolved.snapshotNetworks ? env->GetMethodID(network, "isConsentGranted", "()Z") : nullptr;
    resolved.establish = resolved.isConsentGranted ? env->GetMethodID(tunnel, "establish", "([B)I") : nullptr;
    resolved.lastEstablishError = resolved.establish ? env->GetMethodID(tunnel, "lastEstablishError", "()I") : nullptr;
    resolved.waitVpnNetworkGone =
        resolved.lastEstablishError ? env->GetMethodID(tunnel, "waitVpnNetworkGone", "(I)Z") : nullptr;
    if (resolved.waitVpnNetworkGone == nullptr) {  // NoSuchMethodError is pending
        env->DeleteGlobalRef(network);
        env->DeleteGlobalRef(tunnel);
        return false;
    }
    resolved.vm = vm;
    state = resolved;
    return true;
}

bool registerVpnHostNatives(JNIEnv* env) {
    if (!initializeVpnHostBridge(env)) return false;
    jclass hosts = env->FindClass(kVpnHostsClass);
    if (hosts == nullptr) return false;  // NoClassDefFoundError is pending
    const jint registered =
        env->RegisterNatives(hosts, kVpnHostMethods, static_cast<jint>(std::size(kVpnHostMethods)));
    env->DeleteLocalRef(hosts);
    return registered == JNI_OK;
}

std::shared_ptr<AndroidNetworkHost> makeJniNetworkHost(JNIEnv* env, jobject host) {
    if (host == nullptr || bridge().vm == nullptr || !env->IsInstanceOf(host, bridge().networkHostClass)) {
        return nullptr;
    }
    jobject global = env->NewGlobalRef(host);
    if (global == nullptr) return nullptr;
    return std::make_shared<JniNetworkHost>(global);
}

std::shared_ptr<AndroidTunnelHost> makeJniTunnelHost(JNIEnv* env, jobject host) {
    if (host == nullptr || bridge().vm == nullptr || !env->IsInstanceOf(host, bridge().tunnelHostClass)) {
        return nullptr;
    }
    jobject global = env->NewGlobalRef(host);
    if (global == nullptr) return nullptr;
    return std::make_shared<JniTunnelHost>(global);
}

void attachRegisteredVpnHosts(AndroidVpnContext& context) {
    std::shared_ptr<AndroidNetworkHost> network;
    std::shared_ptr<AndroidTunnelHost> tunnel;
    {
        std::lock_guard<std::mutex> lock(registry().mutex);
        network = registry().network;
        tunnel = registry().tunnel;
    }
    context.setNetworkHost(std::move(network));
    context.setTunnelHost(std::move(tunnel));
}

}  // namespace phpvpn
