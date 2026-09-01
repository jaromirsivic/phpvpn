#include "core/protocol/ndjson_reader.h"

#include "core/base/hex.h"
#include "core/crypto/canonical.h"
#include "core/protocol/base64url.h"
#include "core/protocol/data_record.h"
#include "core/protocol/json_scanner.h"
#include "core/protocol/uuid.h"

namespace phpvpn {
namespace {

constexpr std::string_view kDataMacKeys[] = {"type", "requestId", "tag"};

// Cheap type probe on the compact record prefix. The full scanner still runs
// afterwards, this only decides which parser to use and keeps the order check
// independent of the record contents.
bool startsWithType(std::string_view line, std::string_view type) {
    std::string prefix;
    prefix.reserve(10 + type.size());
    prefix += R"({"type":")";
    prefix += type;
    prefix += '"';
    return line.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

Result<DataResponse> NdjsonResponseReader::parseAndVerify(CryptoProvider& crypto,
                                                          const SecureBuffer& fullAuthKey,
                                                          std::string_view expectedRequestId,
                                                          std::string_view body,
                                                          const NdjsonLimits& limits) {
    if (body.empty()) {
        return Error(ErrorCode::response_invalid, "the data response is empty");
    }
    if (body.back() != '\n') {
        return Error(ErrorCode::wire_invalid_json, "the data response does not end with LF");
    }

    DataResponse response;
    response.requestId.assign(expectedRequestId);

    // messageId and the exact base64url GCM tag text of every data line, in
    // response order, feed the canonical string of chapter 9.5.
    std::vector<std::pair<std::string, std::string>> macPairs;

    std::string_view dataResultLine;
    std::string_view dataMacLine;
    bool sawDataResult = false;
    bool sawDataMac = false;

    std::size_t offset = 0;
    while (offset < body.size()) {
        const std::size_t lineFeed = body.find('\n', offset);
        if (lineFeed == std::string_view::npos) {
            return Error(ErrorCode::wire_invalid_json, "a response line is not terminated by LF");
        }

        const std::size_t lineLengthWithLf = lineFeed - offset + 1;
        if (static_cast<std::int64_t>(lineLengthWithLf) > limits.maxNdjsonLineBytes) {
            return Error(ErrorCode::wire_line_too_long, "a response line exceeds the line limit");
        }

        const std::string_view line = body.substr(offset, lineFeed - offset);
        offset = lineFeed + 1;

        if (line.empty()) {
            return Error(ErrorCode::wire_invalid_json, "an empty response line is not allowed");
        }
        if (line.back() == '\r') {
            return Error(ErrorCode::wire_invalid_json, "CRLF is not allowed");
        }
        if (sawDataMac) {
            return Error(ErrorCode::wire_invalid_record_order, "a line follows data-mac");
        }

        if (startsWithType(line, "data-mac")) {
            if (!sawDataResult) {
                return Error(ErrorCode::wire_invalid_record_order,
                             "data-mac appears before data-result");
            }
            dataMacLine = line;
            sawDataMac = true;
            continue;
        }

        if (startsWithType(line, "data-result")) {
            if (sawDataResult) {
                return Error(ErrorCode::wire_invalid_record_order,
                             "more than one data-result record");
            }
            dataResultLine = line;
            sawDataResult = true;
            continue;
        }

        if (sawDataResult) {
            return Error(ErrorCode::wire_invalid_record_order,
                         "a data record follows data-result");
        }

        // The record count limit is applied before the fields of the exceeding
        // line are decoded, so no oversized response is processed.
        if (static_cast<std::int64_t>(response.records.size()) + 1 >
            limits.maxDataRecordsPerResponse) {
            return Error(ErrorCode::wire_too_many_records,
                         "the response holds more data records than configured");
        }

        DataRecordFields fields;
        PHPVPN_TRY_ASSIGN(fields, DataRecord::parse(line));

        DataResponseRecord record;
        record.messageId.assign(fields.messageId);
        record.src.assign(fields.src);
        record.dst.assign(fields.dst);
        record.ivText.assign(fields.iv);
        record.tagText.assign(fields.tag);
        if (!Base64Url::decode(fields.iv, record.iv) ||
            !Base64Url::decode(fields.tag, record.tag) ||
            !Base64Url::decode(fields.ciphertext, record.ciphertext)) {
            return Error(ErrorCode::wire_invalid_base64url, "a data record field is not canonical");
        }
        response.decodedCiphertextBytes += record.ciphertext.size();

        macPairs.emplace_back(record.messageId, record.tagText);
        response.records.push_back(std::move(record));
    }

    if (!sawDataResult || !sawDataMac) {
        return Error(ErrorCode::wire_invalid_record_order,
                     "the response is missing data-result or data-mac");
    }

    DataResultRecord result;
    PHPVPN_TRY_ASSIGN(result, parseDataResultRecord(dataResultLine));
    if (result.requestId != expectedRequestId) {
        return Error(ErrorCode::response_request_id_mismatch,
                     "data-result belongs to another request");
    }

    FlatObjectFields macFields;
    PHPVPN_TRY_ASSIGN(macFields, JsonScanner::parseFlatStringObject(dataMacLine, kDataMacKeys));
    if (macFields[0] != "data-mac") {
        return Error(ErrorCode::wire_invalid_json, "unexpected record type for data-mac");
    }
    if (macFields[1] != expectedRequestId) {
        return Error(ErrorCode::response_request_id_mismatch,
                     "data-mac belongs to another request");
    }
    if (!Base64Url::isCanonicalWithByteLength(macFields[2], kSha256Size)) {
        return Error(ErrorCode::wire_invalid_base64url, "data-mac tag must decode to 32 bytes");
    }
    std::vector<std::uint8_t> receivedTag;
    if (!Base64Url::decode(macFields[2], receivedTag)) {
        return Error(ErrorCode::wire_invalid_base64url, "data-mac tag is not canonical");
    }

    Sha256Digest resultDigest{};
    PHPVPN_TRY_ASSIGN(resultDigest, crypto.sha256(asBytes(dataResultLine)));
    const std::string resultHex =
        sha256Hex(std::span<const std::uint8_t>(resultDigest.data(), resultDigest.size()));

    const std::string canonical = dataMacCanonical(expectedRequestId, macPairs, resultHex);
    Sha256Digest expectedTag{};
    PHPVPN_TRY_ASSIGN(expectedTag, crypto.hmacSha256(fullAuthKey.span(), asBytes(canonical)));

    if (!constantTimeEquals(
            std::span<const std::uint8_t>(expectedTag.data(), expectedTag.size()),
            std::span<const std::uint8_t>(receivedTag.data(), receivedTag.size()))) {
        return Error(ErrorCode::response_mac_invalid, "data-mac did not verify");
    }

    // Only now may the result fields be used.
    for (std::string_view id : result.acceptedMessageIds) {
        if (!Uuid::isValidV4(id)) {
            return Error(ErrorCode::wire_invalid_uuid, "acceptedMessageIds holds an invalid id");
        }
        response.acceptedMessageIds.emplace_back(id);
    }
    for (std::string_view id : result.deferredMessageIds) {
        if (!Uuid::isValidV4(id)) {
            return Error(ErrorCode::wire_invalid_uuid, "deferredMessageIds holds an invalid id");
        }
        response.deferredMessageIds.emplace_back(id);
    }
    response.acceptedRecords = result.acceptedRecords;
    response.acceptedBytes = result.acceptedBytes;

    return response;
}

}  // namespace phpvpn
