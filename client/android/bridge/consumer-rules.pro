# Native methods are registered from JNI_OnLoad by class and method name, so
# the bridge class and its native members must keep their names.
-keep class phpvpn.android.bridge.NativeBridge { *; }
-keepclasseswithmembernames class phpvpn.android.bridge.** { native <methods>; }
# JNI_OnLoad resolves the VPN host interfaces and their methods by name and
# calls them from native threads (apps/android-jni/jni_vpn_bridge.cpp, WP03);
# a renamed method would make the library fail to load.
-keep interface phpvpn.android.bridge.NetworkHost { *; }
-keep interface phpvpn.android.bridge.TunnelHost { *; }
-keep class phpvpn.android.bridge.VpnHosts { *; }
# JNI_OnLoad registers the natives of the runtime (NativeBridge, above) and of
# the portable models (WP04, apps/android-jni/jni_models.cpp) by class and
# method name; a removed or renamed ModelNatives makes the library fail to load.
-keep class phpvpn.android.bridge.jni.ModelNatives { *; }
