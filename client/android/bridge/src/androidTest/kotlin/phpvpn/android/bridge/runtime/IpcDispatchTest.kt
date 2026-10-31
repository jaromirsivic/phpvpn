package phpvpn.android.bridge.runtime

import android.net.VpnService
import android.system.Os
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.File
import java.util.concurrent.Callable
import java.util.concurrent.Executors
import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.jsonPrimitive
import org.junit.AfterClass
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import phpvpn.android.bridge.NativeBridge
import phpvpn.android.bridge.NativeRuntime
import phpvpn.android.bridge.VpnHosts
import phpvpn.android.bridge.ipc.AccessPayload
import phpvpn.android.bridge.ipc.CommitState
import phpvpn.android.bridge.ipc.ConfigAvailability
import phpvpn.android.bridge.ipc.ConnectionState
import phpvpn.android.bridge.ipc.IdSource
import phpvpn.android.bridge.ipc.IpcOutcome
import phpvpn.android.bridge.ipc.JsonChecks
import phpvpn.android.bridge.ipc.OperationKind
import phpvpn.android.bridge.ipc.OperationResult
import phpvpn.android.bridge.ipc.OperationState
import phpvpn.android.bridge.ipc.Receipt
import phpvpn.android.bridge.ipc.SecretPayload
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.ipc.UrlPayload
import phpvpn.android.bridge.ipc.WizardMode
import phpvpn.android.bridge.ipc.WizardSession
import phpvpn.android.bridge.jni.NativeServerInfo
import phpvpn.android.bridge.jni.ServerInfoParse
import phpvpn.android.bridge.runtime.RuntimeHarness.assertSameMembers
import phpvpn.android.bridge.runtime.RuntimeHarness.fixture
import phpvpn.android.bridge.runtime.RuntimeHarness.json
import phpvpn.android.bridge.runtime.RuntimeHarness.plain
import phpvpn.android.bridge.runtime.RuntimeHarness.raw

/**
 * The full IPC v1 command set through `nativeDispatch` against the real
 * runtime in the application sandbox (WP04): replies are compared with the
 * WP06 fixtures (the desktop shapes) and validated by the WP06 client, and the
 * transport rules of client/tests/unit/test_ipc.cpp and the executors hold
 * (busy, timeouts, size limit, unknown command, correlation).
 */
@RunWith(AndroidJUnit4::class)
class IpcDispatchTest {
    @Before
    fun freshRuntime() {
        // A TunnelHost left by another test class would change the Connect outcome.
        VpnHosts.unregisterTunnelHost()
        RuntimeHarness.restart()
    }

    @Test
    fun getStatusMatchesTheDesktopShape() {
        val id = IdSource.RANDOM.next()
        val reply = json(assertNotNull(raw(plain("get-status", id), 400)))
        assertEquals(id, JsonChecks.string(reply["requestId"]))
        assertSameMembers(json(fixture("status-disconnected.json", id)), reply, ordered = true)
        val status = runBlocking { RuntimeHarness.client().getStatus() }
        assertTrue("$status", status is IpcOutcome.Success)
        val snapshot = (status as IpcOutcome.Success).value
        assertEquals(ConnectionState.DISCONNECTED, snapshot.state)
        assertEquals(NativeBridge.version(), snapshot.clientVersion)
        assertEquals(1, snapshot.ipcVersion)
    }

