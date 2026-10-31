@file:OptIn(ExperimentalSerializationApi::class)

package phpvpn.android.bridge.ipc

import java.math.BigInteger
import java.nio.ByteBuffer
import java.nio.charset.CharacterCodingException
import java.nio.charset.CodingErrorAction
import kotlinx.serialization.ExperimentalSerializationApi
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.JsonUnquotedLiteral

/**
 * Strict JSON reader with the rules of the C++ `Json::parse` (core/protocol/json.cpp).
 *
 * kotlinx.serialization silently keeps the last of duplicate members and
 * accepts invalid UTF-8, so every IPC v1 reply is read here first: strict UTF-8,
 * no duplicate member, no trailing bytes, at most 32 nested levels, no control
 * character inside a string, paired surrogate escapes only. Numbers keep their
 * exact literal (an unquoted [JsonPrimitive]), so the full uint64 range of the
 * counters survives and `1.0` stays distinguishable from `1`.
 *
 * The result is the kotlinx [JsonElement] tree, which the typed models then
 * decode after [WireValidation] has checked the closed schema.
 */
object StrictJson {
    const val MAX_DEPTH = 32

    /** Parses UTF-8 bytes. Returns null for any syntax or encoding violation. */
    fun parse(bytes: ByteArray): JsonElement? {
        val text = decodeUtf8(bytes) ?: return null
        return parse(text)
    }

    /** Parses already decoded text. Returns null for any syntax violation. */
    fun parse(text: String): JsonElement? = try {
        Parser(text).run()
    } catch (_: SyntaxError) {
        null
    }

    /** Strict UTF-8 decoding: malformed, overlong and surrogate sequences fail. */
    fun decodeUtf8(bytes: ByteArray): String? = try {
        Charsets.UTF_8.newDecoder()
            .onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT)
            .decode(ByteBuffer.wrap(bytes))
            .toString()
    } catch (_: CharacterCodingException) {
        null
    }

    private class SyntaxError : Exception() {
        override fun fillInStackTrace(): Throwable = this
    }

    private class Parser(private val text: String) {
        private var position = 0

        fun run(): JsonElement {
            skipWhitespace()
            val value = parseValue(0)
            skipWhitespace()
            if (position != text.length) throw SyntaxError()
            return value
        }

        private fun atEnd() = position >= text.length

        private fun skipWhitespace() {
            while (position < text.length) {
                val c = text[position]
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') position++ else break
            }
        }

        private fun consume(expected: Char): Boolean {
            if (!atEnd() && text[position] == expected) {
                position++
                return true
            }
            return false
        }

        private fun parseValue(depth: Int): JsonElement {
            if (depth > MAX_DEPTH || atEnd()) throw SyntaxError()
            return when (text[position]) {
                '{' -> parseObject(depth)
                '[' -> parseArray(depth)
                '"' -> JsonPrimitive(parseString())
                't' -> literal("true", JsonPrimitive(true))
                'f' -> literal("false", JsonPrimitive(false))
                'n' -> literal("null", JsonNull)
                else -> parseNumber()
            }
        }

        private fun literal(word: String, value: JsonElement): JsonElement {
            if (!text.startsWith(word, position)) throw SyntaxError()
            position += word.length
            return value
        }

        private fun parseObject(depth: Int): JsonObject {
            consume('{')
            skipWhitespace()
            val members = LinkedHashMap<String, JsonElement>()
            if (consume('}')) return JsonObject(members)
            while (true) {
                skipWhitespace()
                if (atEnd() || text[position] != '"') throw SyntaxError()
                val name = parseString()
                if (members.containsKey(name)) throw SyntaxError()
                skipWhitespace()
                if (!consume(':')) throw SyntaxError()
                skipWhitespace()
                members[name] = parseValue(depth + 1)
                skipWhitespace()
                if (consume(',')) continue
                if (consume('}')) return JsonObject(members)
                throw SyntaxError()
            }
        }

        private fun parseArray(depth: Int): JsonArray {
            consume('[')
            skipWhitespace()
            val items = ArrayList<JsonElement>()
            if (consume(']')) return JsonArray(items)
            while (true) {
                skipWhitespace()
                items += parseValue(depth + 1)
                skipWhitespace()
                if (consume(',')) continue
                if (consume(']')) return JsonArray(items)
                throw SyntaxError()
            }
        }

        private fun parseString(): String {
            if (!consume('"')) throw SyntaxError()
            val out = StringBuilder()
            while (true) {
                if (atEnd()) throw SyntaxError()
                val c = text[position]
                if (c == '"') {
                    position++
                    return out.toString()
                }
                if (c < ' ') throw SyntaxError()
                if (c != '\\') {
                    out.append(c)
                    position++
                    continue
                }
                position++
                if (atEnd()) throw SyntaxError()
                when (text[position++]) {
                    '"' -> out.append('"')
                    '\\' -> out.append('\\')
                    '/' -> out.append('/')
                    'b' -> out.append('\b')
                    'f' -> out.append('\u000c')
                    'n' -> out.append('\n')
                    'r' -> out.append('\r')
                    't' -> out.append('\t')
                    'u' -> {
                        val code = readHex4()
                        if (code in 0xD800..0xDBFF) {
                            // A high surrogate must be followed by its low pair.
                            if (position + 1 >= text.length || text[position] != '\\' || text[position + 1] != 'u') {
                                throw SyntaxError()
                            }
                            position += 2
                            val low = readHex4()
                            if (low !in 0xDC00..0xDFFF) throw SyntaxError()
                            out.append(code.toChar()).append(low.toChar())
                        } else if (code in 0xDC00..0xDFFF) {
                            throw SyntaxError()
                        } else {
                            out.append(code.toChar())
                        }
                    }
                    else -> throw SyntaxError()
                }
            }
        }

        private fun readHex4(): Int {
            if (position + 4 > text.length) throw SyntaxError()
            var value = 0
            repeat(4) {
                val digit = Character.digit(text[position++], 16)
                if (digit < 0) throw SyntaxError()
                value = value * 16 + digit
            }
            return value
        }

        private fun parseNumber(): JsonElement {
            val start = position
            consume('-')
            if (atEnd()) throw SyntaxError()
            when {
                text[position] == '0' -> position++
                text[position] in '1'..'9' -> while (!atEnd() && text[position] in '0'..'9') position++
                else -> throw SyntaxError()
            }
            if (!atEnd() && text[position] == '.') {
                position++
                if (atEnd() || text[position] !in '0'..'9') throw SyntaxError()
                while (!atEnd() && text[position] in '0'..'9') position++
            }
            if (!atEnd() && (text[position] == 'e' || text[position] == 'E')) {
                position++
                if (!atEnd() && (text[position] == '+' || text[position] == '-')) position++
                if (atEnd() || text[position] !in '0'..'9') throw SyntaxError()
                while (!atEnd() && text[position] in '0'..'9') position++
            }
            return JsonUnquotedLiteral(text.substring(start, position))
        }
    }
}

