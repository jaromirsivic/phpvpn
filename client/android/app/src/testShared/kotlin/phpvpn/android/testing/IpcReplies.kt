package phpvpn.android.testing

import phpvpn.android.bridge.ipc.FakeIpcTransport

/**
 * IPC v1 replies for the WP08 JVM and Compose UI tests, shared by both source
 * sets (src/testShared). They follow the closed schemas of WireValidation and
 * the WP06 fixtures (status-*.json, config-*.json, server-info.json); `$ID`
 * is replaced by FakeIpcTransport with the request id.
 */
object IpcReplies {
    const val CONFIG_PATH = "/data/user/0/io.github.jaromirsivic.phpvpn/no_backup/phpvpn/config/config.json"
    const val ATTEMPT = "3f2a7c1e-9b44-4b0e-8d2f-5c6a1e7b9d03"
    const val STARTED_AT = "2026-01-01T00:00:00.000Z"

    /** The nine counters in wire order; `dataPollsTotal` is derived. */
    data class Counters(
        val pollsEmpty: Long = 0,
        val pollsWithPayload: Long = 0,
        val requestBody: Long = 0,
        val responseBody: Long = 0,
        val packetsSent: Long = 0,
        val packetsReceived: Long = 0,
        val bytesSent: Long = 0,
        val bytesReceived: Long = 0,
    )

    /**
     * A complete get-status reply. Checks are server URL, secret, access key,
     * contracts. [diagnostic] adds a small http-octets-v1 capture of a failed
     * health request (only valid for an unrecoverable error in state error).
     */
    fun status(
        state: String = "disconnected",
        phase: String = "idle",
        epoch: Long = if (state == "disconnected" && phase == "idle") 0 else 3,
        checks: List<String> = List(4) { "pending" },
        tunnelReady: String = if (state == "connected") "yes" else "no",
        errorCode: String? = null,
        recoverable: Boolean = false,
        startedAt: String? = if (state == "connected" || state == "recovering") STARTED_AT else null,
        counters: Counters = Counters(),
        diagnostic: Boolean = false,
    ): String {
        val attempt = if (epoch == 0L) "null" else "\"$ATTEMPT\""
        val error = errorCode?.let { """{"code":"$it","description":"Safe public error","recoverable":$recoverable}""" } ?: "null"
        val started = startedAt?.let { "\"$it\"" } ?: "null"
        val c = counters
        val capture = """{"originalSizeBytes":4,"prefix":"\u0001\u0001\u0001\u0001","separator":"","suffix":""}"""
        val http = if (diagnostic) {
            ""","httpDiagnostic":{"format":"http-octets-v1","action":"health","responseComplete":true,""" +
                """"requestHeadersSource":"application","responseHeadersSource":"unavailable","request":$capture,"response":$capture}"""
        } else {
            ""
        }
        return """{"ipcVersion":1,"requestId":"${'$'}ID","status":{"connectionEpoch":$epoch,"connectAttemptId":$attempt,""" +
            """"state":"$state","phase":"$phase","stateChangedAt":"2026-09-04T18:52:31.412Z","connectionStartedAt":$started,""" +
            """"serverUrlCheck":"${checks[0]}","serverSecretCheck":"${checks[1]}","accessKeyCheck":"${checks[2]}",""" +
            """"communicationContractsCheck":"${checks[3]}","serverReachable":"unknown","serverVerified":"unknown",""" +
            """"tunnelReady":"$tunnelReady","lastError":$error,"dataPollsTotal":${c.pollsEmpty + c.pollsWithPayload},""" +
            """"dataPollsEmpty":${c.pollsEmpty},"dataPollsWithPayload":${c.pollsWithPayload},""" +
            """"serializedDataRequestBodyBytes":${c.requestBody},"serializedDataResponseBodyBytes":${c.responseBody},""" +
            """"ipv4PacketsSent":${c.packetsSent},"ipv4PacketsReceived":${c.packetsReceived},"ipv4BytesSent":${c.bytesSent},""" +
            """"ipv4BytesReceived":${c.bytesReceived},"recoveryCount":0,"clientVersion":"1.0.3","ipcVersion":1$http}}"""
    }

    val ALL_PASSED = List(4) { "success" }

    fun connected(counters: Counters = Counters(), startedAt: String = STARTED_AT) =
        status("connected", "running", checks = ALL_PASSED, counters = counters, startedAt = startedAt)

    fun connecting(checks: List<String> = listOf("success", "success", "pending", "pending")) =
        status("connecting", "access-key-check", checks = checks, tunnelReady = "unknown")

    fun reconnecting() = status("connected", "reconnecting", checks = ALL_PASSED)

    fun stopping() = status("connected", "stopping", checks = ALL_PASSED)

    fun disconnected() = status()

    /** Final connect failure (06). A transport error fails the URL check; diagnostic data only when captured. */
    fun failed(code: String = "endpoint_unavailable_or_invalid", diagnostic: Boolean = false): String {
        val checks = when (code) {
            "endpoint_unavailable_or_invalid" -> listOf("failed", "pending", "pending", "pending")
            "access_key_rejected" -> listOf("success", "success", "failed", "pending")
            else -> listOf("success", "failed", "pending", "pending")
        }
        return status("error", "idle", checks = checks, tunnelReady = "unknown", errorCode = code, diagnostic = diagnostic)
    }