    @Test
    fun configurationCommandsAnswerWithTheDesktopShapes() = runBlocking {
        val client = RuntimeHarness.client()
        // The runtime created the default document at startup (ensureDefaultAtStartup).
        val stored = RuntimeHarness.configFile.readText()

        val stateId = IdSource.RANDOM.next()
        val state = json(assertNotNull(raw(plain("get-config-state", stateId))))
        assertSameMembers(json(fixture("config-state.json", stateId)), state)
        val typedState = (client.getConfigState() as IpcOutcome.Success).value
        assertEquals(ConfigAvailability.VALID, typedState.availability)
        assertEquals(RuntimeHarness.configFile.absolutePath, typedState.configPath)

        val documentId = IdSource.RANDOM.next()
        assertSameMembers(json(fixture("config-document-valid.json", documentId)), json(assertNotNull(raw(plain("get-config-document", documentId)))))
        val document = (client.getConfigDocument() as IpcOutcome.Success).value
        assertEquals(ConfigAvailability.VALID, document.availability)
        assertEquals(stored, document.document!!.reveal())
        document.wipe()

        val defaultId = IdSource.RANDOM.next()
        assertSameMembers(json(fixture("default-config.json", defaultId)), json(assertNotNull(raw(plain("get-default-config", defaultId)))))
        val template = (client.getDefaultConfig() as IpcOutcome.Success).value
        assertEquals(fixture("default-config-document.json").trimEnd(), template.document.reveal().trimEnd())

        // get-config answers without an envelope: the exact stored document.
        assertEquals(stored, raw(plain("get-config")))
        assertEquals(stored, (client.getConfig() as IpcOutcome.Success).value.reveal())

        val createId = IdSource.RANDOM.next()
        val create = json(assertNotNull(raw(plain("create-default-config", createId))))
        assertSameMembers(json(fixture("create-default-config-race.json", createId)), create)
        assertEquals(false, (create["result"] as JsonObject)["created"]?.jsonPrimitive?.content?.toBooleanStrict())
    }

    @Test
    fun createDefaultConfigCreatesOnceAndNeverOverwrites() = runBlocking {
        val client = RuntimeHarness.client()
        assertTrue(RuntimeHarness.configFile.delete())
        val missing = (client.getConfigDocument() as IpcOutcome.Success).value
        assertEquals(ConfigAvailability.MISSING, missing.availability)
        assertNull(missing.document)

        val created = (client.createDefaultConfig() as IpcOutcome.Success).value
        assertTrue(created.created)
        assertEquals(ConfigAvailability.VALID, created.availability)
        assertEquals(created.document.reveal(), RuntimeHarness.configFile.readText())
        assertEquals("600", Integer.toOctalString(Os.stat(RuntimeHarness.configFile.path).st_mode and 0x1ff))

        val custom = RuntimeHarness.document()
        val setId = IdSource.RANDOM.next()
        assertEquals(fixture("ack.json", setId), raw(RuntimeHarness.setConfig(custom, setId), 30_000))
        val again = (client.createDefaultConfig() as IpcOutcome.Success).value
        assertFalse(again.created)
        assertEquals(custom, again.document.reveal())
        assertEquals(custom, RuntimeHarness.configFile.readText())
    }

    @Test
    fun setConfigPublishesWithTheExchangeBackup() {
        val first = RuntimeHarness.document(url = "http://10.0.2.2:18094/first/phpvpn.php")
        val second = RuntimeHarness.document(url = "http://10.0.2.2:18094/second/phpvpn.php")
        val id = IdSource.RANDOM.next()
        assertEquals(fixture("ack.json", id), raw(RuntimeHarness.setConfig(first, id), 30_000))
        assertEquals(fixture("ack.json", id), raw(RuntimeHarness.setConfig(second, id), 30_000))
        val directory = RuntimeHarness.configFile.parentFile!!
        val previous = File(directory, "config.previous")
        assertEquals(second, RuntimeHarness.configFile.readText())
        assertEquals(first, previous.readText())
        // The exchange strategy moves the original inode instead of linking it.
        for (file in listOf(RuntimeHarness.configFile, previous)) {
            val stat = Os.stat(file.path)
            assertEquals("links of ${file.name}", 1L, stat.st_nlink)
            assertEquals("mode of ${file.name}", "600", Integer.toOctalString(stat.st_mode and 0x1ff))
            assertEquals(Os.getuid(), stat.st_uid)
        }
        // No stage is left; the publish journal stays with its CONFIRMED record (config_files_posix).
        val names = directory.list()!!.toSet()
        assertTrue("stage files are cleaned up: $names", names.all { it in setOf("config.json", "config.previous", "config.publish-journal") })
        // A rejected document changes nothing.
        val rejectedId = IdSource.RANDOM.next()
        val rejected = json(assertNotNull(raw(RuntimeHarness.setConfig(second.replace("10.99.99.0/24", "10.99.99.0/16"), rejectedId))))
        assertEquals("config_invalid_cidr", JsonChecks.string((rejected["error"] as JsonObject)["code"]))
        assertEquals(second, RuntimeHarness.configFile.readText())
    }

