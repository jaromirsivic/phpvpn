// Single pass scanners for the wire messages, plan_client.md decision 10.
//
// Wire messages have a closed field set and carry only ASCII values, so no
// escape sequence is ever required. The scanners therefore reject escapes,
// duplicate members, unknown members, missing members and any byte after the
// closing brace, which removes every ambiguity from the bytes that later enter
// a canonical string or an AAD.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/base/result.h"

namespace phpvpn {

// Result of a flat object scan: the values in the order of `allowedKeys`.
using FlatObjectFields = std::vector<std::string_view>;

class JsonScanner {
public:
    // Scans a flat object whose members are all JSON strings. Every key of
    // `allowedKeys` must be present exactly once and nothing else is accepted.
    // Insignificant whitespace between tokens is tolerated.
    static Result<FlatObjectFields> parseFlatStringObject(
        std::string_view text, std::span<const std::string_view> allowedKeys);

    // Same as above but additionally forbids any whitespace outside the string
    // values and requires the members in exactly the given order, as protocol.md
    // chapter 9.3 demands for the `data` record.
    static Result<FlatObjectFields> parseOrderedCompactStringObject(
        std::string_view text, std::span<const std::string_view> orderedKeys);
};

// `data-result` mixes strings, arrays of strings and non negative integers, so
// it gets a dedicated scanner instead of the generic flat one.
struct DataResultRecord {
    std::string_view requestId;
    std::vector<std::string_view> acceptedMessageIds;
    std::vector<std::string_view> deferredMessageIds;
    std::int64_t acceptedRecords = 0;
    std::int64_t acceptedBytes = 0;
};

Result<DataResultRecord> parseDataResultRecord(std::string_view text);

}  // namespace phpvpn
