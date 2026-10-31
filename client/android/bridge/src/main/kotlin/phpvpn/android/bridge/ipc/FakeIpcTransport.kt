package phpvpn.android.bridge.ipc

import kotlinx.coroutines.delay
import kotlinx.serialization.json.JsonObject

/**
 * Scripted [IpcTransport] for JVM tests and previews, without JNI.
 *
 * Replies are chosen per command: one-shot replies queued with [enqueue] are
 * used first, then the persistent handler set with [respond]; a command
 * without either times out (null, like the real transport at its deadline).
 * A [Reply.Delayed] reply waits on the coroutine clock, so with
 * kotlinx-coroutines-test a delay longer than the caller's budget behaves as a
 * timeout. Every request is recorded (parsed) in [requests] because the client
 * wipes the request bytes after the call. Test-only: the recorded requests
 * may contain secrets and must never be logged.
 */
class FakeIpcTransport : IpcTransport {
    sealed interface Reply {
        /** Exact bytes, for malformed or non-UTF-8 replies. */
        class Raw(val bytes: ByteArray) : Reply

        /** UTF-8 text; every `$ID` is replaced with the request identifier. */
        data class Text(val text: String) : Reply

        /** No reply within the budget (transport returns null). */
        data object Timeout : Reply

        /** Waits [delayMs] on the coroutine clock, then answers [reply]. */
        data class Delayed(val delayMs: Long, val reply: Reply) : Reply
    }

    data class Recorded(val command: String, val json: JsonObject, val timeoutMs: Int) {
        val requestId: String get() = JsonChecks.string(json["requestId"]) ?: ""
    }

    private val queues = HashMap<String, ArrayDeque<(Recorded) -> Reply>>()
    private val handlers = HashMap<String, (Recorded) -> Reply>()
    private val recorded = ArrayList<Recorded>()

    val requests: List<Recorded> get() = synchronized(this) { recorded.toList() }

    fun requests(command: String): List<Recorded> = requests.filter { it.command == command }

    fun clearRequests() = synchronized(this) { recorded.clear() }

    /** Persistent reply for every request of [command]. */
    fun respond(command: String, handler: (Recorded) -> Reply) = synchronized(this) {
        handlers[command] = handler
    }

    /** One-shot replies for the next requests of [command], in order. */
    fun enqueue(command: String, vararg replies: (Recorded) -> Reply) = synchronized(this) {
        val queue = queues.getOrPut(command) { ArrayDeque() }
        replies.forEach { queue.addLast(it) }
    }

    fun enqueueText(command: String, vararg texts: String) =
        enqueue(command, *texts.map { text -> { _: Recorded -> Reply.Text(text) } }.toTypedArray())

    override suspend fun dispatch(request: ByteArray, timeoutMs: Int): ByteArray? {
        val json = StrictJson.parse(request) as? JsonObject ?: return null
        val command = JsonChecks.string(json["command"]) ?: return null
        val entry = Recorded(command, json, timeoutMs)
        val reply = synchronized(this) {
            recorded += entry
            queues[command]?.removeFirstOrNull()?.invoke(entry) ?: handlers[command]?.invoke(entry)
        } ?: return null
        return resolve(reply, entry, timeoutMs.toLong())
    }

    private suspend fun resolve(reply: Reply, entry: Recorded, budgetMs: Long): ByteArray? = when (reply) {
        is Reply.Raw -> reply.bytes.copyOf()
        is Reply.Text -> reply.text.replace("\$ID", entry.requestId).toByteArray(Charsets.UTF_8)
        Reply.Timeout -> null
        is Reply.Delayed -> if (reply.delayMs >= budgetMs) {
            delay(budgetMs)
            null
        } else {
            delay(reply.delayMs)
            resolve(reply.reply, entry, budgetMs - reply.delayMs)
        }
    }
}