    @Test
    fun infoDiagnosticsAndDismissAnswerCorrelated() = runBlocking {
        val client = RuntimeHarness.client()
        val diagnosticsId = IdSource.RANDOM.next()
        assertSameMembers(json(fixture("diagnostics.json", diagnosticsId)), json(assertNotNull(raw(plain("get-diagnostics", diagnosticsId)))))
        assertTrue(client.getDiagnostics() is IpcOutcome.Success)

        val reachable = RuntimeHarness.relayReachable()
        val setId = IdSource.RANDOM.next()
        assertEquals(fixture("ack.json", setId), raw(RuntimeHarness.setConfig(RuntimeHarness.document(), setId), 30_000))
        val info = client.getInfo()
        val serverInfo = client.getServerInfo()
        Log.i(TAG, "relay reachable=$reachable get-info=${info.javaClass.simpleName} get-server-info=${serverInfo.javaClass.simpleName}")
        for (outcome in listOf(info, serverInfo)) {
            assertTrue("a correlated, schema-valid reply: $outcome", outcome is IpcOutcome.Success || outcome is IpcOutcome.ServiceError)
        }
        if (reachable) {
            assertTrue("$info", info is IpcOutcome.Success)
            assertTrue("$serverInfo", serverInfo is IpcOutcome.Success)
            val reply = (serverInfo as IpcOutcome.Success).value
            val parsed = NativeServerInfo.parse(reply.rawReply.encodeToByteArray())
            assertTrue("$parsed", parsed is ServerInfoParse.Parsed)
            assertEquals("10.99.99.2", (parsed as ServerInfoParse.Parsed).view.localAddress)
        }

        // dismiss-connect-diagnostic of an attempt the service never had.
        val dismissId = IdSource.RANDOM.next()
        val dismiss = json(
            assertNotNull(
                raw("""{"ipcVersion":1,"requestId":"$dismissId","command":"dismiss-connect-diagnostic","connectionEpoch":1,"connectAttemptId":"${IdSource.RANDOM.next()}"}"""),
            ),
        )
        assertEquals(dismissId, JsonChecks.string(dismiss["requestId"]))
        assertTrue("$dismiss", dismiss.keys == setOf("ipcVersion", "requestId", "result") || dismiss.keys == setOf("ipcVersion", "requestId", "error"))
        val typed = client.dismissConnectDiagnostic(1uL, IdSource.RANDOM.next())
        assertTrue("$typed", typed is IpcOutcome.Success || typed is IpcOutcome.ServiceError)
    }

