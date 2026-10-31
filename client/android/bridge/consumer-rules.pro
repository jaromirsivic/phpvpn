# Native methods are registered from JNI_OnLoad by class and method name, so
# the bridge class and its native members must keep their names.
-keep class phpvpn.android.bridge.NativeBridge { *; }
-keepclasseswithmembernames class phpvpn.android.bridge.** { native <methods>; }
