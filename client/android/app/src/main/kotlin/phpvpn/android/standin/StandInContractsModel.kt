package phpvpn.android.standin

import java.security.SecureRandom
import phpvpn.android.bridge.controller.ContractColumn
import phpvpn.android.bridge.controller.ContractFocus
import phpvpn.android.bridge.controller.ContractRowView
import phpvpn.android.bridge.controller.ContractTargetHandle
import phpvpn.android.bridge.controller.ContractsModelHandle
import phpvpn.android.bridge.errors.ErrorCode
import phpvpn.android.bridge.ipc.SecretText
import phpvpn.android.bridge.ipc.WireFormats

/** `CryptoProvider::randomBytes` of the stand-in; false is a provider failure. */
fun interface RandomSource {
    fun fill(bytes: ByteArray): Boolean

    companion object {
        val SECURE: RandomSource = RandomSource { bytes ->
            SecureRandom().nextBytes(bytes)
            true
        }
    }
}

/**
 * Kotlin stand-in of `ContractsModel` (core/presentation/contracts_model.cpp)
 * behind [ContractsModelHandle], so WP04 can swap in the JNI handle without
 * touching the screens. Rows are every client address of `vpnCidr` except the
 * local IP, in numeric order. The draft is kept per target IP for all rows,
 * independent of any page. Single owner, UI thread.
 */
