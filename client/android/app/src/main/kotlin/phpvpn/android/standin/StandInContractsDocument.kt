package phpvpn.android.standin

import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.HttpHeaders
import phpvpn.android.bridge.ipc.IPC_MAX_MESSAGE_BYTES
import phpvpn.android.bridge.ipc.IPC_VERSION
import phpvpn.android.bridge.ipc.IpcCommand
import phpvpn.android.bridge.ipc.IpcJson
import phpvpn.android.bridge.ipc.JsonChecks
import phpvpn.android.bridge.ipc.MAX_CONFIG_FILE_BYTES
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.ipc.SetConfigRequest
import phpvpn.android.bridge.ipc.StrictJson
import phpvpn.android.bridge.ipc.WireFormats

/*
 * Kotlin stand-in of core/config/contracts_document.{h,cpp} for the app until
 * WP04 delivers the JNI ContractsModel. It mirrors the row and candidate rules
 * exactly; the configuration validation is the structural subset documented
 * on [StandInContractsDocument.parse], the complete validateConfigValues stays
 * native (the service validates every set-config again in any case).
 */

/** `kMaxContractRows`. */
const val MAX_CONTRACT_ROWS = 252

/** `kMaxContractKeyTextBytes` = `kMaxConfigFileBytes`. */
const val MAX_CONTRACT_KEY_TEXT_BYTES = MAX_CONFIG_FILE_BYTES

private const val MIN_PREFIX = 24
private const val MAX_PREFIX = 29

/** Result of a stand-in operation: a value or the error code of the C++ `Result`. */
sealed interface StandInResult<out T> {
    data class Ok<T>(val value: T) : StandInResult<T>

    data class Failed(val code: ErrorCode) : StandInResult<Nothing>
}

/** `isValidContractKey`: exactly 64 lowercase hexadecimal characters. */
fun isValidContractKey(text: CharArray): Boolean = text.size == 64 && text.all { it in '0'..'9' || it in 'a'..'f' }

/** A canonical client network: first client is network + 2, last is broadcast - 1. */
data class ClientNetwork(val network: Long, val prefix: Int) {
    val firstClient: Long get() = network + 2
    val lastClient: Long get() = (network or ((1L shl (32 - prefix)) - 1)) - 1

    fun isClient(ip: Long): Boolean = ip in firstClient..lastClient
}

/** `checkedContractPeerCount`: the peers without self, checked before any row is allocated. */
fun checkedContractPeerCount(network: ClientNetwork?, localIp: Long?): StandInResult<Int> {
    if (network == null || network.prefix < MIN_PREFIX || network.prefix > MAX_PREFIX) return StandInResult.Failed(ErrorCode.CONFIG_INVALID_CIDR)
    if (localIp == null || !network.isClient(localIp)) return StandInResult.Failed(ErrorCode.CONFIG_INVALID_ADDRESS)
    val count = network.lastClient - network.firstClient
    if (count < 0 || count > MAX_CONTRACT_ROWS) return StandInResult.Failed(ErrorCode.CONFIG_INVALID_CIDR)
    return StandInResult.Ok(count.toInt())
}

/** `checkedContractKeyTextSize`: the new total, or `config_too_large` without overflow. */
fun checkedContractKeyTextSize(total: Long, oldSize: Long, newSize: Long): StandInResult<Long> {
    if (total < 0 || oldSize < 0 || newSize < 0 || total > MAX_CONTRACT_KEY_TEXT_BYTES || oldSize > total ||
        newSize > MAX_CONTRACT_KEY_TEXT_BYTES - (total - oldSize)
    ) return StandInResult.Failed(ErrorCode.CONFIG_TOO_LARGE)
    return StandInResult.Ok(total - oldSize + newSize)
}

/** `validateContractsEnvelopeSize`. */
fun validateContractsEnvelopeSize(encodedBytes: Long): ErrorCode? =
    if (encodedBytes > IPC_MAX_MESSAGE_BYTES) ErrorCode.IPC_MESSAGE_TOO_LARGE else null

/** Measures the encoded set-config request, as `contractsEnvelopeSize` does with the native builder. */
fun interface ContractsEnvelopeSizer {
    fun size(document: String): StandInResult<Long>
}

/** `contractsEnvelopeSize`: the real request with a synthetic UUIDv4 of the fixed encoded length. */
val StandInEnvelopeSizer = ContractsEnvelopeSizer { document ->
    val secret = SecretText.of(document)
    try {
        val request = SetConfigRequest(IPC_VERSION, "12345678-1234-4234-9234-123456789abc", IpcCommand.SET_CONFIG.wireName, secret)
        val bytes = IpcJson.encodeToString(SetConfigRequest.serializer(), request).toByteArray(Charsets.UTF_8)
        val size = bytes.size.toLong()
        bytes.fill(0)
        StandInResult.Ok(size)
    } finally {
        secret.wipe()
    }
}

