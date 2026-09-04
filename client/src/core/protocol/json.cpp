#include "core/protocol/json.h"

#include <charconv>
#include <cmath>
#include <cstdlib>

namespace phpvpn {
namespace {

class Parser {
public:
    Parser(std::string_view text, std::size_t maxDepth) : text_(text), maxDepth_(maxDepth) {}

    Result<JsonValue> run() {
        skipWhitespace();
        auto value = parseValue(0);
        if (!value.ok()) {
            return value;
        }
        skipWhitespace();
        if (position_ != text_.size()) {
            return Error(ErrorCode::config_invalid_json, "trailing bytes after the document");
        }
        return value;
    }

private:
    bool atEnd() const noexcept { return position_ >= text_.size(); }
    char peek() const noexcept { return text_[position_]; }

    void skipWhitespace() noexcept {
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++position_;
            } else {
                break;
            }
        }
    }

    bool consume(char expected) noexcept {
        if (!atEnd() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    bool consumeLiteral(std::string_view literal) noexcept {
        if (text_.compare(position_, literal.size(), literal) == 0) {
            position_ += literal.size();
            return true;
        }
        return false;
    }

    Result<JsonValue> parseValue(std::size_t depth) {
        if (depth > maxDepth_) {
            return Error(ErrorCode::config_invalid_json, "document nested too deeply");
        }
        if (atEnd()) {
            return Error(ErrorCode::config_invalid_json, "unexpected end of document");
        }

        switch (peek()) {
            case '{':
                return parseObject(depth);
            case '[':
                return parseArray(depth);
            case '"': {
                std::string value;
                PHPVPN_TRY(parseString(value));
                return JsonValue::makeString(std::move(value));
            }
            case 't':
                if (consumeLiteral("true")) {
                    return JsonValue::makeBool(true);
                }
                return Error(ErrorCode::config_invalid_json, "invalid literal");
            case 'f':
                if (consumeLiteral("false")) {
                    return JsonValue::makeBool(false);
                }
                return Error(ErrorCode::config_invalid_json, "invalid literal");
            case 'n':
                if (consumeLiteral("null")) {
                    return JsonValue::makeNull();
                }
                return Error(ErrorCode::config_invalid_json, "invalid literal");
            default:
                return parseNumber();
        }
    }

    Result<JsonValue> parseObject(std::size_t depth) {
        consume('{');
        JsonObject members;
        skipWhitespace();
        if (consume('}')) {
            return JsonValue::makeObject(std::move(members));
        }

        for (;;) {
            skipWhitespace();
            if (atEnd() || peek() != '"') {
                return Error(ErrorCode::config_invalid_json, "object member name expected");
            }
            std::string key;
            PHPVPN_TRY(parseString(key));

            for (const auto& member : members) {
                if (member.first == key) {
                    return Error(ErrorCode::config_invalid_json, "duplicate object member");
                }
            }

            skipWhitespace();
            if (!consume(':')) {
                return Error(ErrorCode::config_invalid_json, "colon expected");
            }
            skipWhitespace();

            auto value = parseValue(depth + 1);
            if (!value.ok()) {
                return value;
            }
            members.emplace_back(std::move(key), std::move(value).value());

            skipWhitespace();
            if (consume(',')) {
                continue;
            }
            if (consume('}')) {
                break;
            }
            return Error(ErrorCode::config_invalid_json, "comma or closing brace expected");
        }

        return JsonValue::makeObject(std::move(members));
    }

    Result<JsonValue> parseArray(std::size_t depth) {
        consume('[');
        JsonArray items;
        skipWhitespace();
        if (consume(']')) {
            return JsonValue::makeArray(std::move(items));
        }

        for (;;) {
            skipWhitespace();
            auto value = parseValue(depth + 1);
            if (!value.ok()) {
                return value;
            }
            items.push_back(std::move(value).value());

            skipWhitespace();
            if (consume(',')) {
                continue;
            }
            if (consume(']')) {
                break;
            }
            return Error(ErrorCode::config_invalid_json, "comma or closing bracket expected");
        }

        return JsonValue::makeArray(std::move(items));
    }

    Status parseString(std::string& out) {
        if (!consume('"')) {
            return fail(ErrorCode::config_invalid_json, "string expected");
        }
        std::string value;
        while (true) {
            if (atEnd()) {
                return fail(ErrorCode::config_invalid_json, "unterminated string");
            }
            const auto c = static_cast<unsigned char>(text_[position_]);
            if (c == '"') {
                ++position_;
                break;
            }
            if (c < 0x20) {
                return fail(ErrorCode::config_invalid_json, "control character inside a string");
            }
            if (c != '\\') {
                value.push_back(text_[position_]);
                ++position_;
                continue;
            }

            ++position_;
            if (atEnd()) {
                return fail(ErrorCode::config_invalid_json, "unterminated escape sequence");
            }
            const char escape = text_[position_++];
            switch (escape) {
                case '"':
                    value.push_back('"');
                    break;
                case '\\':
                    value.push_back('\\');
                    break;
                case '/':
                    value.push_back('/');
                    break;
                case 'b':
                    value.push_back('\b');
                    break;
                case 'f':
                    value.push_back('\f');
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                case 'u': {
                    std::uint32_t code = 0;
                    PHPVPN_TRY(readHex4(code));
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        // A high surrogate must be followed by its low pair.
                        if (position_ + 1 >= text_.size() || text_[position_] != '\\' ||
                            text_[position_ + 1] != 'u') {
                            return fail(ErrorCode::config_invalid_json, "unpaired surrogate");
                        }
                        position_ += 2;
                        std::uint32_t low = 0;
                        PHPVPN_TRY(readHex4(low));
                        if (low < 0xDC00 || low > 0xDFFF) {
                            return fail(ErrorCode::config_invalid_json, "unpaired surrogate");
                        }
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else if (code >= 0xDC00 && code <= 0xDFFF) {
                        return fail(ErrorCode::config_invalid_json, "unpaired surrogate");
                    }
                    appendUtf8(value, code);
                    break;
                }
                default:
                    return fail(ErrorCode::config_invalid_json, "unknown escape sequence");
            }
        }
        out = std::move(value);
        return ok();
    }

    Status readHex4(std::uint32_t& out) {
        if (position_ + 4 > text_.size()) {
            return fail(ErrorCode::config_invalid_json, "truncated unicode escape");
        }
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[position_ + static_cast<std::size_t>(i)];
            int digit;
            if (c >= '0' && c <= '9') {
                digit = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                digit = c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
                digit = c - 'A' + 10;
            } else {
                return fail(ErrorCode::config_invalid_json, "invalid unicode escape");
            }
            value = (value << 4) | static_cast<std::uint32_t>(digit);
        }
        position_ += 4;
        out = value;
        return ok();
    }

    static void appendUtf8(std::string& out, std::uint32_t code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0u | (code >> 6)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0u | (code >> 12)));
            out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | (code >> 18)));
            out.push_back(static_cast<char>(0x80u | ((code >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        }
    }

    Result<JsonValue> parseNumber() {
        const std::size_t start = position_;
        if (consume('-')) {
            // sign consumed
        }
        if (atEnd()) {
            return Error(ErrorCode::config_invalid_json, "number expected");
        }
        if (peek() == '0') {
            ++position_;
        } else if (peek() >= '1' && peek() <= '9') {
            while (!atEnd() && peek() >= '0' && peek() <= '9') {
                ++position_;
            }
        } else {
            return Error(ErrorCode::config_invalid_json, "number expected");
        }

        bool integral = true;
        if (!atEnd() && peek() == '.') {
            integral = false;
            ++position_;
            if (atEnd() || peek() < '0' || peek() > '9') {
                return Error(ErrorCode::config_invalid_json, "fraction digits expected");
            }
            while (!atEnd() && peek() >= '0' && peek() <= '9') {
                ++position_;
            }
        }
        if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
            integral = false;
            ++position_;
            if (!atEnd() && (peek() == '+' || peek() == '-')) {
                ++position_;
            }
            if (atEnd() || peek() < '0' || peek() > '9') {
                return Error(ErrorCode::config_invalid_json, "exponent digits expected");
            }
            while (!atEnd() && peek() >= '0' && peek() <= '9') {
                ++position_;
            }
        }

        const std::string_view literal = text_.substr(start, position_ - start);
        if (integral) {
            std::int64_t value = 0;
            const auto result =
                std::from_chars(literal.data(), literal.data() + literal.size(), value);
            if (result.ec != std::errc() || result.ptr != literal.data() + literal.size()) {
                if (literal.front() != '-') {
                    std::uint64_t unsignedValue = 0;
                    const auto unsignedResult = std::from_chars(
                        literal.data(), literal.data() + literal.size(), unsignedValue);
                    if (unsignedResult.ec == std::errc() &&
                        unsignedResult.ptr == literal.data() + literal.size()) {
                        return JsonValue::makeUnsignedInteger(unsignedValue);
                    }
                }
                // Out of range for int64_t; keep it as a real so the validator
                // can reject it with a precise configuration error.
                return JsonValue::makeReal(std::strtod(std::string(literal).c_str(), nullptr));
            }
            return JsonValue::makeInteger(value);
        }

        return JsonValue::makeReal(std::strtod(std::string(literal).c_str(), nullptr));
    }

    std::string_view text_;
    std::size_t position_ = 0;
    std::size_t maxDepth_;
};

