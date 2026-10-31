package phpvpn.android.bridge.ipc

import kotlinx.serialization.DeserializationStrategy
import kotlinx.serialization.SerializationException
import kotlinx.serialization.SerializationStrategy
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import phpvpn.android.bridge.errors.ErrorCode

/** Result of one IPC v1 exchange. A timeout is never a success. */
sealed interface IpcOutcome<out T> {
    data class Success<T>(val value: T) : IpcOutcome<T>

    /**
     * The service answered with a correlated error envelope. [error] carries the
     * code and the recoverable flag (its description is never displayed);
     * [wizard] is the structured error of the configuration and Wizard commands.
     */
    data class ServiceError(val error: SafeError, val wizard: WizardError? = null) : IpcOutcome<Nothing> {
        val code: String get() = error.code
        val errorCode: ErrorCode? get() = ErrorCode.fromWire(error.code)
        val busy: Boolean get() = error.code == ErrorCode.BUSY.wireName
    }

    /** No reply within the budget or the runtime is stopping: the outcome is unknown. */
    data object TimedOut : IpcOutcome<Nothing>

    /** The reply or the prepared request violated the contract; nothing is applied. */
    data class Rejected(val code: ErrorCode) : IpcOutcome<Nothing>
}

inline fun <T, R> IpcOutcome<T>.map(transform: (T) -> R): IpcOutcome<R> = when (this) {
    is IpcOutcome.Success -> IpcOutcome.Success(transform(value))
    is IpcOutcome.ServiceError -> this
    IpcOutcome.TimedOut -> IpcOutcome.TimedOut
    is IpcOutcome.Rejected -> this
}

fun <T> IpcOutcome<T>.valueOrNull(): T? = (this as? IpcOutcome.Success)?.value

/** Identity of one open Wizard session ("relace" in client.md). */
data class WizardSession(val serviceInstanceId: String, val sessionId: String)

/** `get-server-info`: the validated result and the exact reply for the native ServerInfoModel (WP04). */
data class ServerInfoReply(val result: ServerInfoResult, val rawReply: String)

/**
 * Unclassified reply of `set-config`. The save classification
 * (`classifySaveResponse`, apps/tray/save_response.cpp) stays native and reaches
 * Kotlin through [phpvpn.android.bridge.controller.SaveResponseClassifier].
 * The caller wipes [response].
 */
class RawExchange(
    val requestId: String,
    val response: ByteArray?,
    val writeAttempted: Boolean,
    val completedAtMs: Long,
)

/**
 * Typed IPC v1 client: one suspend function per command, kotlinx.serialization
 * models, the closed-schema checks of [WireValidation] and the tray budgets of
 * [IpcTimeouts].
 *
 * Threading: the client itself is main-safe; it only calls [IpcTransport.dispatch],
 * which the real JNI transport (WP04) runs on its own IO dispatcher.
 *
 * Secrets: request bytes are zeroed right after the transport returns, reply
 * bytes right after parsing. Secret members are [SecretText] in the models;
 * the caller wipes them after use. Nothing here logs.
 */
