#include "core/config/contracts_document.h"

#include <algorithm>
#include <array>
#include "core/base/hex.h"
#include "core/ipc/ipc_protocol.h"

namespace phpvpn {
namespace {
// Only called on privately owned, mutable DOMs. JsonValue exposes const string
// and array accessors, but the underlying objects here are not const.
void wipeDom(JsonValue& value) noexcept {
    if (value.isString()) {
        auto& text = const_cast<std::string&>(value.stringValue());
        secureWipe(text.data(), text.size());
    }
    for (auto& item : const_cast<JsonArray&>(value.arrayValue())) wipeDom(item);
    for (auto& member : value.objectValue()) wipeDom(member.second);
}
struct TextWiper {
    std::string& text;
    ~TextWiper() { secureWipe(text.data(), text.size()); }
};
struct ValuesWiper {
    ConfigValues& values;
    ~ValuesWiper() {
        secureWipe(values.rawJson.data(), values.rawJson.size());
        secureWipe(values.serverSecret.data(), values.serverSecret.size());
    }
};
bool equalDom(const JsonValue& a, const JsonValue& b) {
    if (a.type() != b.type()) return false;
    switch (a.type()) {
        case JsonType::null: return true;
        case JsonType::boolean: return a.boolValue() == b.boolValue();
        case JsonType::integer: return a.integerValue() == b.integerValue();
        case JsonType::unsignedInteger: return a.unsignedValue() == b.unsignedValue();
        case JsonType::real: return a.realValue() == b.realValue();
        case JsonType::string: return a.stringValue() == b.stringValue();
        case JsonType::array:
            if (a.arrayValue().size() != b.arrayValue().size()) return false;
            for (std::size_t i = 0; i < a.arrayValue().size(); ++i)
                if (!equalDom(a.arrayValue()[i], b.arrayValue()[i])) return false;
            return true;
        case JsonType::object:
            if (a.objectValue().size() != b.objectValue().size()) return false;
            for (const auto& [key, value] : a.objectValue()) {
                const auto* other = b.find(key);
                if (!other || !equalDom(value, *other)) return false;
            }
            return true;
    }
    return false;
}
} // namespace

bool isValidContractKey(std::string_view text) noexcept {
    return text.size() == kAesKeySize * 2 && isLowercaseHex(text);
}
Result<std::size_t> checkedContractPeerCount(const Ipv4Cidr& cidr, Ipv4Address localIp) {
    // Reject even a default-constructed /0 before calling address arithmetic.
    if (cidr.prefixLength() < Ipv4Cidr::kMinPrefixLength ||
        cidr.prefixLength() > Ipv4Cidr::kMaxPrefixLength)
        return Error(ErrorCode::config_invalid_cidr);
    if (!cidr.isValidClientAddress(localIp)) return Error(ErrorCode::config_invalid_address);
    const std::uint64_t first = cidr.firstClientAddress().value();
    const std::uint64_t last = cidr.lastClientAddress().value();
    if (last < first || last - first > kMaxContractRows ||
        last - first + 1 != cidr.clientAddressCount())
        return Error(ErrorCode::config_invalid_cidr);
    return static_cast<std::size_t>(last - first); // clients minus self
}
Result<std::size_t> checkedContractKeyTextSize(std::size_t total, std::size_t oldSize,
                                            std::size_t newSize) {
    if (total > kMaxContractKeyTextBytes || oldSize > total ||
        newSize > kMaxContractKeyTextBytes - (total - oldSize))
        return Error(ErrorCode::config_too_large);
    return total - oldSize + newSize;
}
Status validateContractsEnvelopeSize(std::size_t encodedBytes) {
    return encodedBytes > kIpcMaxMessageBytes ? fail(ErrorCode::ipc_message_too_large) : ok();
}
ContractsDocument::~ContractsDocument() { wipeDom(dom_); }
ContractsCandidate::~ContractsCandidate() { wipeDom(dom_); }
std::string_view ContractsCandidate::text() const noexcept {
    return {reinterpret_cast<const char*>(text_.data()), text_.size()};
}
Result<std::unique_ptr<ContractsDocument>> ContractsDocument::parse(std::string_view text) {
    ConfigWarnings warnings;
    auto values = validateConfigValues(text, warnings, false);
    if (!values) return values.error();
    ValuesWiper wipe{*values};
    PHPVPN_TRY(checkedContractPeerCount(values->vpnCidr, *values->virtualIp));
    auto document = std::unique_ptr<ContractsDocument>(new ContractsDocument);
    PHPVPN_TRY_ASSIGN(document->dom_, Json::parse(values->rawJson));
    document->localIp_ = *values->virtualIp;
    document->cidr_ = values->vpnCidr;
    return document;
}
std::optional<std::string_view> ContractsDocument::existingKey(Ipv4Address remoteIp) const {
    const auto* contracts = dom_.find("communicationContracts");
    const auto* contract = contracts ? contracts->find(remoteIp.toString()) : nullptr;
    if (!contract) return std::nullopt;
    return contract->find("payloadKey")->stringValue();
}
Result<std::shared_ptr<const ContractsCandidate>> ContractsDocument::buildCandidate(
    std::span<const ContractEdit> rows, const ContractsEnvelopeSizer& envelopeSizer) const {
    const auto count = checkedContractPeerCount(cidr_, localIp_);
    if (!count) return count.error();
    if (rows.size() != *count || !envelopeSizer) return Error(ErrorCode::invalid_argument);
    std::array<bool, kMaxContractRows + 1> seen{};
    std::size_t total = 0;
    for (const auto& row : rows) {
        if (row.remoteIp == localIp_ || !cidr_.isValidClientAddress(row.remoteIp))
            return Error(ErrorCode::config_invalid_address);
        const auto offset = row.remoteIp.value() - cidr_.firstClientAddress().value();
        if (seen[offset]) return Error(ErrorCode::invalid_argument);
        seen[offset] = true;
        PHPVPN_TRY_ASSIGN(total, checkedContractKeyTextSize(total, 0, row.keyText.size()));
        if (row.enabled && !isValidContractKey(row.keyText))
            return Error(ErrorCode::config_invalid_key, std::string(kContractKeyError));
    }
    auto candidate = std::shared_ptr<ContractsCandidate>(new ContractsCandidate);
    candidate->dom_ = dom_; // Exactly one DOM copy per candidate, none per row.
    JsonValue* contracts = nullptr;
    for (auto& member : candidate->dom_.objectValue())
        if (member.first == "communicationContracts") contracts = &member.second;
    const bool anyEnabled = std::any_of(rows.begin(), rows.end(), [](const auto& row) { return row.enabled; });
    if (!contracts && anyEnabled) {
        candidate->dom_.objectValue().emplace_back("communicationContracts", JsonValue::makeObject({}));
        contracts = &candidate->dom_.objectValue().back().second;
    }
    if (contracts) {
        // Replace only the closed-schema contract objects, preserving all other
        // root/master/runtime values and the presence of this object itself.
        wipeDom(*contracts);
        contracts->objectValue().clear();
        for (const auto& row : rows) if (row.enabled)
            contracts->objectValue().emplace_back(row.remoteIp.toString(),
                JsonValue::makeObject({{"payloadKey", JsonValue::makeString(std::string(row.keyText))}}));
    }
    auto text = Json::serialize(candidate->dom_);
    TextWiper wipe{text};
    if (text.size() > kMaxConfigFileBytes) return Error(ErrorCode::config_too_large);
    ConfigWarnings warnings;
    auto validated = validateConfigValues(text, warnings, false);
    if (!validated) return validated.error();
    ValuesWiper wipeValidated{*validated};
    auto size = envelopeSizer(text);
    if (!size) return size.error();
    PHPVPN_TRY(validateContractsEnvelopeSize(*size));
    candidate->text_.assign(asBytes(text));
    return std::shared_ptr<const ContractsCandidate>(std::move(candidate));
}
Result<bool> ContractsCandidate::matchesDocument(std::string_view currentDocument) const {
    auto parsed = ContractsDocument::parse(currentDocument);
    if (!parsed) return parsed.error();
    return equalDom(dom_, parsed.value()->dom());
}
} // namespace phpvpn
