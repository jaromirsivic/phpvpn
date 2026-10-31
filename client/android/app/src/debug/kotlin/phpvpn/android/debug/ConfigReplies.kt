package phpvpn.android.debug

import kotlinx.serialization.json.JsonPrimitive
import phpvpn.android.bridge.ipc.FakeIpcTransport

/**
 * Synthetic IPC v1 replies of the configuration commands for the WP10 JVM
 * and Compose UI tests (debug source set, so both test source sets see it).
 * Every `$ID` is replaced with the request id by [FakeIpcTransport]. The
 * documents are synthetic: the editor template of client.md and contract keys
 * from its test vectors, no real secrets.
 */
object ConfigReplies {
    const val CONFIG_PATH = "/data/user/0/io.github.jaromirsivic.phpvpn/no_backup/phpvpn/config/config.json"

    /** `makeDefaultEditorDocument` of client.md ("Edit Config: skutečný obsah, vytvoření a Reset"). */
    val DEFAULT_DOCUMENT: String = document()

    /** The editor template with another local IP, CIDR and contracts (peer to key). */
    fun document(
        virtualIp: String = "10.99.99.2",
        vpnCidr: String = "10.99.99.0/24",
        contracts: Map<String, String>? = emptyMap(),
    ): String = buildString {
        append("{\n  \"masterSetup\": {\n")
        append("    \"serverSecret\": \"CHANGE_THIS_SERVER_SECRET\",\n")
        append("    \"serverUrl\": \"http://127.0.0.1/phpvpn.php\",\n")
        append("    \"httpHeaders\": \"Cache-Control: no-cache\\r\\nPragma: no-cache\\r\\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:155.0) Gecko/20100101 Firefox/155.0\",\n")
        append("    \"trustAnyTlsCertificate\": true,\n")
        append("    \"useDefaultServerSecretInWizard\": false,\n")
        append("    \"vpnCidr\": \"$vpnCidr\",\n")
        append("    \"virtualIp\": \"$virtualIp\",\n")
        append("    \"autoDetectIpAddressInWizard\": true,\n")
        append("    \"accessKey\": \"0202020202020202020202020202020202020202020202020202020202020202\",\n")
        append("    \"useDefaultAccessKeyInWizard\": true,\n")
        append("    \"autoConnectOnStartup\": false\n  }")
        if (contracts != null) {
            append(",\n  \"communicationContracts\": {")
            append(contracts.entries.joinToString(",") { (peer, key) -> "\n    \"$peer\": {\n      \"payloadKey\": \"$key\"\n    }" })
            append(if (contracts.isEmpty()) "}" else "\n  }")
        }
        append("\n}\n")
    }

    /** Every availability other than valid carries a safe reason code. */
    private fun reasonFor(availability: String): String? = when (availability) {
        "valid" -> null
        "missing" -> "config_not_found"
        "invalid" -> "config_invalid_json"
        "repairable" -> "config_invalid_value"
        else -> "config_unreadable"
    }

    private fun quote(text: String?): String = if (text == null) "null" else JsonPrimitive(text).toString()

    private fun result(body: String) = "{\"ipcVersion\":1,\"requestId\":\"\$ID\",\"result\":$body}"

    fun configDocument(availability: String, document: String?, reasonCode: String? = reasonFor(availability), revision: String? = "revision-1"): String =
        result(
            "{\"configPath\":${quote(CONFIG_PATH)},\"availability\":\"$availability\",\"reasonCode\":${quote(reasonCode)}," +
                "\"document\":${quote(document)},\"revisionToken\":${quote(if (document == null) null else revision)}}",
        )

    fun missing(): String = configDocument("missing", null, "config_not_found")

    fun valid(document: String = DEFAULT_DOCUMENT): String = configDocument("valid", document)

    fun createDefault(created: Boolean, availability: String = "valid", document: String = DEFAULT_DOCUMENT): String =
        result(
            "{\"created\":$created,\"configPath\":${quote(CONFIG_PATH)},\"availability\":\"$availability\"," +
                "\"document\":${quote(document)},\"revisionToken\":\"revision-created\"}",
        )

    fun defaultConfig(document: String = DEFAULT_DOCUMENT): String = result("{\"document\":${quote(document)}}")

    /** The plain acknowledgement of set-config. */
    const val ACK = "{\"ipcVersion\":1,\"requestId\":\"\$ID\",\"result\":\"ok\"}"

    /** The classic error envelope of set-config. */
    fun error(code: String, description: String, recoverable: Boolean = false): String =
        "{\"ipcVersion\":1,\"requestId\":\"\$ID\",\"error\":{\"code\":\"$code\",\"description\":${quote(description)},\"recoverable\":$recoverable}}"

    /** A rejection that proves nothing was published (canonical description of error.cpp). */
    val INVALID_JSON = error("config_invalid_json", "The configuration is not a valid JSON document.")

    /** The curated header validator text with its line number. */
    val INVALID_HEADER = error("config_invalid_value", "masterSetup.httpHeaders line 2: empty line or missing colon")

    /** config_write_failed does not prove an unchanged disk: the result stays unknown. */
    val WRITE_FAILED = error("config_write_failed", "The configuration could not be written.")

    private const val STATUS_ID = "3f2a7c1e-9b44-4b0e-8d2f-5c6a1e7b9d03"

    fun status(state: String, phase: String, epoch: Int = 3, started: Boolean = state == "connected", tunnel: String = if (state == "connected") "yes" else "no"): String =
        "{\"ipcVersion\":1,\"requestId\":\"\$ID\",\"status\":{\"connectionEpoch\":$epoch," +
            "\"connectAttemptId\":${if (epoch == 0) "null" else "\"$STATUS_ID\""},\"state\":\"$state\",\"phase\":\"$phase\"," +
            "\"stateChangedAt\":\"2026-01-01T00:00:00.000Z\",\"connectionStartedAt\":${if (started) "\"2026-01-01T00:00:00.000Z\"" else "null"}," +
            "\"serverUrlCheck\":\"success\",\"serverSecretCheck\":\"success\",\"accessKeyCheck\":\"success\"," +
            "\"communicationContractsCheck\":\"success\",\"serverReachable\":\"yes\",\"serverVerified\":\"yes\",\"tunnelReady\":\"$tunnel\"," +
            "\"lastError\":null,\"dataPollsTotal\":0,\"dataPollsEmpty\":0,\"dataPollsWithPayload\":0,\"serializedDataRequestBodyBytes\":0," +
            "\"serializedDataResponseBodyBytes\":0,\"ipv4PacketsSent\":0,\"ipv4PacketsReceived\":0,\"ipv4BytesSent\":0,\"ipv4BytesReceived\":0," +
            "\"recoveryCount\":0,\"clientVersion\":\"1.0.3\",\"ipcVersion\":1}}"

    val CONNECTED = status("connected", "running")
    val STOPPING = status("connected", "stopping")
    val DISCONNECTED_SAME = status("disconnected", "idle", started = false)
    val DISCONNECTED = status("disconnected", "idle", epoch = 0, started = false)

    /** The `config` member of a recorded set-config request. */
    fun submitted(request: FakeIpcTransport.Recorded): String = (request.json["config"] as JsonPrimitive).content

    /** Answers [command] with [text] for every request. */
    fun FakeIpcTransport.answer(command: String, text: String) = respond(command) { FakeIpcTransport.Reply.Text(text) }

    /** Answers the next requests of [command] in order. */
    fun FakeIpcTransport.answerNext(command: String, vararg texts: String) = enqueueText(command, *texts)
}
