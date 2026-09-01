// Small strict JSON DOM used by the configuration file and by IPC.
//
// plan_client.md decision 10: the configuration and IPC use a small DOM parser
// while the wire messages use single pass scanners with a closed field set. A
// duplicate member and an unknown member are always an error, so the parser
// reports duplicates instead of silently overwriting them the way a tolerant
// parser would.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/base/result.h"

namespace phpvpn {

class JsonValue;

using JsonObject = std::vector<std::pair<std::string, JsonValue>>;
using JsonArray = std::vector<JsonValue>;

enum class JsonType { null, boolean, integer, real, string, array, object };

class JsonValue {
public:
    JsonValue() = default;

    static JsonValue makeNull();
    static JsonValue makeBool(bool value);
    static JsonValue makeInteger(std::int64_t value);
    static JsonValue makeReal(double value);
    static JsonValue makeString(std::string value);
    static JsonValue makeArray(JsonArray value);
    static JsonValue makeObject(JsonObject value);

    JsonType type() const noexcept { return type_; }
    bool isNull() const noexcept { return type_ == JsonType::null; }
    bool isBool() const noexcept { return type_ == JsonType::boolean; }
    bool isInteger() const noexcept { return type_ == JsonType::integer; }
    bool isNumber() const noexcept {
        return type_ == JsonType::integer || type_ == JsonType::real;
    }
    bool isString() const noexcept { return type_ == JsonType::string; }
    bool isArray() const noexcept { return type_ == JsonType::array; }
    bool isObject() const noexcept { return type_ == JsonType::object; }

    bool boolValue() const noexcept { return boolean_; }
    std::int64_t integerValue() const noexcept { return integer_; }
    double realValue() const noexcept { return real_; }
    const std::string& stringValue() const noexcept { return string_; }
    const JsonArray& arrayValue() const noexcept { return array_; }
    const JsonObject& objectValue() const noexcept { return object_; }
    JsonObject& objectValue() noexcept { return object_; }

    // Ordered lookup; the object keeps the order of the source document so a
    // round trip through get-config and set-config stays stable.
    const JsonValue* find(std::string_view key) const noexcept;

private:
    JsonType type_ = JsonType::null;
    bool boolean_ = false;
    std::int64_t integer_ = 0;
    double real_ = 0.0;
    std::string string_;
    JsonArray array_;
    JsonObject object_;
};

class Json {
public:
    // Strict parser: rejects duplicate members, trailing bytes, comments, NaN,
    // infinities and control characters inside strings.
    static Result<JsonValue> parse(std::string_view text, std::size_t maxDepth = 32);

    // Compact serialisation without any insignificant whitespace.
    static std::string serialize(const JsonValue& value);

    // Pretty serialisation used only for the configuration file and for the
    // text the tray editor shows.
    static std::string serializePretty(const JsonValue& value, int indent = 2);

    // Escapes a string into a JSON string literal including the quotes.
    static std::string escapeString(std::string_view text);
};

}  // namespace phpvpn
