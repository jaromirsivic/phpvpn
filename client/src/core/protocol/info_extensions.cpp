#include "core/protocol/info_extensions.h"
#include <algorithm>
#include <set>
namespace phpvpn {
bool validMaskedContractHash(std::string_view hash) {
    return hash.size() == 64 && std::all_of(hash.begin(), hash.end(),
                                            [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
bool validInfoAddress(Ipv4Address address) {
    const auto host = address.value() & 255;
    return host >= 2 && host <= 254;
}
JsonValue maskedContractsJson(const std::vector<MaskedCommunicationContract> &contracts) {
    JsonArray values;
    for (const auto &c : contracts)
        values.push_back(JsonValue::makeObject(
            {{"peer", JsonValue::makeString(c.peer.toString())}, {"hash", JsonValue::makeString(c.hash)}}));
    return JsonValue::makeArray(std::move(values));
}
Result<std::vector<MaskedCommunicationContract>> parseMaskedContracts(const JsonValue &value, Ipv4Address owner) {
    if (!value.isArray() || value.arrayValue().size() > kMaxMaskedCommunicationContracts)
        return Error(ErrorCode::response_invalid);
    std::vector<MaskedCommunicationContract> result;
    std::set<std::uint32_t> seen;
    for (const auto &item : value.arrayValue()) {
        const auto *peer = item.find("peer");
        const auto *hash = item.find("hash");
        if (!item.isObject() || item.objectValue().size() != 2 || !peer || !peer->isString() || !hash ||
            !hash->isString() || !validMaskedContractHash(hash->stringValue()))
            return Error(ErrorCode::response_invalid);
        auto ip = Ipv4Address::parse(peer->stringValue());
        if (!ip || !validInfoAddress(*ip) || *ip == owner || (ip->value() >> 8) != (owner.value() >> 8) ||
            !seen.insert(ip->value()).second)
            return Error(ErrorCode::response_invalid);
        result.push_back({*ip, hash->stringValue()});
    }
    return result;
}
Result<std::string> canonicalInfoExtensions(const InfoRequestOptions &options, Ipv4Address src) {
    if (!validInfoAddress(src) ||
        (!options.maskedCommunicationContracts && !options.includeMaskedCommunicationContracts))
        return Error(ErrorCode::response_invalid);
    JsonObject fields;
    if (options.maskedCommunicationContracts) {
        auto value = maskedContractsJson(*options.maskedCommunicationContracts);
        auto checked = parseMaskedContracts(value, src);
        if (!checked.ok())
            return checked.error();
        fields.emplace_back("maskedCommunicationContracts", std::move(value));
    }
    if (options.includeMaskedCommunicationContracts)
        fields.emplace_back("includeMaskedCommunicationContracts",
                            JsonValue::makeBool(*options.includeMaskedCommunicationContracts));
    auto json = Json::serialize(JsonValue::makeObject(std::move(fields)));
    if (json.size() > kMaxInfoRequestBodyBytes)
        return Error(ErrorCode::response_invalid);
    return json;
}
std::string infoFullCanonical(std::string_view src, std::string_view timestamp, std::string_view requestId,
                              std::string_view hash) {
    return "PHPVPN-AUTH-FULL\nPOST\ninfo\n" + std::string(src) + "\n" + std::string(timestamp) + "\n" +
           std::string(requestId) + "\n" + std::string(hash);
}
} // namespace phpvpn