void serializeInto(const JsonValue& value, std::string& out, int indent, int level) {
    const bool pretty = indent > 0;
    const auto newline = [&](int depth) {
        if (pretty) {
            out.push_back('\n');
            out.append(static_cast<std::size_t>(depth * indent), ' ');
        }
    };

    switch (value.type()) {
        case JsonType::null:
            out += "null";
            break;
        case JsonType::boolean:
            out += value.boolValue() ? "true" : "false";
            break;
        case JsonType::integer:
            out += std::to_string(value.integerValue());
            break;
        case JsonType::unsignedInteger:
            out += std::to_string(value.unsignedValue());
            break;
        case JsonType::real: {
            char buffer[64];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value.realValue());
            out.append(buffer, static_cast<std::size_t>(result.ptr - buffer));
            break;
        }
        case JsonType::string:
            out += Json::escapeString(value.stringValue());
            break;
        case JsonType::array: {
            const JsonArray& items = value.arrayValue();
            if (items.empty()) {
                out += "[]";
                break;
            }
            out.push_back('[');
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                newline(level + 1);
                serializeInto(items[i], out, indent, level + 1);
            }
            newline(level);
            out.push_back(']');
            break;
        }
        case JsonType::object: {
            const JsonObject& members = value.objectValue();
            if (members.empty()) {
                out += "{}";
                break;
            }
            out.push_back('{');
            for (std::size_t i = 0; i < members.size(); ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                newline(level + 1);
                out += Json::escapeString(members[i].first);
                out.push_back(':');
                if (pretty) {
                    out.push_back(' ');
                }
                serializeInto(members[i].second, out, indent, level + 1);
            }
            newline(level);
            out.push_back('}');
            break;
        }
    }
}

}  // namespace

