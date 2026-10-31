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