    @Test
    fun connectWithoutTunnelHostEndsInATunnelError() = runBlocking {
        assumeTrue("relay ${RuntimeHarness.serverUrl} is not reachable", RuntimeHarness.relayReachable())
        val client = RuntimeHarness.client()
        val setId = IdSource.RANDOM.next()
        assertEquals(fixture("ack.json", setId), raw(RuntimeHarness.setConfig(RuntimeHarness.document(), setId), 30_000))
        // VpnService.prepare() == null: the consent exists, so the missing service is the cause.
        // Without consent loadAndVerify fails with tunnel_permission_missing: not recoverable,
        // state error (screen 37). With consent (an earlier VpnService test of this APK may
        // have granted it) the missing service is tunnel_adapter_failed, which the core
        // publishes as tunnel_start_failed and, being recoverable on every platform, retries
        // in state recovering instead of error (see the WP04 report, deviations).
        val consent = VpnService.prepare(RuntimeHarness.context) == null
        val expectedCode = if (consent) "tunnel_start_failed" else "tunnel_permission_missing"
        val expectedStates = if (consent) setOf(ConnectionState.RECOVERING, ConnectionState.ERROR) else setOf(ConnectionState.ERROR)

        // The tunnel stage is reached only after health, info and the access key check against
        // the relay. php -S closes right after its last write, and through the emulator network
        // a reply then lost its final byte now and then (wire_invalid_json; see the WP04 report).
        // The harness relay URL goes through a proxy that avoids it; an attempt that still stops
        // before the tunnel is repeated, at most three times, and every attempt is logged.
        var last: phpvpn.android.bridge.ipc.StatusSnapshot? = null
        for (attempt in 1..3) {
            val accepted = client.connect()
            assertTrue("$accepted", accepted is IpcOutcome.Success)
            val deadline = System.nanoTime() + 30_000_000_000L
            while (System.nanoTime() < deadline) {
                last = (client.getStatus() as? IpcOutcome.Success)?.value
                if (last?.state == ConnectionState.ERROR || last?.lastError?.code?.startsWith("tunnel_") == true) break
                Thread.sleep(100)
            }
            Log.i(TAG, "attempt $attempt consent=$consent state=${last?.state} lastError=${last?.lastError?.code} " +
                "checks=${last?.serverUrlCheck}/${last?.serverSecretCheck}/${last?.accessKeyCheck}")
            if (last?.lastError?.code?.startsWith("tunnel_") == true) break
            assertTrue(client.disconnect() is IpcOutcome.Success)
            RuntimeHarness.waitUntil(10_000, "Disconnect") {
                (runBlocking { client.getStatus() } as? IpcOutcome.Success)?.value?.state == ConnectionState.DISCONNECTED
            }
        }
        assertEquals(expectedCode, last!!.lastError?.code)
        assertTrue("${last!!.state}", last!!.state in expectedStates)
        assertTrue(last!!.tunnelReady != phpvpn.android.bridge.ipc.Tristate.YES)

        // The notification snapshot of WP05 reports the same state.
        val notification = json(assertNotNull(NativeBridge.nativeStatus()).decodeToString())
        assertEquals(NativeBridge.STATUS_REQUEST_ID, JsonChecks.string(notification["requestId"]))
        assertTrue(JsonChecks.string((notification["status"] as JsonObject)["state"]) in setOf("recovering", "error"))

        assertTrue(client.disconnect() is IpcOutcome.Success)
        RuntimeHarness.waitUntil(10_000, "Disconnect") {
            (runBlocking { client.getStatus() } as? IpcOutcome.Success)?.value?.state == ConnectionState.DISCONNECTED
        }
    }

    @Test
    fun everyWizardCommandAgainstTheRelay() = runBlocking {
        assumeTrue("relay ${RuntimeHarness.serverUrl} is not reachable", RuntimeHarness.relayReachable())
        // A garbled relay reply (see connectWithoutTunnelHostEndsInATunnelError) fails one check
        // with a wire or verification code; the whole flow is then repeated, at most three times.
        for (attempt in 1..3) {
            try {
                wizardFlow()
                return@runBlocking
            } catch (flake: RelayFlake) {
                Log.i(TAG, "wizard attempt $attempt: relay reply refused (${flake.message})")
                if (attempt == 3) throw AssertionError("the relay garbled a reply in three attempts: ${flake.message}")
            }
        }
    }

    private class RelayFlake(code: String) : Exception(code)