JsonValue JsonValue::makeNull() { return JsonValue(); }

JsonValue JsonValue::makeBool(bool value) {
    JsonValue json;
    json.type_ = JsonType::boolean;
    json.boolean_ = value;
    return json;
}

JsonValue JsonValue::makeInteger(std::int64_t value) {
    JsonValue json;
    json.type_ = JsonType::integer;
    json.integer_ = value;
    return json;
}

JsonValue JsonValue::makeUnsignedInteger(std::uint64_t value) {
    JsonValue json;
    json.type_ = JsonType::unsignedInteger;
    json.unsigned_ = value;
    return json;
}

std::uint64_t jsonUnsignedOrZero(const JsonValue* value) noexcept {
    if (value && value->isUnsignedInteger()) return value->unsignedValue();
    if (value && value->isInteger() && value->integerValue() >= 0)
        return static_cast<std::uint64_t>(value->integerValue());
    return 0;
}

JsonValue JsonValue::makeReal(double value) {
    JsonValue json;
    json.type_ = JsonType::real;
    json.real_ = value;
    return json;
}

JsonValue JsonValue::makeString(std::string value) {
    JsonValue json;
    json.type_ = JsonType::string;
    json.string_ = std::move(value);
    return json;
}

JsonValue JsonValue::makeArray(JsonArray value) {
    JsonValue json;
    json.type_ = JsonType::array;
    json.array_ = std::move(value);
    return json;
}

JsonValue JsonValue::makeObject(JsonObject value) {
    JsonValue json;
    json.type_ = JsonType::object;
    json.object_ = std::move(value);
    return json;
}

const JsonValue* JsonValue::find(std::string_view key) const noexcept {
    if (type_ != JsonType::object) {
        return nullptr;
    }
    for (const auto& member : object_) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

Result<JsonValue> Json::parse(std::string_view text, std::size_t maxDepth) {
    // A UTF-8 BOM is not part of the document and the protocol forbids it.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        return Error(ErrorCode::config_invalid_json, "byte order mark is not allowed");
    }
    Parser parser(text, maxDepth);
    return parser.run();
}

std::string Json::serialize(const JsonValue& value) {
    std::string out;
    serializeInto(value, out, 0, 0);
    return out;
}

std::string Json::serializePretty(const JsonValue& value, int indent) {
    std::string out;
    serializeInto(value, out, indent, 0);
    return out;
}

std::string Json::escapeString(std::string_view text) {
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out.push_back(kHexDigits[(c >> 4) & 0x0Fu]);
                    out.push_back(kHexDigits[c & 0x0Fu]);
                } else {
                    out.push_back(raw);
                }
                break;
        }
    }
    out.push_back('"');
    return out;
}

}  // namespace phpvpn
