// JNI entry of libphpvpn-android.so (plan-android-v001 steps 0 and 4).
//
// Every native method is registered from JNI_OnLoad with RegisterNatives, so
// the Kotlin side never depends on mangled export names. Strings cross the
// boundary as UTF-8 byte arrays, never as jstring: modified UTF-8 would corrupt
// configuration text. WP00 ships only nativeVersion; nativeStart, nativeStop,
// nativeDispatch and the model bridges arrive with WP04.
#include <jni.h>

#include <cstddef>
#include <iterator>

namespace {

// The version from PHPVPN_CLIENT_VERSION, the single source of truth in
// client/CMakeLists.txt (the Gradle versionName reads the same line).
jbyteArray nativeVersion(JNIEnv* env, jclass) {
    static constexpr char kVersion[] = PHPVPN_CLIENT_VERSION;
    const auto size = static_cast<jsize>(sizeof(kVersion) - 1);
    jbyteArray result = env->NewByteArray(size);
    if (result == nullptr) {
        return nullptr;  // OutOfMemoryError is pending in the VM
    }
    env->SetByteArrayRegion(result, 0, size, reinterpret_cast<const jbyte*>(kVersion));
    return result;
}

const JNINativeMethod kBridgeMethods[] = {
    {const_cast<char*>("nativeVersion"), const_cast<char*>("()[B"), reinterpret_cast<void*>(&nativeVersion)},
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
    return registered == JNI_OK ? JNI_VERSION_1_6 : JNI_ERR;
}
