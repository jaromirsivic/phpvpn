package phpvpn.android.bridge.models

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.StrictJson
import phpvpn.android.bridge.ipc.StructuralConfigDocumentCheck
import phpvpn.android.bridge.jni.NativeConfigDocumentCheck
import phpvpn.android.bridge.runtime.RuntimeHarness

/**
 * The native configuration document check (WP04) that replaces WP06's
 * structural approximation: a declared availability is accepted only when the
 * full validator derives the same one (documentMatches of
 * core/ipc/wizard_ipc.cpp, wizard_ipc_configuration_documents_* of
 * test_wizard_ipc.cpp), which closes the two cases WP06 documented as
 * deviations.
 */
@RunWith(AndroidJUnit4::class)
class ConfigDocumentCheckJniTest {
    private fun fixtureDocument(name: String): Pair<String, ConfigAvailability> {
        val result = (StrictJson.parse(RuntimeHarness.fixture(name, "00000000-0000-4000-8000-000000000001")) as JsonObject)["result"] as JsonObject
        val availability = ConfigAvailability.entries.first { it.wireName == (result["availability"] as JsonPrimitive).content }
        return (result["document"] as JsonPrimitive).content to availability
    }

    @Test
    fun declaredAvailabilityMustBeTheDerivedOne() {
        val readable = listOf(ConfigAvailability.VALID, ConfigAvailability.REPAIRABLE, ConfigAvailability.INVALID)
        for (name in listOf("config-document-valid.json", "config-document-repairable.json", "config-document-invalid.json")) {
            val (document, declared) = fixtureDocument(name)
            for (availability in readable) {
                assertEquals("$name as ${availability.wireName}", availability == declared, NativeConfigDocumentCheck.matches(document, availability))
            }
        }
        val valid = fixtureDocument("config-document-valid.json").first
        // The two WP06 deviations: a valid text declared repairable or invalid.
        assertTrue(StructuralConfigDocumentCheck.matches(valid, ConfigAvailability.INVALID))
        assertFalse(NativeConfigDocumentCheck.matches(valid, ConfigAvailability.INVALID))
        assertFalse(NativeConfigDocumentCheck.matches(valid, ConfigAvailability.REPAIRABLE))
        assertFalse(NativeConfigDocumentCheck.matches(valid, ConfigAvailability.MISSING))
        assertTrue(NativeConfigDocumentCheck.matches("{", ConfigAvailability.INVALID))
        assertFalse(NativeConfigDocumentCheck.matches("{", ConfigAvailability.REPAIRABLE))
        // A wrong value inside a syntactically complete document is not valid.
        assertFalse(NativeConfigDocumentCheck.matches(valid.replace("10.99.99.0/24", "10.99.99.0/16"), ConfigAvailability.VALID))
    }

    @Test
    fun templateCheckRunsTheFullValidator() {
        val template = RuntimeHarness.fixture("default-config-document.json")
        assertTrue(NativeConfigDocumentCheck.validTemplate(template))
        assertFalse(NativeConfigDocumentCheck.validTemplate(template.replace("10.99.99.0/24", "10.99.99.0/16")))
        assertFalse(NativeConfigDocumentCheck.validTemplate(template.replace("\"accessKey\": \"02", "\"accessKey\": \"zz")))
        assertFalse(NativeConfigDocumentCheck.validTemplate("{}"))
    }
}
