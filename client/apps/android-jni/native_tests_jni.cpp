// JNI entry of libphpvpn-android-tests.so (plan-android-v001 T-N03), built only
// with PHPVPN_ANDROID_NATIVE_TESTS for the debug variant of the bridge module.
//
// The configuration files must be tested where the application keeps them:
// inside the sandbox (application uid, SELinux untrusted_app, file based
// encryption of noBackupFilesDir). The adb shell user has other SELinux
// contexts, so the instrumented test calls the native test harness through
// this library instead. The production libphpvpn-android.so never contains
// test code.
#include <jni.h>
#include <stdlib.h>

#include <exception>
#include <iostream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>

#include "harness.h"

namespace {

std::string utf8(JNIEnv* env, jbyteArray bytes) {
    if (bytes == nullptr) return {};
    const jsize size = env->GetArrayLength(bytes);
    std::string text(static_cast<std::size_t>(size), '\0');
    if (size > 0) env->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(text.data()));
    return text;
}

jbyteArray bytes(JNIEnv* env, const std::string& text) {
    const auto size = static_cast<jsize>(text.size());
    jbyteArray result = env->NewByteArray(size);
    if (result == nullptr) return nullptr;  // OutOfMemoryError is pending in the VM
    env->SetByteArrayRegion(result, 0, size, reinterpret_cast<const jbyte*>(text.data()));
    return result;
}

// Runs every registered case whose "suite.name" contains `filter` with TMPDIR
// set to `directory`, and returns the harness output followed by one line
// "exit <status>" as UTF-8. TMPDIR and std::cout are process wide, so one run
// at a time.
jbyteArray nativeRun(JNIEnv* env, jclass, jbyteArray directory, jbyteArray filter) {
    static std::mutex running;
    std::lock_guard<std::mutex> lock(running);
    const std::string temporary = utf8(env, directory);
    const std::string selected = utf8(env, filter);
    std::ostringstream output;
    int status = 2;
    if (temporary.empty() || ::setenv("TMPDIR", temporary.c_str(), 1) != 0) {
        output << "the temporary directory could not be set\n";
    } else {
        std::streambuf* previous = std::cout.rdbuf(output.rdbuf());
        try {
            status = phpvpn::test::Registry::instance().run(selected);
        } catch (const std::exception& error) {
            output << "unexpected exception: " << error.what() << "\n";
        } catch (...) {
            output << "unexpected unknown exception\n";
        }
        std::cout.flush();
        std::cout.rdbuf(previous);
    }
    output << "exit " << status << "\n";
    return bytes(env, output.str());
}

const JNINativeMethod kTestMethods[] = {
    {const_cast<char*>("nativeRun"), const_cast<char*>("([B[B)[B"), reinterpret_cast<void*>(&nativeRun)},
};

}  // namespace

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || env == nullptr) {
        return JNI_ERR;
    }
    jclass library = env->FindClass("phpvpn/android/bridge/NativeTestLibrary");
    if (library == nullptr) {
        return JNI_ERR;  // NoClassDefFoundError is pending
    }
    const jint registered = env->RegisterNatives(library, kTestMethods, static_cast<jint>(std::size(kTestMethods)));
    env->DeleteLocalRef(library);
    return registered == JNI_OK ? JNI_VERSION_1_6 : JNI_ERR;
}
