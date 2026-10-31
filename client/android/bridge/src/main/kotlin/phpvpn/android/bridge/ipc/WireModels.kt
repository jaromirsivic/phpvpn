package phpvpn.android.bridge.ipc

import kotlinx.serialization.DeserializationStrategy
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonContentPolymorphicSerializer
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject

/*
 * kotlinx.serialization models of the IPC v1 contract, client.md "Veřejné lokální
 * IPC" and "Rozšíření IPC v1", member for member as the native builders write
 * them (core/ipc/ipc_protocol.cpp, core/ipc/wizard_ipc.cpp). Every member is
 * required; `null` is a value, never an omission. The only optional members are
 * `httpDiagnostic` (status and failed url/secret operations) and the Wizard
 * error's `serverUrl`/`configPath`; they default to null and are omitted when
 * absent, while an explicit null is rejected by [WireValidation].
 *
 * Replies are always validated by [WireValidation] on the strict tree of
 * [StrictJson] before they are decoded into these classes.
 */

/** Encoder/decoder of the wire models: closed schemas, explicit nulls. */
val IpcJson: Json = Json {
    encodeDefaults = false
    explicitNulls = true
    ignoreUnknownKeys = false
    isLenient = false
    coerceInputValues = false
    allowSpecialFloatingPointValues = false
}

const val IPC_VERSION = 1
const val IPC_MAX_MESSAGE_BYTES = 4 * 1024 * 1024
const val MAX_CONFIG_FILE_BYTES = 524_288L
const val MAX_HTTP_TIMEOUT_SECONDS = 2_147_483L

// ---------------------------------------------------------------- enumerations

@Serializable
enum class ConnectionState {
    @SerialName("disconnected") DISCONNECTED,
    @SerialName("connecting") CONNECTING,
    @SerialName("connected") CONNECTED,
    @SerialName("recovering") RECOVERING,
    @SerialName("error") ERROR,
    ;

    val wireName: String get() = name.lowercase()
}

@Serializable
enum class ConnectionPhase(val wireName: String) {
    @SerialName("idle") IDLE("idle"),
    @SerialName("health") HEALTH("health"),
    @SerialName("info") INFO("info"),
    @SerialName("access-key-check") ACCESS_KEY_CHECK("access-key-check"),
    @SerialName("communication-contracts-check") COMMUNICATION_CONTRACTS_CHECK("communication-contracts-check"),
    @SerialName("starting-tunnel") STARTING_TUNNEL("starting-tunnel"),
    @SerialName("running") RUNNING("running"),
    @SerialName("reconnecting") RECONNECTING("reconnecting"),
    @SerialName("stopping") STOPPING("stopping"),
}

@Serializable
enum class Tristate {
    @SerialName("unknown") UNKNOWN,
    @SerialName("yes") YES,
    @SerialName("no") NO,
}

@Serializable
enum class CheckpointResult {
    @SerialName("pending") PENDING,
    @SerialName("success") SUCCESS,
    @SerialName("failed") FAILED,
}

@Serializable
enum class HttpAction {
    @SerialName("health") HEALTH,
    @SerialName("info") INFO,
}

/** `winhttp` means "captured by the platform transport" on every platform. */
@Serializable
enum class RequestHeadersSource {
    @SerialName("winhttp") WINHTTP,
    @SerialName("merged") MERGED,
    @SerialName("application") APPLICATION,
}

@Serializable
enum class ResponseHeadersSource {
    @SerialName("winhttp") WINHTTP,
    @SerialName("available") AVAILABLE,
    @SerialName("unavailable") UNAVAILABLE,
}

@Serializable
enum class ConfigAvailability(val wireName: String) {
    @SerialName("missing") MISSING("missing"),
    @SerialName("unreadable") UNREADABLE("unreadable"),
    @SerialName("encoding_error") ENCODING_ERROR("encoding_error"),
    @SerialName("too_large") TOO_LARGE("too_large"),
    @SerialName("invalid") INVALID("invalid"),
    @SerialName("repairable") REPAIRABLE("repairable"),
    @SerialName("valid") VALID("valid"),
    ;