/** Typed checks over the strict tree, the counterparts of the C++ helpers. */
object JsonChecks {
    private val UINT64_MAX: BigInteger = BigInteger.ONE.shiftLeft(64).subtract(BigInteger.ONE)
    private val INTEGER = Regex("-?(0|[1-9][0-9]*)")

    /** An integral literal (no fraction, no exponent) as an exact value. */
    fun integer(value: JsonElement?): BigInteger? {
        if (value !is JsonPrimitive || value.isString || value is JsonNull) return null
        val content = value.content
        if (!INTEGER.matches(content)) return null
        return BigInteger(content)
    }

    /** A non-negative integer in the uint64 range (`number` in wizard_ipc.cpp). */
    fun uint64(value: JsonElement?, positive: Boolean = false): ULong? {
        val number = integer(value) ?: return null
        if (number.signum() < 0 || number > UINT64_MAX) return null
        if (positive && number.signum() == 0) return null
        return number.toString().toULong()
    }

    /** Signed int64 (`isInteger` of the C++ parser). */
    fun int64(value: JsonElement?): Long? {
        val number = integer(value) ?: return null
        return if (number.bitLength() <= 63) number.toLong() else null
    }

    fun string(value: JsonElement?): String? =
        if (value is JsonPrimitive && value.isString) value.content else null

    fun bool(value: JsonElement?): Boolean? =
        if (value is JsonPrimitive && !value.isString && value !is JsonNull) value.content.toBooleanStrictOrNull() else null

    fun isNull(value: JsonElement?): Boolean = value is JsonNull

    fun nullableString(value: JsonElement?): Boolean = value is JsonNull || string(value) != null

    fun oneOf(value: JsonElement?, vararg allowed: String): Boolean = string(value)?.let { it in allowed } ?: false

    /** Exactly these member names, no more and no fewer (`shape` in wizard_ipc.cpp). */
    fun shape(value: JsonElement?, vararg names: String): Boolean =
        value is JsonObject && value.size == names.size && names.all { value.containsKey(it) }

    fun obj(value: JsonElement?): JsonObject? = value as? JsonObject
}
