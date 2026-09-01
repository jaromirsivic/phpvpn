#include "core/protocol/json_scanner.h"

namespace phpvpn {
namespace {

class WireScanner {
public:
    WireScanner(std::string_view text, bool allowWhitespace)
        : text_(text), allowWhitespace_(allowWhitespace) {}

    bool atEnd() const noexcept { return position_ >= text_.size(); }
    char peek() const noexcept { return text_[position_]; }
    std::size_t position() const noexcept { return position_; }

    void skipWhitespace() noexcept {
        if (!allowWhitespace_) {
            return;
        }
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

    // Reads a JSON string that must not contain an escape sequence, a control
    // character or a byte above 0x7F, and returns a view into the source bytes.
    Result<std::string_view> readAsciiString() {
        if (!consume('"')) {
            return Error(ErrorCode::wire_invalid_json, "string expected");
        }
        const std::size_t start = position_;
        while (true) {
            if (atEnd()) {
                return Error(ErrorCode::wire_invalid_json, "unterminated string");
            }
            const auto c = static_cast<unsigned char>(text_[position_]);
            if (c == '"') {
                break;
            }
            if (c == '\\') {
                return Error(ErrorCode::wire_invalid_json, "escape sequence is not allowed");
            }
            if (c < 0x20 || c > 0x7E) {
                return Error(ErrorCode::wire_invalid_json, "non printable byte inside a string");
            }
            ++position_;
        }
        const std::string_view value = text_.substr(start, position_ - start);
        ++position_;  // closing quote
        return value;
    }

    Result<std::int64_t> readNonNegativeInteger() {
        if (atEnd()) {
            return Error(ErrorCode::wire_invalid_json, "number expected");
        }
        const std::size_t start = position_;
        if (peek() == '0') {
            ++position_;
        } else if (peek() >= '1' && peek() <= '9') {
            while (!atEnd() && peek() >= '0' && peek() <= '9') {
                ++position_;
            }
        } else {
            return Error(ErrorCode::wire_invalid_json, "non negative integer expected");
        }
        if (position_ - start > 19) {
            return Error(ErrorCode::wire_invalid_json, "integer out of range");
        }

        std::int64_t value = 0;
        for (std::size_t i = start; i < position_; ++i) {
            const std::int64_t digit = text_[i] - '0';
            if (value > (INT64_MAX - digit) / 10) {
                return Error(ErrorCode::wire_invalid_json, "integer out of range");
            }
            value = value * 10 + digit;
        }
        return value;
    }

    Result<std::vector<std::string_view>> readStringArray() {
        if (!consume('[')) {
            return Error(ErrorCode::wire_invalid_json, "array expected");
        }
        std::vector<std::string_view> items;
        skipWhitespace();
        if (consume(']')) {
            return items;
        }
        for (;;) {
            skipWhitespace();
            std::string_view item;
            PHPVPN_TRY_ASSIGN(item, readAsciiString());
            items.push_back(item);
            skipWhitespace();
            if (consume(',')) {
                continue;
            }
            if (consume(']')) {
                break;
            }
            return Error(ErrorCode::wire_invalid_json, "comma or closing bracket expected");
        }
        return items;
    }

private:
    std::string_view text_;
    bool allowWhitespace_;
    std::size_t position_ = 0;
};

Result<FlatObjectFields> scanFlatStringObject(std::string_view text,
                                              std::span<const std::string_view> keys,
                                              bool allowWhitespace,
                                              bool requireOrder) {
    WireScanner scanner(text, allowWhitespace);
    scanner.skipWhitespace();
    if (!scanner.consume('{')) {
        return Error(ErrorCode::wire_invalid_json, "object expected");
    }

    FlatObjectFields values(keys.size());
    std::vector<bool> seen(keys.size(), false);
    std::size_t memberIndex = 0;

    scanner.skipWhitespace();
    if (!scanner.consume('}')) {
        for (;;) {
            scanner.skipWhitespace();
            std::string_view key;
            PHPVPN_TRY_ASSIGN(key, scanner.readAsciiString());

            std::size_t index = keys.size();
            for (std::size_t i = 0; i < keys.size(); ++i) {
                if (keys[i] == key) {
                    index = i;
                    break;
                }
            }
            if (index == keys.size()) {
                return Error(ErrorCode::wire_unexpected_field, "unknown member");
            }
            if (seen[index]) {
                return Error(ErrorCode::wire_duplicate_field, "duplicate member");
            }
            if (requireOrder && index != memberIndex) {
                return Error(ErrorCode::wire_invalid_record_order, "unexpected member order");
            }
            seen[index] = true;
            ++memberIndex;

            scanner.skipWhitespace();
            if (!scanner.consume(':')) {
                return Error(ErrorCode::wire_invalid_json, "colon expected");
            }
            scanner.skipWhitespace();

            std::string_view value;
            PHPVPN_TRY_ASSIGN(value, scanner.readAsciiString());
            values[index] = value;

            scanner.skipWhitespace();
            if (scanner.consume(',')) {
                continue;
            }
            if (scanner.consume('}')) {
                break;
            }
            return Error(ErrorCode::wire_invalid_json, "comma or closing brace expected");
        }
    }

    scanner.skipWhitespace();
    if (!scanner.atEnd()) {
        return Error(ErrorCode::wire_invalid_json, "trailing bytes after the object");
    }
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (!seen[i]) {
            return Error(ErrorCode::wire_missing_field, "missing member");
        }
    }