    /** The document text can be read and edited (valid, repairable, invalid). */
    val readable: Boolean get() = this == VALID || this == REPAIRABLE || this == INVALID
}

@Serializable
enum class WizardMode {
    @SerialName("create") CREATE,
    @SerialName("edit") EDIT,
    @SerialName("repair") REPAIR,
    @SerialName("reset") RESET,
}

@Serializable
enum class OperationKind {
    @SerialName("url") URL,
    @SerialName("secret") SECRET,
    @SerialName("access") ACCESS,
    @SerialName("finish") FINISH,
}

@Serializable
enum class OperationState {
    @SerialName("pending") PENDING,
    @SerialName("running") RUNNING,
    @SerialName("cancelling") CANCELLING,
    @SerialName("succeeded") SUCCEEDED,
    @SerialName("failed") FAILED,
    @SerialName("cancelled") CANCELLED,
    @SerialName("interrupted") INTERRUPTED,
    ;

    val terminal: Boolean get() = this == SUCCEEDED || this == FAILED || this == CANCELLED || this == INTERRUPTED
}

@Serializable
enum class AttemptState {
    @SerialName("cancelling") CANCELLING,
    @SerialName("cancelled") CANCELLED,
    @SerialName("closed") CLOSED,
}

@Serializable
enum class CommitState {
    @SerialName("pending") PENDING,
    @SerialName("confirmed") CONFIRMED,
    @SerialName("failed") FAILED,
    @SerialName("unknown") UNKNOWN,
}

// ---------------------------------------------------------------- shared objects

/** Safe error of the original envelope and of `get-status.lastError`. */
@Serializable
data class SafeError(val code: String, val description: String, val recoverable: Boolean)

/**
 * One bounded HTTP message of `http-octets-v1`. Every original octet is one
 * character U+0000..U+00FF of [prefix] and [suffix]; [octets] restores them.
 */
@Serializable
data class HttpDiagnosticMessage(
    val originalSizeBytes: ULong,
    val prefix: String,
    val separator: String,
    val suffix: String,
) {
    val truncated: Boolean get() = originalSizeBytes > HTTP_DIAGNOSTIC_LIMIT.toULong()

    fun prefixOctets(): ByteArray = ByteArray(prefix.length) { prefix[it].code.toByte() }

    fun suffixOctets(): ByteArray = ByteArray(suffix.length) { suffix[it].code.toByte() }

    companion object {
        const val HTTP_DIAGNOSTIC_LIMIT = 65_536
        const val HTTP_DIAGNOSTIC_PREFIX = 64_512
        const val HTTP_DIAGNOSTIC_TAIL = 1_024
        const val SEPARATOR = "\r\n...\r\n...\r\n...\r\n...\r\n"
    }
}

@Serializable
data class HttpDiagnostic(
    val format: String,
    val action: HttpAction,
    val responseComplete: Boolean,
    val requestHeadersSource: RequestHeadersSource,
    val responseHeadersSource: ResponseHeadersSource,
    val request: HttpDiagnosticMessage,
    val response: HttpDiagnosticMessage,
) {
    companion object {
        const val FORMAT = "http-octets-v1"
    }
}

// ---------------------------------------------------------------- original commands

/** `get-status`, also the reply of `connect` and `disconnect`. 26 members, 27 with httpDiagnostic. */
@Serializable
data class StatusSnapshot(
    val connectionEpoch: ULong,
    val connectAttemptId: String?,
    val httpDiagnostic: HttpDiagnostic? = null,
    val state: ConnectionState,
    val phase: ConnectionPhase,
    val stateChangedAt: String,
    val connectionStartedAt: String?,
    val serverUrlCheck: CheckpointResult,
    val serverSecretCheck: CheckpointResult,
    val accessKeyCheck: CheckpointResult,
    val communicationContractsCheck: CheckpointResult,
    val serverReachable: Tristate,
    val serverVerified: Tristate,
    val tunnelReady: Tristate,
    val lastError: SafeError?,
    val dataPollsTotal: ULong,
    val dataPollsEmpty: ULong,
    val dataPollsWithPayload: ULong,
    val serializedDataRequestBodyBytes: ULong,
    val serializedDataResponseBodyBytes: ULong,
    val ipv4PacketsSent: ULong,
    val ipv4PacketsReceived: ULong,
    val ipv4BytesSent: ULong,
    val ipv4BytesReceived: ULong,
    val recoveryCount: ULong,
    val clientVersion: String,
    val ipcVersion: Int,
)