    private suspend fun wizardFlow() {
        val client = RuntimeHarness.client()
        val setId = IdSource.RANDOM.next()
        assertEquals(fixture("ack.json", setId), raw(RuntimeHarness.setConfig(RuntimeHarness.document(), setId), 30_000))

        val view = ok(client.wizardOpen())
        assertEquals(WizardMode.EDIT, view.mode)
        val session = WizardSession(view.serviceInstanceId, view.sessionId)
        val keepalive = ok(client.wizardKeepalive(session))
        assertTrue(keepalive.alive)
        assertEquals(60_000L, keepalive.leaseDurationMs)

        val fields = view.fields!!.deepCopy()
        // One manual attempt per step: a fresh client attempt id, the session sequence grows (WizardController).
        val attempts = List(3) { IdSource.RANDOM.next() }
        val url = complete(client, session, ok(client.wizardCheckUrl(session, attempts[0], 1uL, fields.serverUrl, fields.httpHeaders)).operationId)
        assertEquals(OperationKind.URL, url.kind)
        assertEquals("${url.error}", OperationState.SUCCEEDED, url.operationState)
        assertTrue((url.payload as UrlPayload).verified)

        val secret = SecretText.of("CHANGE_THIS_SERVER_SECRET")
        val secretOperation = complete(
            client, session,
            ok(client.wizardCheckSecret(session, attempts[1], 2uL, fields.serverUrl, secret, fields.httpHeaders)).operationId,
        )
        secret.wipe()
        assertEquals("${secretOperation.error}", OperationState.SUCCEEDED, secretOperation.operationState)
        val secretPayload = secretOperation.payload as SecretPayload
        assertTrue(secretPayload.verified)

        // autoDetectIpAddressInWizard: the access check must use the suggested address and,
        // with useDefaultAccessKeyInWizard, its default key (wizard-default relay: .2 to .15).
        val ip = secretPayload.suggestedIp ?: fields.virtualIp
        val octet = ip.substringAfterLast('.').toInt()
        val chosen = fields.copy(virtualIp = ip, accessKey = SecretText.of("%02x".format(octet).repeat(32)))
        val access = complete(client, session, ok(client.wizardCheckAccess(session, attempts[2], 3uL, chosen)).operationId)
        assertEquals("${access.error}", OperationState.SUCCEEDED, access.operationState)
        assertTrue((access.payload as AccessPayload).verified)

        val commitId = IdSource.RANDOM.next()
        val finish = complete(client, session, ok(client.wizardFinish(session, commitId, chosen, false)).operationId)
        assertEquals("${finish.error}", OperationState.SUCCEEDED, finish.operationState)
        assertEquals(commitId, (finish.payload as Receipt).commitId)
        val commit = ok(client.wizardGetCommit(session, commitId))
        assertEquals(CommitState.CONFIRMED, commit.commitState)
        // Cancelling a finished operation or a closed attempt reports their final state.
        val cancelled = ok(client.wizardCancelOperation(session, finish.operationId))
        assertEquals(OperationState.SUCCEEDED, cancelled.operationState)
        val closedAttempt = ok(client.wizardCancelAttempt(session, attempts[2], 3uL))
        assertEquals(3uL, closedAttempt.attemptSequence)

        val reset = ok(client.wizardReset(session))
        assertEquals(WizardMode.RESET, reset.mode)
        // Resume is only meaningful after an interruption; the service answers with a correlated error.
        val resume = client.wizardResume(session, view.connectionEpoch)
        assertTrue("$resume", resume is IpcOutcome.Success || resume is IpcOutcome.ServiceError)
        assertTrue(ok(client.wizardClose(session)).closed)
        fields.wipe()
        chosen.accessKey.wipe()
    }

