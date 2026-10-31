package phpvpn.android.bridge.jni

import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.ConfigDocumentCheck

/**
 * The full configuration validator behind the document checks of the Wizard
 * family replies (`documentMatches` and the `get-default-config` template
 * check of core/ipc/wizard_ipc.cpp). Passed to `IpcClient`, it replaces
 * WP06's `StructuralConfigDocumentCheck`, so a declared availability is
 * accepted only when the native validator derives the same one from the text.
 * The UTF-8 copy handed to native code is wiped after the call.
 */
object NativeConfigDocumentCheck : ConfigDocumentCheck {
    override fun matches(document: String, availability: ConfigAvailability): Boolean {
        val bytes = document.encodeToByteArray()
        return try {
            ModelNatives.configDocumentMatches(bytes, utf8(availability.wireName))
        } finally {
            bytes.fill(0)
        }
    }

    override fun validTemplate(document: String): Boolean {
        val bytes = document.encodeToByteArray()
        return try {
            ModelNatives.configValidTemplate(bytes)
        } finally {
            bytes.fill(0)
        }
    }
}