@Serializable
data class StatusEnvelope(val ipcVersion: Int, val requestId: String, val status: StatusSnapshot)

@Serializable
data class ErrorEnvelope(val ipcVersion: Int, val requestId: String, val error: SafeError)

/** `{"ipcVersion":1,"requestId":"...","result":"ok"}` of set-config and dismiss-connect-diagnostic. */
@Serializable
data class Acknowledgement(val ipcVersion: Int, val requestId: String, val result: String)

@Serializable
data class QueueDiagnostics(
    val itemCount: ULong,
    val occupiedBytes: Long,
    val limitBytes: Long,
    val peerQueueCount: ULong,
    val activeSubBucketCount: ULong,
    val inFlightBytes: Long,
    val queuedItemCount: ULong,
)

@Serializable
data class DiagnosticsCounters(
    val dataPollsTotal: ULong,
    val dataPollsEmpty: ULong,
    val dataPollsWithPayload: ULong,
    val serializedDataRequestBodyBytes: ULong,
    val serializedDataResponseBodyBytes: ULong,
    val ipv4PacketsSent: ULong,
    val ipv4PacketsReceived: ULong,
    val ipv4BytesSent: ULong,
    val ipv4BytesReceived: ULong,
    val packetsDropped: ULong,
    val batchesRejected: ULong,
    val responsesRejected: ULong,
    val peakResponseCiphertextBytes: ULong,
    val queueExpiredPackets: ULong,
    val outboundQueueExpiredPackets: ULong,
    val inboundQueueExpiredPackets: ULong,
)

@Serializable
data class DiagnosticsSnapshot(
    val state: ConnectionState,
    val phase: ConnectionPhase,
    val clientVersion: String,
    val serviceUptimeSeconds: Long,
    val configurationValid: Boolean,
    val configurationWarnings: List<String>,
    val lastHealthVerified: Tristate,
    val lastInfoVerified: Tristate,
    val lastDataVerified: Tristate,
    val lastRecoveryAt: String,
    val recoveryCount: ULong,
    val lastSoftRecoveryAt: String,
    val softRecoveryCount: ULong,
    val tunnelReady: Tristate,
    val outboundQueue: QueueDiagnostics,
    val inboundQueue: QueueDiagnostics,
    val counters: DiagnosticsCounters,
    val lastError: SafeError?,
)

@Serializable
data class DiagnosticsEnvelope(val ipcVersion: Int, val requestId: String, val diagnostics: DiagnosticsSnapshot)

@Serializable
data class MaskedContract(val peer: String, val hash: String)

/** `get-server-info` result: the verified plaintext as received and the local masked contracts. */
@Serializable
data class ServerInfoResult(
    val infoPlaintext: String,
    val localAddress: String,
    val localMaskedCommunicationContracts: List<MaskedContract>,
)

// ---------------------------------------------------------------- Wizard and configuration

/** Structured error of the configuration and Wizard commands (6 members, 8 with path/URL). */
@Serializable
data class WizardError(
    val code: String,
    val description: String,
    val recoverable: Boolean,
    val field: String?,
    val httpStatus: Int?,
    val suggestedAction: String,
    val serverUrl: String? = null,
    val configPath: String? = null,
)

