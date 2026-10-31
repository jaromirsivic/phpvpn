// JNI implementation of the two VPN hosts (plan-android-v001 point 18).
//
// NetworkHost (process-wide, implemented over the application context by
// phpvpn.android.bridge.AndroidNetworkHost) and TunnelHost (implemented by the
// live VpnService) are Kotlin interfaces; this file wraps them as the pure C++
// AndroidNetworkHost and AndroidTunnelHost of platform/android/vpn_context.h.
//
// Kotlin registers them through phpvpn.android.bridge.VpnHosts:
// registerNetworkHost(host) once per process (WP04, before nativeStart),
// registerTunnelHost(service) in VpnService.onCreate and unregisterTunnelHost()
// in onDestroy (WP05). A registration reaches the installed context at once
// and is remembered for a context created later (attachRegisteredVpnHosts).
//
// Threads: the hosts are called from the engine network thread and from the
// teardown, which are native threads. The first call on such a thread attaches
// it to the VM; it is detached when the thread ends (thread_local guard), never
// after each call, so a host call costs no attach. Every call runs inside its
// own local reference frame and clears a pending Java exception, which becomes
// an error code; nothing of the exception text reaches the log.
#pragma once

#include <jni.h>

#include <memory>

#include "platform/android/vpn_context.h"

namespace phpvpn {

// Caches the JavaVM, the two interface classes and their method IDs. Must run
// on a thread that sees the application class loader (JNI_OnLoad). False with
// a pending NoClassDefFoundError or NoSuchMethodError.
bool initializeVpnHostBridge(JNIEnv* env);

// initializeVpnHostBridge plus RegisterNatives for VpnHosts. The one call
// jni_entry.cpp makes from JNI_OnLoad.
bool registerVpnHostNatives(JNIEnv* env);

// The C++ host over a Kotlin object (a global reference held until the host
// is destroyed). Null for a null object or before initializeVpnHostBridge.
std::shared_ptr<AndroidNetworkHost> makeJniNetworkHost(JNIEnv* env, jobject host);
std::shared_ptr<AndroidTunnelHost> makeJniTunnelHost(JNIEnv* env, jobject host);

// WP04, nativeStart: gives a new context the hosts registered so far, before
// AndroidVpnContext::install(context) and ServiceRuntime::start.
void attachRegisteredVpnHosts(AndroidVpnContext& context);

// Values of TunnelHost.lastEstablishError() (TunnelHost.kt).
inline constexpr jint kEstablishOk = 0;
inline constexpr jint kEstablishNotPrepared = 1;
inline constexpr jint kEstablishRejected = 2;
inline constexpr jint kEstablishFailed = 3;

}  // namespace phpvpn