    return values;
}

}  // namespace

Result<FlatObjectFields> JsonScanner::parseFlatStringObject(
    std::string_view text, std::span<const std::string_view> allowedKeys) {
    return scanFlatStringObject(text, allowedKeys, /*allowWhitespace=*/true, /*requireOrder=*/false);
}

Result<FlatObjectFields> JsonScanner::parseOrderedCompactStringObject(
    std::string_view text, std::span<const std::string_view> orderedKeys) {
    return scanFlatStringObject(text, orderedKeys, /*allowWhitespace=*/false, /*requireOrder=*/true);
}

Result<DataResultRecord> parseDataResultRecord(std::string_view text) {
    static constexpr std::string_view kKeys[] = {"type",
                                                 "requestId",
                                                 "acceptedMessageIds",
                                                 "deferredMessageIds",
                                                 "acceptedRecords",
                                                 "acceptedBytes"};

    WireScanner scanner(text, /*allowWhitespace=*/true);
    scanner.skipWhitespace();
    if (!scanner.consume('{')) {
        return Error(ErrorCode::wire_invalid_json, "object expected");
    }

    DataResultRecord record;
    std::string_view type;
    bool seen[6] = {};

    for (;;) {
        scanner.skipWhitespace();
        std::string_view key;
        PHPVPN_TRY_ASSIGN(key, scanner.readAsciiString());

        std::size_t index = 6;
        for (std::size_t i = 0; i < 6; ++i) {
            if (kKeys[i] == key) {
                index = i;
                break;
            }
        }
        if (index == 6) {
            return Error(ErrorCode::wire_unexpected_field, "unknown member of data-result");
        }
        if (seen[index]) {
            return Error(ErrorCode::wire_duplicate_field, "duplicate member of data-result");
        }
        seen[index] = true;

        scanner.skipWhitespace();
        if (!scanner.consume(':')) {
            return Error(ErrorCode::wire_invalid_json, "colon expected");
        }
        scanner.skipWhitespace();

        switch (index) {
            case 0:
                PHPVPN_TRY_ASSIGN(type, scanner.readAsciiString());
                break;
            case 1:
                PHPVPN_TRY_ASSIGN(record.requestId, scanner.readAsciiString());
                break;
            case 2:
                PHPVPN_TRY_ASSIGN(record.acceptedMessageIds, scanner.readStringArray());
                break;
            case 3:
                PHPVPN_TRY_ASSIGN(record.deferredMessageIds, scanner.readStringArray());
                break;
            case 4:
                PHPVPN_TRY_ASSIGN(record.acceptedRecords, scanner.readNonNegativeInteger());
                break;
            default:
                PHPVPN_TRY_ASSIGN(record.acceptedBytes, scanner.readNonNegativeInteger());
                break;
        }

        scanner.skipWhitespace();
        if (scanner.consume(',')) {
            continue;
        }
        if (scanner.consume('}')) {
            break;
        }
        return Error(ErrorCode::wire_invalid_json, "comma or closing brace expected");
    }

    scanner.skipWhitespace();
    if (!scanner.atEnd()) {
        return Error(ErrorCode::wire_invalid_json, "trailing bytes after data-result");
    }
    for (bool present : seen) {
        if (!present) {
            return Error(ErrorCode::wire_missing_field, "missing member of data-result");
        }
    }
    if (type != "data-result") {
        return Error(ErrorCode::wire_invalid_json, "unexpected record type");
    }

    return record;
}

}  // namespace phpvpn