@Serializable
data class WizardFields(
    val serverUrl: String,
    val serverSecret: SecretText,
    val virtualIp: String,
    val accessKey: SecretText,
    val httpHeaders: String,
    val useDefaultServerSecretInWizard: Boolean,
    val autoDetectIpAddressInWizard: Boolean,
    val useDefaultAccessKeyInWizard: Boolean,
) {
    fun wipe() = wipeAll(serverSecret, accessKey)

    /** Independent copy whose secrets have their own lifetime. */
    fun deepCopy(): WizardFields = WizardFields(
        serverUrl, serverSecret.copy(), virtualIp, accessKey.copy(), httpHeaders,
        useDefaultServerSecretInWizard, autoDetectIpAddressInWizard, useDefaultAccessKeyInWizard,
    )
}

@Serializable
data class WizardContext(
    val httpHeaders: String,
    val vpnCidr: String,
    val trustAnyTlsCertificate: Boolean,
    val maxOutboundQueueBytes: Long,
    val maxInboundQueueBytes: Long,
    val minOutboundPerPeerQueueBytes: Long,
    val minInboundPerPeerQueueBytes: Long,
    val maxQueuedPacketAgeMs: Long,
    val maxResponseBodyBytes: Long,
    val maxNdjsonLineBytes: Long,
    val maxDataRecordsPerResponse: Long,
    val responseTimeoutSeconds: Long,
    val timeSyncIntervalSeconds: Long,
)

/** Result of wizard-open, wizard-reset and wizard-resume. */
@Serializable
data class WizardView(
    val sessionId: String,
    val serviceInstanceId: String,
    val revisionToken: String?,
    val availability: ConfigAvailability,
    val mode: WizardMode,
    val fields: WizardFields?,
    val context: WizardContext?,
    val contractCount: ULong,
    val existingDefaultContractPeers: List<String>,
    val connectionEpoch: ULong,
    val vpnState: ConnectionState,
    val vpnPhase: ConnectionPhase,
) {
    val offline: Boolean get() = vpnState == ConnectionState.DISCONNECTED && vpnPhase == ConnectionPhase.IDLE
}

@Serializable
data class KeepaliveResult(val alive: Boolean, val leaseDurationMs: Long)

/** Acknowledgement of a started check or Finish: `operationState` is always "pending". */
@Serializable
data class OperationStarted(val operationId: String, val operationState: OperationState)

@Serializable(with = OperationPayloadSerializer::class)
sealed interface OperationPayload

@Serializable
data class UrlPayload(val verified: Boolean) : OperationPayload

@Serializable
data class SecretPayload(val verified: Boolean, val suggestedIp: String?) : OperationPayload

@Serializable
data class AccessPayload(val verified: Boolean, val discardedRecordCount: ULong) : OperationPayload

/** Finish receipt, also `wizard-get-commit.receipt`. */
@Serializable
data class Receipt(val commitId: String, val revisionToken: String, val commitState: String) : OperationPayload

/** The payload shape is closed per kind; the members select the class. */
object OperationPayloadSerializer : JsonContentPolymorphicSerializer<OperationPayload>(OperationPayload::class) {
    override fun selectDeserializer(element: JsonElement): DeserializationStrategy<OperationPayload> {
        val members = (element as? JsonObject)?.keys ?: emptySet()
        return when {
            "commitId" in members -> Receipt.serializer()
            "suggestedIp" in members -> SecretPayload.serializer()
            "discardedRecordCount" in members -> AccessPayload.serializer()
            else -> UrlPayload.serializer()
        }
    }
}

/** Typed OperationResult of wizard-get-operation. */
@Serializable
data class OperationResult(
    val operationId: String,
    val kind: OperationKind,
    val operationState: OperationState,
    val connectionEpoch: ULong,
    val clientAttemptId: String?,
    val attemptSequence: ULong?,
    val error: WizardError?,
    val payload: OperationPayload?,
    val httpDiagnostic: HttpDiagnostic? = null,
)

@Serializable
data class CancelOperationResult(val operationId: String, val operationState: OperationState)

@Serializable
data class CancelAttemptResult(val attemptSequence: ULong, val attemptState: AttemptState)

@Serializable
data class CommitResult(val commitState: CommitState, val receipt: Receipt?, val error: WizardError?)