/** `ContractEdit`: one row of the complete snapshot handed to [StandInContractsDocument.buildCandidate]. */
class ContractEdit(val remoteIp: Long, val enabled: Boolean, val keyText: CharArray)

/** `ContractsCandidate`: one immutable DOM and its serialized text. */
class StandInContractsCandidate internal constructor(val dom: JsonObject, private val textChars: SecretText) {
    /** A copy of the text the caller wipes. */
    fun text(): SecretText = textChars.copy()

    /** Object order is insignificant, types and absent/present remain significant. */
    fun matchesDocument(currentDocument: String): StandInResult<Boolean> =
        when (val parsed = StandInContractsDocument.parse(currentDocument)) {
            is StandInResult.Ok -> StandInResult.Ok(parsed.value.dom == dom)
            is StandInResult.Failed -> parsed
        }

    fun wipe() = textChars.wipe()
}

/**
 * `ContractsDocument`: one validated DOM, the local IP and the network.
 * [dom] keeps the member order of the original text.
 */
class StandInContractsDocument private constructor(
    val dom: JsonObject,
    val localIp: Long,
    val network: ClientNetwork,
) {
    /** Absent and present are distinct. */
    fun existingKey(remoteIp: Long): String? {
        val contracts = dom[CONTRACTS] as? JsonObject ?: return null
        val contract = contracts[WireFormats.formatIpv4(remoteIp)] as? JsonObject ?: return null
        return JsonChecks.string(contract[PAYLOAD_KEY])
    }

    /**
     * Complete row snapshot required, in any order. Duplicates, omissions and
     * foreign or self peers are rejected. Disabled key text is never published.
     */
    fun buildCandidate(rows: List<ContractEdit>, sizer: ContractsEnvelopeSizer?): StandInResult<StandInContractsCandidate> {
        val count = checkedContractPeerCount(network, localIp)
        if (count is StandInResult.Failed) return count
        if (rows.size != (count as StandInResult.Ok).value || sizer == null) return StandInResult.Failed(ErrorCode.INVALID_ARGUMENT)
        val seen = BooleanArray(MAX_CONTRACT_ROWS + 1)
        var total = 0L
        for (row in rows) {
            if (row.remoteIp == localIp || !network.isClient(row.remoteIp)) return StandInResult.Failed(ErrorCode.CONFIG_INVALID_ADDRESS)
            val offset = (row.remoteIp - network.firstClient).toInt()
            if (seen[offset]) return StandInResult.Failed(ErrorCode.INVALID_ARGUMENT)
            seen[offset] = true
            when (val size = checkedContractKeyTextSize(total, 0, utf8Length(row.keyText))) {
                is StandInResult.Ok -> total = size.value
                is StandInResult.Failed -> return size
            }
            if (row.enabled && !isValidContractKey(row.keyText)) return StandInResult.Failed(ErrorCode.CONFIG_INVALID_KEY)
        }
        val anyEnabled = rows.any { it.enabled }
        val contracts = if (dom.containsKey(CONTRACTS) || anyEnabled) {
            JsonObject(
                rows.filter { it.enabled }.sortedBy { it.remoteIp }.associate { row ->
                    WireFormats.formatIpv4(row.remoteIp) to JsonObject(mapOf(PAYLOAD_KEY to JsonPrimitive(String(row.keyText))))
                },
            )
        } else {
            null
        }
        // Replace only the closed-schema contract object, preserving every other
        // member, its type, its position and the presence of the object itself.
        val members = LinkedHashMap<String, JsonElement>()
        for ((name, value) in dom) members[name] = if (name == CONTRACTS && contracts != null) contracts else value
        if (contracts != null && !dom.containsKey(CONTRACTS)) members[CONTRACTS] = contracts
        val candidate = JsonObject(members)
        val text = IpcJson.encodeToString(JsonElement.serializer(), candidate)
        if (WireFormats.utf8Length(text) > MAX_CONFIG_FILE_BYTES) return StandInResult.Failed(ErrorCode.CONFIG_TOO_LARGE)
        val validated = parse(text)
        if (validated is StandInResult.Failed) return validated
        when (val size = sizer.size(text)) {
            is StandInResult.Failed -> return size
            is StandInResult.Ok -> validateContractsEnvelopeSize(size.value)?.let { return StandInResult.Failed(it) }
        }
        return StandInResult.Ok(StandInContractsCandidate(candidate, SecretText.of(text)))
    }

    companion object {
        const val CONTRACTS = "communicationContracts"
        const val PAYLOAD_KEY = "payloadKey"
        private const val MASTER = "masterSetup"

        /**
         * Structural subset of `validateConfigValues` (config_validator.cpp):
         * strict JSON within 512 KiB, the `masterSetup` members a complete
         * configuration needs (URL, secret, header grammar, canonical /24 to
         * /29 CIDR, a client virtual IP, a 64-hex access key, so a repairable
         * document is refused) and closed `communicationContracts` entries
         * (client peers other than self, exactly `payloadKey` with a valid
         * key). Runtime limits, TLS and the Wizard metadata are not checked
         * here; the native model of WP04 runs the full validator.
         */
        fun parse(text: String): StandInResult<StandInContractsDocument> {
            if (WireFormats.utf8Length(text) > MAX_CONFIG_FILE_BYTES) return StandInResult.Failed(ErrorCode.CONFIG_TOO_LARGE)
            val root = StrictJson.parse(text) as? JsonObject ?: return StandInResult.Failed(ErrorCode.CONFIG_INVALID_JSON)
            val setup = root[MASTER] as? JsonObject ?: return StandInResult.Failed(ErrorCode.CONFIG_INVALID_VALUE)
            val url = JsonChecks.string(setup["serverUrl"]) ?: return StandInResult.Failed(ErrorCode.CONFIG_INVALID_URL)
            if (!url.startsWith("http://") && !url.startsWith("https://")) return StandInResult.Failed(ErrorCode.CONFIG_INVALID_URL)
            if (JsonChecks.string(setup["serverSecret"]) == null) return StandInResult.Failed(ErrorCode.CONFIG_INVALID_VALUE)
            val headers = JsonChecks.string(setup["httpHeaders"])
            if (headers == null || !HttpHeaders.isValid(headers)) return StandInResult.Failed(ErrorCode.CONFIG_INVALID_VALUE)
            val cidr = parseNetwork(JsonChecks.string(setup["vpnCidr"])) ?: return StandInResult.Failed(ErrorCode.CONFIG_INVALID_CIDR)
            val local = WireFormats.parseIpv4(JsonChecks.string(setup["virtualIp"]))
            val count = checkedContractPeerCount(cidr, local)
            if (count is StandInResult.Failed) return count
            if (!WireFormats.isLowercaseHex64(JsonChecks.string(setup["accessKey"]))) return StandInResult.Failed(ErrorCode.CONFIG_INVALID_KEY)
            if (root.containsKey(CONTRACTS)) {
                val contracts = root[CONTRACTS] as? JsonObject ?: return StandInResult.Failed(ErrorCode.CONFIG_INVALID_VALUE)
                for ((peerText, contract) in contracts) {
                    val peer = WireFormats.parseIpv4(peerText)
                    if (peer == null || peer == local || !cidr.isClient(peer)) return StandInResult.Failed(ErrorCode.CONFIG_INVALID_ADDRESS)
                    val entry = contract as? JsonObject
                    if (entry == null || entry.size != 1) return StandInResult.Failed(ErrorCode.CONFIG_INVALID_VALUE)
                    if (!WireFormats.isLowercaseHex64(JsonChecks.string(entry[PAYLOAD_KEY]))) return StandInResult.Failed(ErrorCode.CONFIG_INVALID_KEY)
                }
            }
            return StandInResult.Ok(StandInContractsDocument(root, local!!, cidr))
        }

        /** Any IPv4 network with a prefix; the /24 to /29 range is checked by [checkedContractPeerCount]. */
        private fun parseNetwork(text: String?): ClientNetwork? {
            if (text == null) return null
            val slash = text.indexOf('/')
            if (slash < 0) return null
            val network = WireFormats.parseIpv4(text.substring(0, slash)) ?: return null
            val length = text.substring(slash + 1)
            if (length.isEmpty() || length.length > 2 || (length.length > 1 && length[0] == '0') || !length.all { it in '0'..'9' }) return null
            val prefix = length.toInt()
            if (prefix > 32) return null
            val mask = if (prefix == 0) 0L else (0xFFFFFFFFL shl (32 - prefix)) and 0xFFFFFFFFL
            if ((network and mask) != network) return null
            return ClientNetwork(network, prefix)
        }
    }
}

/** UTF-8 length of UTF-16 text; an unpaired surrogate counts as the 3-byte replacement it cannot be. */
internal fun utf8Length(chars: CharArray): Long {
    var bytes = 0L
    var i = 0
    while (i < chars.size) {
        val c = chars[i]
        bytes += when {
            c.code < 0x80 -> 1
            c.code < 0x800 -> 2
            Character.isHighSurrogate(c) && i + 1 < chars.size && Character.isLowSurrogate(chars[i + 1]) -> {
                i++
                4
            }
            else -> 3
        }
        i++
    }
    return bytes
}

/** Strict UTF-16: every surrogate is part of a pair (the counterpart of `isStrictUtf8`). */
internal fun isWellFormedUtf16(chars: CharArray): Boolean {
    var i = 0
    while (i < chars.size) {
        val c = chars[i]
        if (Character.isHighSurrogate(c)) {
            if (i + 1 >= chars.size || !Character.isLowSurrogate(chars[i + 1])) return false
            i += 2
            continue
        }
        if (Character.isLowSurrogate(c)) return false
        i++
    }
    return true
}