class IpcClient(
    private val transport: IpcTransport,
    private val ids: IdSource = IdSource.RANDOM,
    private val documentCheck: ConfigDocumentCheck = StructuralConfigDocumentCheck,
    private val clock: MonotonicClock = MonotonicClock.SYSTEM,
) {
    // ------------------------------------------------------------ original commands

    suspend fun getStatus(timeoutMs: Int = IpcCommand.GET_STATUS.timeoutMs): IpcOutcome<StatusSnapshot> =
        statusCommand(IpcCommand.GET_STATUS, timeoutMs)

    /** Idempotent Connect; the reply is the status after acceptance. */
    suspend fun connect(): IpcOutcome<StatusSnapshot> = statusCommand(IpcCommand.CONNECT, IpcCommand.CONNECT.timeoutMs)

    /** Disconnect; the reply is the status once the teardown completed or is under way. */
    suspend fun disconnect(): IpcOutcome<StatusSnapshot> = statusCommand(IpcCommand.DISCONNECT, IpcCommand.DISCONNECT.timeoutMs)

    /** Verified server plaintext `info` object, forwarded without an IPC envelope. */
    suspend fun getInfo(): IpcOutcome<String> {
        val id = ids.next()
        val reply = send(IpcCommand.GET_INFO, PlainRequest.serializer(), PlainRequest(IPC_VERSION, id, IpcCommand.GET_INFO.wireName))
        return reply.decode { root, text ->
            when {
                WireValidation.classicError(root, id) -> serviceError(root)
                root is JsonObject && root["error"] != null -> IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
                root is JsonObject -> IpcOutcome.Success(text)
                else -> IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
            }
        }
    }

    suspend fun getServerInfo(): IpcOutcome<ServerInfoReply> {
        val id = ids.next()
        val reply = send(
            IpcCommand.GET_SERVER_INFO, PlainRequest.serializer(),
            PlainRequest(IPC_VERSION, id, IpcCommand.GET_SERVER_INFO.wireName),
        )
        return reply.decode { root, text ->
            when {
                WireValidation.classicError(root, id) -> serviceError(root)
                JsonChecks.shape(root, "ipcVersion", "requestId", "result") && JsonChecks.int64((root as JsonObject)["ipcVersion"]) == 1L &&
                    JsonChecks.string(root["requestId"]) == id && WireValidation.serverInfo(root["result"]) ->
                    IpcOutcome.Success(ServerInfoReply(decode(ServerInfoResult.serializer(), root["result"]!!), text))
                else -> IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
            }
        }
    }

    /** Stored configuration document, without an envelope. Contains secrets: wipe it. */
    suspend fun getConfig(): IpcOutcome<SecretText> {
        val id = ids.next()
        val reply = send(IpcCommand.GET_CONFIG, PlainRequest.serializer(), PlainRequest(IPC_VERSION, id, IpcCommand.GET_CONFIG.wireName))
        return reply.decode { root, text ->
            when {
                WireValidation.classicError(root, id) -> serviceError(root)
                root is JsonObject && root["error"] != null -> IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
                root is JsonObject -> IpcOutcome.Success(SecretText.of(text))
                else -> IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
            }
        }
    }

    /** Typed `set-config`: acknowledgement or a service error. The [document] stays owned by the caller. */
    suspend fun setConfig(document: SecretText): IpcOutcome<Unit> {
        val exchange = setConfigRaw(document)
        if (!exchange.writeAttempted) return IpcOutcome.Rejected(ErrorCode.IPC_MESSAGE_TOO_LARGE)
        val response = exchange.response ?: return IpcOutcome.TimedOut
        return try {
            Reply(response, exchange.requestId).decode { root, _ ->
                when {
                    WireValidation.acknowledgement(root, exchange.requestId) -> IpcOutcome.Success(Unit)
                    WireValidation.classicError(root, exchange.requestId) -> serviceError(root)
                    else -> IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
                }
            }
        } finally {
            response.fill(0)
        }
    }

    /** `set-config` without classification, for the native save classifier. */
    suspend fun setConfigRaw(document: SecretText, timeoutMs: Int = IpcCommand.SET_CONFIG.timeoutMs): RawExchange {
        val id = ids.next()
        val request = encode(
            SetConfigRequest.serializer(),
            SetConfigRequest(IPC_VERSION, id, IpcCommand.SET_CONFIG.wireName, document),
        ) ?: return RawExchange(id, null, false, clock.nowMs())
        val response = dispatch(request, timeoutMs)
        return RawExchange(id, response, true, clock.nowMs())
    }

    /**
     * Unparsed exchange of a request with only the common triple, for a native
     * consumer that parses the reply itself (the contracts save check).
     */
    suspend fun rawPlain(command: IpcCommand, timeoutMs: Int = command.timeoutMs): RawExchange {
        val id = ids.next()
        val bytes = encode(PlainRequest.serializer(), PlainRequest(IPC_VERSION, id, command.wireName))
            ?: return RawExchange(id, null, false, clock.nowMs())
        return RawExchange(id, dispatch(bytes, timeoutMs), true, clock.nowMs())
    }

    suspend fun getDiagnostics(): IpcOutcome<DiagnosticsSnapshot> {
        val id = ids.next()
        val reply = send(
            IpcCommand.GET_DIAGNOSTICS, PlainRequest.serializer(),
            PlainRequest(IPC_VERSION, id, IpcCommand.GET_DIAGNOSTICS.wireName),
        )
        return reply.decode { root, _ ->
            when {
                WireValidation.diagnostics(root, id) -> IpcOutcome.Success(decode(DiagnosticsEnvelope.serializer(), root!!).diagnostics)
                WireValidation.classicError(root, id) -> serviceError(root)
                else -> IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
            }
        }
    }

    /** Removes the service copy of the Connect diagnostic of exactly this attempt. */
    suspend fun dismissConnectDiagnostic(connectionEpoch: ULong, connectAttemptId: String): IpcOutcome<Unit> {
        if (connectionEpoch == 0uL || !WireFormats.isUuidV4(connectAttemptId)) return IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
        val id = ids.next()
        val reply = send(
            IpcCommand.DISMISS_CONNECT_DIAGNOSTIC, DismissConnectDiagnosticRequest.serializer(),
            DismissConnectDiagnosticRequest(
                IPC_VERSION, id, IpcCommand.DISMISS_CONNECT_DIAGNOSTIC.wireName, connectionEpoch, connectAttemptId,
            ),
        )
        return reply.decode { root, _ ->
            when {
                WireValidation.acknowledgement(root, id) -> IpcOutcome.Success(Unit)
                WireValidation.classicError(root, id) -> serviceError(root)
                else -> IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
            }
        }
    }

    // ------------------------------------------------------------ configuration commands

    suspend fun getConfigState(): IpcOutcome<ConfigStateResult> =
        plainWizard(IpcCommand.GET_CONFIG_STATE, ConfigStateResult.serializer())

    /** Actual document text with availability; wipe the document after use. */
    suspend fun getConfigDocument(): IpcOutcome<ConfigDocumentResult> =
        plainWizard(IpcCommand.GET_CONFIG_DOCUMENT, ConfigDocumentResult.serializer())

    suspend fun getDefaultConfig(): IpcOutcome<DefaultConfigResult> =
        plainWizard(IpcCommand.GET_DEFAULT_CONFIG, DefaultConfigResult.serializer())

    /** Sent only after the user confirmed screen 36a. Never overwrites an existing file. */
    suspend fun createDefaultConfig(): IpcOutcome<CreateDefaultConfigResult> =
        plainWizard(IpcCommand.CREATE_DEFAULT_CONFIG, CreateDefaultConfigResult.serializer())

    // ------------------------------------------------------------ Wizard commands

    suspend fun wizardOpen(): IpcOutcome<WizardView> = plainWizard(IpcCommand.WIZARD_OPEN, WizardView.serializer())

    suspend fun wizardReset(session: WizardSession): IpcOutcome<WizardView> =
        sessionCommand(IpcCommand.WIZARD_RESET, session, WizardView.serializer())

    suspend fun wizardKeepalive(session: WizardSession): IpcOutcome<KeepaliveResult> =
        sessionCommand(IpcCommand.WIZARD_KEEPALIVE, session, KeepaliveResult.serializer())

    suspend fun wizardClose(session: WizardSession, timeoutMs: Int = IpcCommand.WIZARD_CLOSE.timeoutMs): IpcOutcome<CloseResult> =
        sessionCommand(IpcCommand.WIZARD_CLOSE, session, CloseResult.serializer(), timeoutMs)

    suspend fun wizardCheckUrl(
        session: WizardSession,
        clientAttemptId: String,
        attemptSequence: ULong,
        serverUrl: String,
        httpHeaders: String,
    ): IpcOutcome<OperationStarted> {
        if (!validAttempt(clientAttemptId, attemptSequence) || !HttpHeaders.isValid(httpHeaders)) {
            return IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
        }
        val id = ids.next()
        return wizardExchange(
            IpcCommand.WIZARD_CHECK_URL, id, WizardCheckUrlRequest.serializer(),
            WizardCheckUrlRequest(
                IPC_VERSION, id, IpcCommand.WIZARD_CHECK_URL.wireName, session.serviceInstanceId, session.sessionId,
                clientAttemptId, attemptSequence, serverUrl, httpHeaders,
            ),
            OperationStarted.serializer(),
        )
    }

    /** [serverSecret] stays owned by the caller, which wipes it. */
    suspend fun wizardCheckSecret(
        session: WizardSession,
        clientAttemptId: String,
        attemptSequence: ULong,
        serverUrl: String,
        serverSecret: SecretText,
        httpHeaders: String,
    ): IpcOutcome<OperationStarted> {
        if (!validAttempt(clientAttemptId, attemptSequence) || !HttpHeaders.isValid(httpHeaders)) {
            return IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
        }
        val id = ids.next()
        return wizardExchange(
            IpcCommand.WIZARD_CHECK_SECRET, id, WizardCheckSecretRequest.serializer(),
            WizardCheckSecretRequest(
                IPC_VERSION, id, IpcCommand.WIZARD_CHECK_SECRET.wireName, session.serviceInstanceId, session.sessionId,
                clientAttemptId, attemptSequence, serverUrl, httpHeaders, serverSecret,
            ),
            OperationStarted.serializer(),
        )
    }

    suspend fun wizardCheckAccess(
        session: WizardSession,
        clientAttemptId: String,
        attemptSequence: ULong,
        fields: WizardFields,
    ): IpcOutcome<OperationStarted> {
        if (!validAttempt(clientAttemptId, attemptSequence) || !HttpHeaders.isValid(fields.httpHeaders)) {
            return IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
        }
        val id = ids.next()
        return wizardExchange(
            IpcCommand.WIZARD_CHECK_ACCESS, id, WizardCheckAccessRequest.serializer(),
            WizardCheckAccessRequest(
                IPC_VERSION, id, IpcCommand.WIZARD_CHECK_ACCESS.wireName, session.serviceInstanceId, session.sessionId,
                clientAttemptId, attemptSequence, fields,
            ),
            OperationStarted.serializer(),
        )
    }

    suspend fun wizardGetOperation(session: WizardSession, operationId: String): IpcOutcome<OperationResult> =
        operationCommand(IpcCommand.WIZARD_GET_OPERATION, session, operationId, OperationResult.serializer())

    suspend fun wizardCancelOperation(session: WizardSession, operationId: String): IpcOutcome<CancelOperationResult> =
        operationCommand(IpcCommand.WIZARD_CANCEL_OPERATION, session, operationId, CancelOperationResult.serializer())

    suspend fun wizardCancelAttempt(
        session: WizardSession,
        clientAttemptId: String,
        attemptSequence: ULong,
    ): IpcOutcome<CancelAttemptResult> {
        if (!validAttempt(clientAttemptId, attemptSequence)) return IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
        val id = ids.next()
        return wizardExchange(
            IpcCommand.WIZARD_CANCEL_ATTEMPT, id, WizardCancelAttemptRequest.serializer(),
            WizardCancelAttemptRequest(
                IPC_VERSION, id, IpcCommand.WIZARD_CANCEL_ATTEMPT.wireName, session.serviceInstanceId, session.sessionId,
                clientAttemptId, attemptSequence,
            ),
            CancelAttemptResult.serializer(),
        )
    }

    /** Resume after a confirmed Disconnect; the service verifies the state itself. */
    suspend fun wizardResume(session: WizardSession, connectionEpoch: ULong): IpcOutcome<WizardView> {
        val id = ids.next()
        return wizardExchange(
            IpcCommand.WIZARD_RESUME, id, WizardResumeRequest.serializer(),
            WizardResumeRequest(
                IPC_VERSION, id, IpcCommand.WIZARD_RESUME.wireName, session.serviceInstanceId, session.sessionId, connectionEpoch,
            ),
            WizardView.serializer(),
        )
    }

    suspend fun wizardFinish(
        session: WizardSession,
        commitId: String,
        fields: WizardFields,
        createDefaultContracts: Boolean,
    ): IpcOutcome<OperationStarted> {
        if (!WireFormats.isUuidV4(commitId) || !HttpHeaders.isValid(fields.httpHeaders)) {
            return IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
        }
        val id = ids.next()
        return wizardExchange(
            IpcCommand.WIZARD_FINISH, id, WizardFinishRequest.serializer(),
            WizardFinishRequest(
                IPC_VERSION, id, IpcCommand.WIZARD_FINISH.wireName, session.serviceInstanceId, session.sessionId,
                commitId, fields, createDefaultContracts,
            ),
            OperationStarted.serializer(),
        )
    }

    suspend fun wizardGetCommit(session: WizardSession, commitId: String): IpcOutcome<CommitResult> {
        if (!WireFormats.isUuidV4(commitId)) return IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
        val id = ids.next()
        return wizardExchange(
            IpcCommand.WIZARD_GET_COMMIT, id, WizardGetCommitRequest.serializer(),
            WizardGetCommitRequest(
                IPC_VERSION, id, IpcCommand.WIZARD_GET_COMMIT.wireName, session.serviceInstanceId, session.sessionId, commitId,
            ),
            CommitResult.serializer(),
        )
    }

    // ------------------------------------------------------------ plumbing

    private fun validAttempt(attemptId: String, sequence: ULong) = WireFormats.isUuidV4(attemptId) && sequence > 0uL

    private suspend fun statusCommand(command: IpcCommand, timeoutMs: Int): IpcOutcome<StatusSnapshot> {
        val id = ids.next()
        val reply = send(command, PlainRequest.serializer(), PlainRequest(IPC_VERSION, id, command.wireName), timeoutMs)
        return reply.decode { root, _ ->
            when {
                WireValidation.status(root) && JsonChecks.string((root as JsonObject)["requestId"]) == id ->
                    IpcOutcome.Success(decode(StatusEnvelope.serializer(), root).status)
                WireValidation.classicError(root, id) -> serviceError(root)
                else -> IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
            }
        }
    }

    private suspend fun <T> plainWizard(command: IpcCommand, result: DeserializationStrategy<T>): IpcOutcome<T> {
        val id = ids.next()
        return wizardExchange(command, id, PlainRequest.serializer(), PlainRequest(IPC_VERSION, id, command.wireName), result)
    }

    private suspend fun <T> sessionCommand(
        command: IpcCommand,
        session: WizardSession,
        result: DeserializationStrategy<T>,
        timeoutMs: Int = command.timeoutMs,
    ): IpcOutcome<T> {
        val id = ids.next()
        return wizardExchange(
            command, id, WizardSessionRequest.serializer(),
            WizardSessionRequest(IPC_VERSION, id, command.wireName, session.serviceInstanceId, session.sessionId),
            result, timeoutMs,
        )
    }

    private suspend fun <T> operationCommand(
        command: IpcCommand,
        session: WizardSession,
        operationId: String,
        result: DeserializationStrategy<T>,
    ): IpcOutcome<T> {
        if (!WireFormats.isUuidV4(operationId)) return IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
        val id = ids.next()
        return wizardExchange(
            command, id, WizardOperationRequest.serializer(),
            WizardOperationRequest(IPC_VERSION, id, command.wireName, session.serviceInstanceId, session.sessionId, operationId),
            result,
        )
    }

    private suspend fun <R, T> wizardExchange(
        command: IpcCommand,
        id: String,
        requestSerializer: SerializationStrategy<R>,
        request: R,
        result: DeserializationStrategy<T>,
        timeoutMs: Int = command.timeoutMs,
    ): IpcOutcome<T> {
        if (!WireFormats.isUuidV4(id)) return IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
        val reply = send(command, requestSerializer, request, timeoutMs)
        return reply.decode { root, _ ->
            when (val check = WireValidation.wizardReply(command, id, root, documentCheck)) {
                is WizardReplyCheck.Result -> IpcOutcome.Success(decode(result, check.value))
                is WizardReplyCheck.Error -> {
                    val error = decode(WizardError.serializer(), check.error)
                    IpcOutcome.ServiceError(SafeError(error.code, error.description, error.recoverable), error)
                }
                is WizardReplyCheck.Rejected -> IpcOutcome.Rejected(check.code)
            }
        }
    }

    /** Encoded request, or a terminal outcome when the request breaks the contract. */
    private sealed interface Sent {
        class Answer(val reply: Reply) : Sent

        class Done(val outcome: IpcOutcome<Nothing>) : Sent
    }

    private class Reply(val bytes: ByteArray, val requestId: String)

    private suspend fun <R> send(
        command: IpcCommand,
        serializer: SerializationStrategy<R>,
        request: R,
        timeoutMs: Int = command.timeoutMs,
    ): Sent {
        val requestId = (request as? Any)?.let { idOf(it) } ?: ""
        if (!WireFormats.isUuidV4(requestId)) return Sent.Done(IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED))
        val bytes = encode(serializer, request) ?: return Sent.Done(IpcOutcome.Rejected(ErrorCode.IPC_MESSAGE_TOO_LARGE))
        val response = dispatch(bytes, timeoutMs) ?: return Sent.Done(IpcOutcome.TimedOut)
        return Sent.Answer(Reply(response, requestId))
    }

    private fun idOf(request: Any): String? = when (request) {
        is PlainRequest -> request.requestId
        is SetConfigRequest -> request.requestId
        is DismissConnectDiagnosticRequest -> request.requestId
        is WizardSessionRequest -> request.requestId
        is WizardCheckUrlRequest -> request.requestId
        is WizardCheckSecretRequest -> request.requestId
        is WizardCheckAccessRequest -> request.requestId
        is WizardOperationRequest -> request.requestId
        is WizardCancelAttemptRequest -> request.requestId
        is WizardResumeRequest -> request.requestId
        is WizardFinishRequest -> request.requestId
        is WizardGetCommitRequest -> request.requestId
        else -> null
    }

    private fun <R> encode(serializer: SerializationStrategy<R>, request: R): ByteArray? {
        val bytes = IpcJson.encodeToString(serializer, request).toByteArray(Charsets.UTF_8)
        if (bytes.size > IPC_MAX_MESSAGE_BYTES) {
            bytes.fill(0)
            return null
        }
        return bytes
    }

    private suspend fun dispatch(request: ByteArray, timeoutMs: Int): ByteArray? = try {
        transport.dispatch(request, timeoutMs)
    } finally {
        request.fill(0)
    }

    private inline fun <T> Sent.decode(handle: (JsonElement?, String) -> IpcOutcome<T>): IpcOutcome<T> = when (this) {
        is Sent.Done -> outcome
        is Sent.Answer -> reply.decode(handle)
    }

    private inline fun <T> Reply.decode(handle: (JsonElement?, String) -> IpcOutcome<T>): IpcOutcome<T> {
        try {
            if (bytes.size > IPC_MAX_MESSAGE_BYTES) return IpcOutcome.Rejected(ErrorCode.IPC_MESSAGE_TOO_LARGE)
            val text = StrictJson.decodeUtf8(bytes) ?: return IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
            val root = StrictJson.parse(text) ?: return IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
            return try {
                handle(root, text)
            } catch (_: SerializationException) {
                IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
            } catch (_: IllegalArgumentException) {
                IpcOutcome.Rejected(ErrorCode.IPC_MALFORMED)
            }
        } finally {
            bytes.fill(0)
        }
    }

    private fun serviceError(root: JsonElement?): IpcOutcome<Nothing> =
        IpcOutcome.ServiceError(decode(ErrorEnvelope.serializer(), root!!).error)

    private fun <T> decode(deserializer: DeserializationStrategy<T>, element: JsonElement): T =
        IpcJson.decodeFromJsonElement(deserializer, element)
}