    /** A correlated classic error envelope (busy, config_*, ...). */
    fun error(code: String, recoverable: Boolean = false) =
        """{"ipcVersion":1,"requestId":"${'$'}ID","error":{"code":"$code","description":"Safe public error","recoverable":$recoverable}}"""

    /** Availabilities with a readable document carry a revision token, the others null. */
    private val READABLE = setOf("valid", "repairable", "invalid")

    fun configState(availability: String = "valid", reasonCode: String? = null, state: String = "disconnected") =
        """{"ipcVersion":1,"requestId":"${'$'}ID","result":{"availability":"$availability","reasonCode":${reasonCode?.let { "\"$it\"" } ?: "null"},""" +
            """"configPath":"$CONFIG_PATH","revisionToken":${if (availability in READABLE) "\"revision\"" else "null"},""" +
            """"serviceInstanceId":"00000000-0000-4000-8000-000000000001","connectionEpoch":0,"vpnState":"$state","vpnPhase":"idle"}}"""

    /** get-config-document with a valid document that holds the public members and secrets. */
    fun configDocument(
        serverUrl: String = "https://vpn.example.com/phpvpn.php",
        virtualIp: String = "10.80.0.12",
        vpnCidr: String = "10.80.0.0/24",
    ): String {
        val document = """{\n  \"masterSetup\": {\n    \"serverSecret\": \"CHANGE_THIS_SERVER_SECRET\",\n""" +
            """    \"serverUrl\": \"$serverUrl\",\n    \"httpHeaders\": \"Cache-Control: no-cache\",\n""" +
            """    \"trustAnyTlsCertificate\": true,\n    \"useDefaultServerSecretInWizard\": false,\n""" +
            """    \"vpnCidr\": \"$vpnCidr\",\n    \"virtualIp\": \"$virtualIp\",\n    \"autoDetectIpAddressInWizard\": true,\n""" +
            """    \"accessKey\": \"0202020202020202020202020202020202020202020202020202020202020202\",\n""" +
            """    \"useDefaultAccessKeyInWizard\": true,\n    \"autoConnectOnStartup\": false\n  },\n""" +
            """  \"communicationContracts\": {}\n}\n"""
        return """{"ipcVersion":1,"requestId":"${'$'}ID","result":{"configPath":"$CONFIG_PATH","availability":"valid",""" +
            """"reasonCode":null,"document":"$document","revisionToken":"revision"}}"""
    }

    fun configDocumentMissing() =
        """{"ipcVersion":1,"requestId":"${'$'}ID","result":{"configPath":"$CONFIG_PATH","availability":"missing",""" +
            """"reasonCode":"config_not_found","document":null,"revisionToken":null}}"""

    /** One remote client of [serverInfo]: address, lastSeen and its masked contract hash (or none). */
    data class Peer(val address: String, val lastSeen: String, val hash: String?)

    /**
     * A get-server-info reply in the schema of server_info_fixture.h: the local
     * client is always listed (the server returns it), [peers] follow.
     * [localHashes] are the local masked contracts per peer.
     */
    fun serverInfo(
        local: String = "10.80.0.12",
        generatedAt: String = "2026-10-30T08:41:03.000Z",
        peers: List<Peer> = emptyList(),
        localHashes: Map<String, String> = emptyMap(),
    ): String {
        val clients = buildList {
            add("""{"address":"$local","lastSeen":"$generatedAt","maskedCommunicationContracts":[]}""")
            peers.forEach { peer ->
                val masked = peer.hash?.let { """[{"peer":"$local","hash":"$it"}]""" } ?: "[]"
                add("""{"address":"${peer.address}","lastSeen":"${peer.lastSeen}","maskedCommunicationContracts":$masked}""")
            }
        }
        val info = """{"serverVersion":"1.0.0-synthetic","generatedAt":"$generatedAt","clients":[${clients.joinToString(",")}],""" +
            """"limits":{"maxDataRecordsPerRequest":16,"maxRequestBodyBytes":983040,"maxQueueBytesPerClient":16777216,""" +
            """"queueTtlSeconds":60,"minDataPollIntervalMs":50,"maxDataPollIntervalMs":3200}}"""
        val escaped = info.replace("\\", "\\\\").replace("\"", "\\\"")
        val contracts = localHashes.entries.sortedBy { it.key }.joinToString(",") { """{"peer":"${it.key}","hash":"${it.value}"}""" }
        return """{"ipcVersion":1,"requestId":"${'$'}ID","result":{"infoPlaintext":"$escaped","localAddress":"$local",""" +
            """"localMaskedCommunicationContracts":[$contracts]}}"""
    }

    val HASH_A = "a".repeat(64)
    val HASH_B = "b".repeat(64)

    /** The design sample of 28: one valid, one invalid, one offline peer. */
    fun serverInfoSample() = serverInfo(
        peers = listOf(
            Peer("10.80.0.2", "2026-10-30T08:41:02.000Z", HASH_A),
            Peer("10.80.0.3", "2026-10-30T08:40:58.000Z", HASH_B),
            Peer("10.80.0.4", "2026-10-30T08:25:10.000Z", HASH_A),
        ),
        localHashes = mapOf("10.80.0.2" to HASH_A, "10.80.0.3" to HASH_A, "10.80.0.4" to HASH_A),
    )
}

fun FakeIpcTransport.respondText(command: String, text: String) = respond(command) { FakeIpcTransport.Reply.Text(text) }

fun FakeIpcTransport.respondTimeout(command: String) = respond(command) { FakeIpcTransport.Reply.Timeout }
