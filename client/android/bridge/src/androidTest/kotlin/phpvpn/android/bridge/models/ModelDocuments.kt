package phpvpn.android.bridge.models

import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.ipc.StrictJson
import phpvpn.android.bridge.runtime.RuntimeHarness

/**
 * Documents of client/tests/unit/contracts_fixture.h and one_contract_fixture.h,
 * built from the default editor document (default-config-document.json, the
 * literal kDefaultConfigDocument).
 */
object ModelDocuments {
    private val defaultDocument: JsonObject by lazy { StrictJson.parse(RuntimeHarness.fixture("default-config-document.json")) as JsonObject }

    /** `contracts_test::document(local, cidr, contractsPresent)`. */
    fun document(local: String = "10.99.99.2", cidr: String = "10.99.99.0/24", contractsPresent: Boolean = false): String {
        val setup = defaultDocument["masterSetup"] as JsonObject
        val master = JsonObject(setup + ("virtualIp" to JsonPrimitive(local)) + ("vpnCidr" to JsonPrimitive(cidr)))
        val members = LinkedHashMap<String, kotlinx.serialization.json.JsonElement>()
        members["masterSetup"] = master
        if (contractsPresent) members["communicationContracts"] = JsonObject(emptyMap())
        return JsonObject(members).toString()
    }

    const val ONE_CONTRACT_KEY = "918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d"

    /** `test::oneContractDocument()`: one enabled contract with 10.99.99.3. */
    fun oneContractDocument(key: String = ONE_CONTRACT_KEY): String {
        val contracts = JsonObject(mapOf("10.99.99.3" to JsonObject(mapOf("payloadKey" to JsonPrimitive(key)))))
        return JsonObject(defaultDocument + ("communicationContracts" to contracts)).toString()
    }

    fun secret(text: String): SecretText = SecretText.of(text)
}
