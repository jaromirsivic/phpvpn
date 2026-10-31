package phpvpn.android.bridge

/**
 * JNI boundary of libphpvpn-android.so (plan-android-v001, WP00 and WP04).
 *
 * Every native method is registered by the library in `JNI_OnLoad`. Strings
 * cross the boundary as UTF-8 byte arrays, never as `String`, because JNI
 * strings use modified UTF-8 and would corrupt configuration text.
 */
object NativeBridge {
    init {
        System.loadLibrary("phpvpn-android")
    }

    /**
     * Version of the native client, `PHPVPN_CLIENT_VERSION` from
     * client/CMakeLists.txt, as UTF-8 bytes.
     */
    @JvmStatic
    external fun nativeVersion(): ByteArray

    fun version(): String = nativeVersion().toString(Charsets.UTF_8)
}