    @Test
    fun rawRequestsFollowTheDesktopContract() {
        val id = IdSource.RANDOM.next()
        val unknown = json(assertNotNull(raw("""{"ipcVersion":1,"requestId":"$id","command":"shutdown"}""")))
        // A classic request that fails validation is answered uncorrelated (ipc_dispatcher.cpp).
        assertEquals("", JsonChecks.string(unknown["requestId"]))
        assertEquals("ipc_unknown_command", errorCode(unknown))
        // A Wizard-family request that fails validation keeps a valid UUIDv4 correlation.
        val wizardId = IdSource.RANDOM.next()
        val wizard = json(assertNotNull(raw("""{"ipcVersion":1,"requestId":"$wizardId","command":"wizard-keepalive"}""")))
        assertEquals(wizardId, JsonChecks.string(wizard["requestId"]))
        assertEquals("ipc_malformed", errorCode(wizard))

        assertEquals("ipc_unsupported_version", errorCode(json(assertNotNull(raw("""{"ipcVersion":2,"requestId":"$id","command":"get-status"}""")))))
        assertEquals("ipc_malformed", errorCode(json(assertNotNull(raw("not json")))))
        assertEquals("ipc_malformed", errorCode(json(assertNotNull(raw("""{"ipcVersion":1,"requestId":"$id","command":"get-status","extra":1}""")))))
        assertEquals("ipc_malformed", errorCode(json(assertNotNull(raw("""{"ipcVersion":1,"requestId":"not-a-uuid","command":"get-status"}""")))))
        assertEquals("ipc_malformed", errorCode(json(assertNotNull(raw("""{"ipcVersion":1,"ipcVersion":1,"requestId":"$id","command":"get-status"}""")))))

        // Exactly 4 MiB is still read and answered; one byte more is refused
        // before anything is copied, like a desktop connection that is closed.
        val limit = 4 * 1024 * 1024
        val padding = limit - """{"ipcVersion":1,"requestId":"$id","command":"get-status","x":""}""".length
        val largest = """{"ipcVersion":1,"requestId":"$id","command":"get-status","x":"${"a".repeat(padding)}"}"""
        assertEquals(limit, largest.length)
        assertEquals("ipc_malformed", errorCode(json(assertNotNull(raw(largest)))))
        val started = System.nanoTime()
        assertNull(NativeBridge.nativeDispatch(ByteArray(limit + 1) { 'a'.code.toByte() }, 5_000))
        assertTrue("refused at once", System.nanoTime() - started < 1_000_000_000L)
        assertNull(NativeBridge.nativeDispatch(ByteArray(0), 1_000))

        // Concurrent callers each receive their own correlated reply.
        val pool = Executors.newFixedThreadPool(16)
        try {
            val ids = List(32) { IdSource.RANDOM.next() }
            val replies = pool.invokeAll(ids.map { requestId -> Callable { raw(plain("get-status", requestId), 2_000) } })
            ids.zip(replies).forEach { (requestId, reply) -> assertEquals(requestId, JsonChecks.string(json(assertNotNull(reply.get()))["requestId"])) }
        } finally {
            pool.shutdownNow()
        }
    }

    @Test
    fun timeoutsLeaveTheJobRunningAndBusyAnswersTheSameClass() {
        val setId = IdSource.RANDOM.next()
        assertEquals(fixture("ack.json", setId), raw(RuntimeHarness.setConfig(RuntimeHarness.document(RuntimeHarness.BLACKHOLE_URL), setId), 30_000))

        // get-server-info runs on the diagnostic executor (one outstanding).
        val started = System.nanoTime()
        assertNull(raw(plain("get-server-info"), 300))
        val waited = (System.nanoTime() - started) / 1_000_000
        assertTrue("the caller's timeout was honoured: $waited ms", waited in 250..2_000)

        // The short control class is not blocked by the abandoned job...
        assertNotNull(raw(plain("get-status"), 400))
        // ...but the diagnostic class still is: the exact desktop busy reply.
        val busyId = IdSource.RANDOM.next()
        assertEquals(fixture("busy-classic.json", busyId), raw(plain("get-info", busyId), 2_000))
    }

    private suspend fun complete(client: phpvpn.android.bridge.ipc.IpcClient, session: WizardSession, operationId: String): OperationResult {
        val deadline = System.nanoTime() + 30_000_000_000L
        while (true) {
            val operation = (client.wizardGetOperation(session, operationId) as IpcOutcome.Success).value
            val code = operation.error?.code ?: ""
            if (operation.operationState == OperationState.FAILED && (code.startsWith("wire_") || code.startsWith("response_") || code == "server_verification_failed")) {
                client.wizardClose(session)
                throw RelayFlake(code)
            }
            if (operation.operationState.terminal) return operation
            assertTrue("operation $operationId did not finish", System.nanoTime() < deadline)
            Thread.sleep(200)
        }
    }

    private fun <T> ok(outcome: IpcOutcome<T>): T {
        assertTrue("expected success: $outcome", outcome is IpcOutcome.Success)
        return (outcome as IpcOutcome.Success).value
    }

    private fun errorCode(reply: JsonObject): String? = JsonChecks.string((reply["error"] as? JsonObject)?.get("code"))

    companion object {
        private const val TAG = "phpvpn-wp04-ipc"

        @JvmStatic
        @AfterClass
        fun stopRuntime() = NativeRuntime.stop()
    }
}

/** Asserts a reply and returns it. */
private fun <T> assertNotNull(value: T?): T {
    org.junit.Assert.assertNotNull("no reply", value)
    return value!!
}