class StandInContractsModel private constructor(
    private var document: StandInContractsDocument?,
    override val configPath: String,
    override val revisionToken: String,
) : ContractsModelHandle {
    private class Row(val remoteIp: Long, val text: String) {
        var enabled = false
        var originalEnabled = false
        var keyValid = true
        var key = CharArray(0)
        var originalKey = CharArray(0)

        fun dirty(): Boolean = enabled != originalEnabled || (enabled && !key.contentEquals(originalKey))

        fun keyErrorVisible(): Boolean = !keyValid && (enabled || key.isNotEmpty())

        fun wipe() {
            key.fill('\u0000')
            originalKey.fill('\u0000')
        }
    }

    /** `ContractTarget`: immutable values captured when a child opens. */
    private class Target(
        override val remoteIp: String,
        override val contractId: String,
        val ip: Long,
        val identity: Any,
        val generation: Long,
    ) : ContractTargetHandle {
        override fun toString(): String = "ContractTarget($contractId)"
    }

    private var rows: MutableList<Row> = mutableListOf()
    private var identity: Any? = Any()
    private var generation = 1L
    private var keyBytes = 0L
    private var isClosed = false
    private var currentFocus: ContractFocus? = null

    /** `ContractsModel::candidate`: the frozen candidate between prepare and resume. */
    var candidate: StandInContractsCandidate? = null
        private set

    override val localIp: String get() = document?.let { WireFormats.formatIpv4(it.localIp) } ?: ""
    override val dirty: Boolean get() = rows.any { it.dirty() }
    override val blockingErrors: Int get() = rows.count { it.enabled && !it.keyValid }
    override val firstBlockingFocus: ContractFocus?
        get() = rows.firstOrNull { it.enabled && !it.keyValid }?.let { ContractFocus(it.text, ContractColumn.KEY) }
    override val keyTextBytes: Long get() = keyBytes
    override val canPrepareSave: Boolean get() = !isClosed && candidate == null && blockingErrors == 0
    override val frozen: Boolean get() = candidate != null
    override val closed: Boolean get() = isClosed
    override val focus: ContractFocus? get() = currentFocus
    override val saveGeneration: Long get() = generation

    override fun rows(): List<ContractRowView> = rows.map(::view)

    override fun row(remoteIp: String): ContractRowView? = find(remoteIp)?.let(::view)

    /** Lightweight row state without key copies, for the list screen. */
    fun rowStates(): List<RowState> = rows.map { RowState(it.text, it.enabled, it.keyValid, it.key.isNotEmpty(), it.dirty()) }

    data class RowState(val remoteIp: String, val enabled: Boolean, val keyValid: Boolean, val hasKey: Boolean, val dirty: Boolean)

    override fun setEnabled(remoteIp: String, enabled: Boolean): ErrorCode? {
        val row = editable(remoteIp) ?: return ErrorCode.INVALID_ARGUMENT
        row.enabled = enabled
        return null
    }

    override fun setKey(remoteIp: String, key: SecretText): ErrorCode? = key.read { setKeyChars(remoteIp, it) }

    private fun setKeyChars(remoteIp: String, key: CharArray): ErrorCode? {
        val row = editable(remoteIp) ?: return ErrorCode.INVALID_ARGUMENT
        val size = checkedContractKeyTextSize(keyBytes, utf8Length(row.key), utf8Length(key))
        if (size is StandInResult.Failed) return size.code
        if (!isWellFormedUtf16(key)) return ErrorCode.CONFIG_ENCODING_ERROR
        // Copy first, then replace: the input may alias this row's own key.
        val replacement = key.copyOf()
        row.key.fill('\u0000')
        row.key = replacement
        row.keyValid = isValidContractKey(replacement)
        keyBytes = (size as StandInResult.Ok).value
        return null
    }

    override fun setFocus(focus: ContractFocus?): ErrorCode? {
        if (isClosed || (focus != null && find(focus.remoteIp) == null)) return ErrorCode.INVALID_ARGUMENT
        currentFocus = focus
        return null
    }

    override fun target(remoteIp: String): ContractTargetHandle? {
        if (isClosed || frozen) return null
        val row = find(remoteIp) ?: return null
        val local = document?.localIp ?: return null
        val first = minOf(local, row.remoteIp)
        val second = maxOf(local, row.remoteIp)
        return Target(row.text, "${WireFormats.formatIpv4(first)}_${WireFormats.formatIpv4(second)}", row.remoteIp, identity ?: return null, generation)
    }

    /** `confirmRandom` with the injected [random] source (32 bytes, lowercase hex). */
    fun confirmRandom(target: ContractTargetHandle, confirmed: Boolean, random: RandomSource): ErrorCode? {
        if (!accepts(target)) return ErrorCode.INVALID_ARGUMENT
        if (!confirmed) return null
        val bytes = ByteArray(32)
        val hex = CharArray(64)
        try {
            if (!random.fill(bytes)) return ErrorCode.CRYPTO_FAILED
            for (i in bytes.indices) {
                val value = bytes[i].toInt() and 0xff
                hex[2 * i] = HEX[value shr 4]
                hex[2 * i + 1] = HEX[value and 0xf]
            }
            return applyKeyChars(target, hex)
        } finally {
            bytes.fill(0)
            hex.fill('\u0000')
        }
    }

    override fun confirmRandom(target: ContractTargetHandle, confirmed: Boolean): ErrorCode? =
        confirmRandom(target, confirmed, RandomSource.SECURE)

    override fun applyPasswordKey(target: ContractTargetHandle, key: SecretText): ErrorCode? = key.read { applyKeyChars(target, it) }

    private fun applyKeyChars(target: ContractTargetHandle, key: CharArray): ErrorCode? {
        if (!accepts(target)) return ErrorCode.INVALID_ARGUMENT
        if (!isValidContractKey(key)) return ErrorCode.CONFIG_INVALID_KEY
        return setKeyChars(target.remoteIp, key)
    }

    /**
     * `prepareSave`: a failure keeps the draft and the generation; success
     * freezes editing, retires existing child targets and keeps one candidate.
     */
    fun prepareSave(sizer: ContractsEnvelopeSizer?): StandInResult<StandInContractsCandidate> {
        if (!canPrepareSave) return StandInResult.Failed(if (blockingErrors > 0) ErrorCode.CONFIG_INVALID_KEY else ErrorCode.INVALID_ARGUMENT)
        if (generation == Long.MAX_VALUE) return StandInResult.Failed(ErrorCode.INTERNAL)
        val doc = document ?: return StandInResult.Failed(ErrorCode.INVALID_ARGUMENT)
        val built = doc.buildCandidate(rows.map { ContractEdit(it.remoteIp, it.enabled, it.key) }, sizer)
        if (built is StandInResult.Ok) {
            candidate = built.value
            generation++
        }
        return built
    }

    override fun resumeEditing(saveGeneration: Long): Boolean {
        if (isClosed || candidate == null || saveGeneration != generation) return false
        candidate?.wipe()
        candidate = null
        return true
    }

    /** `close`: the caller has already handled the dirty or unknown confirmation. */
    fun close() {
        isClosed = true
        identity = null
        candidate?.wipe()
        candidate = null
        rows.forEach { it.wipe() }
        rows = mutableListOf()
        document = null
        currentFocus = null
        keyBytes = 0
    }

    private fun accepts(target: ContractTargetHandle): Boolean {
        val captured = target as? Target ?: return false
        return !isClosed && !frozen && captured.identity === identity && captured.generation == generation && find(captured.remoteIp) != null
    }

    private fun editable(remoteIp: String): Row? = if (isClosed || frozen) null else find(remoteIp)

    private fun find(remoteIp: String): Row? {
        val ip = WireFormats.parseIpv4(remoteIp) ?: return null
        val doc = document ?: return null
        if (!doc.network.isClient(ip) || ip == doc.localIp) return null
        val index = (ip - doc.network.firstClient).toInt() - if (ip > doc.localIp) 1 else 0
        return rows.getOrNull(index)?.takeIf { it.remoteIp == ip }
    }

    private fun view(row: Row) = ContractRowView(
        remoteIp = row.text,
        enabled = row.enabled,
        keyText = SecretText.adopt(row.key.copyOf()),
        keyValid = row.keyValid,
        dirty = row.dirty(),
        keyErrorVisible = row.keyErrorVisible(),
    )

    override fun toString(): String = "StandInContractsModel(rows=${rows.size}, dirty=$dirty)"

    companion object {
        private val HEX = "0123456789abcdef".toCharArray()

        /** `ContractsModel::create`: the full validation runs before any row is allocated. */
        fun create(document: String, configPath: String = "", revisionToken: String = ""): StandInResult<StandInContractsModel> {
            val parsed = StandInContractsDocument.parse(document)
            if (parsed is StandInResult.Failed) return parsed
            val doc = (parsed as StandInResult.Ok).value
            val count = checkedContractPeerCount(doc.network, doc.localIp)
            if (count is StandInResult.Failed) return count
            val model = StandInContractsModel(doc, configPath, revisionToken)
            for (ip in doc.network.firstClient..doc.network.lastClient) {
                if (ip == doc.localIp) continue
                val row = Row(ip, WireFormats.formatIpv4(ip))
                // Missing JSON keys stay absent in both the working and the
                // baseline buffer; deriving a default is an explicit action.
                doc.existingKey(ip)?.let { key ->
                    row.key = key.toCharArray()
                    row.enabled = true
                    row.originalEnabled = true
                }
                row.keyValid = isValidContractKey(row.key)
                row.originalKey = row.key.copyOf()
                model.keyBytes += utf8Length(row.key)
                model.rows += row
            }
            return StandInResult.Ok(model)
        }
    }
}
