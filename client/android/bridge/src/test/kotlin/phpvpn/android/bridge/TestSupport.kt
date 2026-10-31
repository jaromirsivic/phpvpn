package phpvpn.android.bridge

import java.io.File
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import phpvpn.android.bridge.ipc.FakeIpcTransport
import phpvpn.android.bridge.ipc.IdSource
import phpvpn.android.bridge.ipc.IpcJson
import phpvpn.android.bridge.ipc.StrictJson

/** Fixtures under src/test/resources/ipc and the desktop sources they mirror. */
object Fixtures {
    /** UUIDs of the desktop tests (test_wizard_ipc.cpp `id`/`other`, test_ipc.cpp, tray_fixture.h). */
    const val ID = "00000000-0000-4000-8000-000000000001"
    const val OTHER = "00000000-0000-4000-8000-000000000002"
    const val REQUEST_ID = "3f2504e0-4f89-41d3-9a0c-0305e82c3301"
    const val STATUS_ID = "3f2a7c1e-9b44-4b0e-8d2f-5c6a1e7b9d03"
    const val CONFIG_PATH = "/data/user/0/io.github.jaromirsivic.phpvpn/no_backup/phpvpn/config/config.json"

    fun text(name: String): String {
        val stream = Fixtures::class.java.getResourceAsStream("/ipc/$name") ?: error("missing fixture $name")
        return stream.use { it.readBytes().toString(Charsets.UTF_8) }
    }

    /** Fixture text with `$ID` replaced by [requestId]. */
    fun reply(name: String, requestId: String): String = text(name).replace("\$ID", requestId)

    fun json(name: String, requestId: String = ID): JsonObject = StrictJson.parse(reply(name, requestId)) as JsonObject

    /** client/ (the C++ sources), found from the Gradle module directory. */
    val clientRoot: File by lazy {
        var dir: File? = File("").absoluteFile
        while (dir != null && !File(dir, "src/core/base/error.h").isFile) dir = dir.parentFile
        dir ?: error("client/src/core/base/error.h not found above ${File("").absolutePath}")
    }

    fun source(relative: String): String = File(clientRoot, relative).readText(Charsets.UTF_8)
}

/** Deterministic UUIDv4 identifiers: ...4000-8000-0000000000NN. */
class SequentialIds(private var next: Int = 100) : IdSource {
    override fun next(): String = "00000000-0000-4000-8000-%012x".format(next++)
}

/** Answers every request of [command] with the fixture, correlated with the request id. */
fun FakeIpcTransport.respondFixture(command: String, fixture: String) =
    respond(command) { FakeIpcTransport.Reply.Text(Fixtures.text(fixture)) }

fun FakeIpcTransport.enqueueFixture(command: String, vararg fixtures: String) =
    enqueue(command, *fixtures.map { name -> { _: FakeIpcTransport.Recorded -> FakeIpcTransport.Reply.Text(Fixtures.text(name)) } }.toTypedArray())

// ---------------------------------------------------------------- tree edits (the C++ `set`)

fun JsonObject.with(name: String, value: JsonElement): JsonObject = JsonObject(this + (name to value))

fun JsonObject.with(name: String, value: String): JsonObject = with(name, JsonPrimitive(value))

fun JsonObject.without(name: String): JsonObject = JsonObject(this - name)

fun JsonObject.obj(name: String): JsonObject = this[name] as JsonObject

/** Replaces the object at [path] (nested member names) by [edit] of it. */
fun JsonObject.edit(vararg path: String, edit: (JsonObject) -> JsonObject): JsonObject =
    if (path.isEmpty()) edit(this) else with(path[0], obj(path[0]).edit(*path.drop(1).toTypedArray(), edit = edit))

fun JsonElement.serialized(): String = IpcJson.encodeToString(JsonElement.serializer(), this)

fun number(literal: String): JsonElement = StrictJson.parse(literal)!!

fun array(vararg items: String) = JsonArray(items.map { JsonPrimitive(it) })