@Serializable
data class CloseResult(val closed: Boolean)

@Serializable
data class ConfigStateResult(
    val availability: ConfigAvailability,
    val reasonCode: String?,
    val configPath: String,
    val revisionToken: String?,
    val serviceInstanceId: String,
    val connectionEpoch: ULong,
    val vpnState: ConnectionState,
    val vpnPhase: ConnectionPhase,
)

/** `get-config-document`: the actual text of a readable document, null otherwise. */
@Serializable
data class ConfigDocumentResult(
    val configPath: String,
    val availability: ConfigAvailability,
    val reasonCode: String?,
    val document: SecretText?,
    val revisionToken: String?,
) {
    fun wipe() = document?.wipe()
}

@Serializable
data class DefaultConfigResult(val document: SecretText)

@Serializable
data class CreateDefaultConfigResult(
    val created: Boolean,
    val configPath: String,
    val availability: ConfigAvailability,
    val document: SecretText,
    val revisionToken: String,
)

// ---------------------------------------------------------------- requests

/** Requests with only the common triple, and the original seven commands. */
@Serializable
data class PlainRequest(val ipcVersion: Int, val requestId: String, val command: String)

@Serializable
data class SetConfigRequest(val ipcVersion: Int, val requestId: String, val command: String, val config: SecretText)

@Serializable
data class DismissConnectDiagnosticRequest(
    val ipcVersion: Int,
    val requestId: String,
    val command: String,
    val connectionEpoch: ULong,
    val connectAttemptId: String,
)

/** wizard-reset, wizard-keepalive, wizard-close. */
@Serializable
data class WizardSessionRequest(
    val ipcVersion: Int,
    val requestId: String,
    val command: String,
    val serviceInstanceId: String,
    val sessionId: String,
)

@Serializable
data class WizardCheckUrlRequest(
    val ipcVersion: Int,
    val requestId: String,
    val command: String,
    val serviceInstanceId: String,
    val sessionId: String,
    val clientAttemptId: String,
    val attemptSequence: ULong,
    val serverUrl: String,
    val httpHeaders: String,
)

@Serializable
data class WizardCheckSecretRequest(
    val ipcVersion: Int,
    val requestId: String,
    val command: String,
    val serviceInstanceId: String,
    val sessionId: String,
    val clientAttemptId: String,
    val attemptSequence: ULong,
    val serverUrl: String,
    val httpHeaders: String,
    val serverSecret: SecretText,
)

@Serializable
data class WizardCheckAccessRequest(
    val ipcVersion: Int,
    val requestId: String,
    val command: String,
    val serviceInstanceId: String,
    val sessionId: String,
    val clientAttemptId: String,
    val attemptSequence: ULong,
    val fields: WizardFields,
)

/** wizard-get-operation and wizard-cancel-operation. */
@Serializable
data class WizardOperationRequest(
    val ipcVersion: Int,
    val requestId: String,
    val command: String,
    val serviceInstanceId: String,
    val sessionId: String,
    val operationId: String,
)

@Serializable
data class WizardCancelAttemptRequest(
    val ipcVersion: Int,
    val requestId: String,
    val command: String,
    val serviceInstanceId: String,
    val sessionId: String,
    val clientAttemptId: String,
    val attemptSequence: ULong,
)

@Serializable
data class WizardResumeRequest(
    val ipcVersion: Int,
    val requestId: String,
    val command: String,
    val serviceInstanceId: String,
    val sessionId: String,
    val connectionEpoch: ULong,
)

@Serializable
data class WizardFinishRequest(
    val ipcVersion: Int,
    val requestId: String,
    val command: String,
    val serviceInstanceId: String,
    val sessionId: String,
    val commitId: String,
    val fields: WizardFields,
    val createDefaultContracts: Boolean,
)

@Serializable
data class WizardGetCommitRequest(
    val ipcVersion: Int,
    val requestId: String,
    val command: String,
    val serviceInstanceId: String,
    val sessionId: String,
    val commitId: String,
)
