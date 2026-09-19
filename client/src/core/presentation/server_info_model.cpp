#include "core/presentation/server_info_model.h"
#include "core/crypto/masked_contracts.h"
#include "core/ipc/ipc_protocol.h"
#include "core/protocol/iso8601.h"
#include "core/protocol/uuid.h"
#include <algorithm>
namespace phpvpn {
const char *serverClientStateText(ServerClientState state) {
    switch (state) {
    case ServerClientState::valid:
        return "Online - valid communication contract";
    case ServerClientState::invalid:
        return "Online - invalid communication contract";
    default:
        return "Offline";
    }
}
Result<ServerInfoModel> buildServerInfoModel(ServerInfoSnapshot snapshot) {
    auto local =
        parseMaskedContracts(maskedContractsJson(snapshot.localMaskedCommunicationContracts), snapshot.localAddress);
    if (!local.ok())
        return local.error();
    auto info = ResponseParser::parseInfoPlaintext(snapshot.infoPlaintext, snapshot.localAddress);
    if (!info.ok())
        return info.error();
    const auto generatedAt = *Iso8601::parse(info->generatedAt);
    ServerInfoModel model;
    for (const auto &client : info->clients) {
        if (client.address == snapshot.localAddress)
            continue;
        ServerClientState state = ServerClientState::offline;
        if (generatedAt - client.lastSeenMs <= 60000) {
            ++model.onlineClients;
            state = ServerClientState::invalid;
            const auto found =
                std::find_if(local->begin(), local->end(), [&](const auto &c) { return c.peer == client.address; });
            if (found != local->end() && !client.maskedCommunicationContracts.empty() &&
                found->hash == client.maskedCommunicationContracts.front().hash) {
                state = ServerClientState::valid;
                ++model.validOnlineClients;
            }
        }
        model.rows.push_back(
            {client.address, canonicalContractId(snapshot.localAddress, client.address), client.lastSeenMs, state});
    }
    std::sort(model.rows.begin(), model.rows.end(),
              [](const auto &a, const auto &b) { return a.state != b.state ? a.state < b.state : a.peer < b.peer; });
    model.snapshot = std::move(snapshot);
    return model;
}
std::string serverInfoCountText(const ServerInfoModel& model) {
    return "(" + std::to_string(model.onlineClients) +
        (model.onlineClients == 1 ? " online client, " : " online clients, ") +
        std::to_string(model.validOnlineClients) +
        (model.validOnlineClients == 1 ? " valid contract)" : " valid contracts)");
}
Result<ServerInfoSnapshot> parseServerInfoResult(std::string_view response) {
    if (response.size() > kIpcMaxMessageBytes)
        return Error(ErrorCode::ipc_message_too_large);
    auto root = Json::parse(response);
    if (!root || !root->isObject() || root->objectValue().size() != 3)
        return Error(ErrorCode::ipc_malformed);
    const auto *version = root->find("ipcVersion");
    const auto *id = root->find("requestId");
    const auto *result = root->find("result");
    if (!version || !version->isInteger() || version->integerValue() != kIpcVersion || !id || !id->isString() ||
        !Uuid::isValidV4(id->stringValue()) || !result || !result->isObject() || result->objectValue().size() != 3)
        return Error(ErrorCode::ipc_malformed);
    const auto *text = result->find("infoPlaintext");
    const auto *address = result->find("localAddress");
    const auto *contracts = result->find("localMaskedCommunicationContracts");
    if (!text || !text->isString() || !address || !address->isString() || !contracts)
        return Error(ErrorCode::ipc_malformed);
    auto ip = Ipv4Address::parse(address->stringValue());
    if (!ip || !validInfoAddress(*ip))
        return Error(ErrorCode::ipc_malformed);
    auto local = parseMaskedContracts(*contracts, *ip);
    if (!local)
        return local.error();
    ServerInfoSnapshot snapshot{text->stringValue(), *ip, std::move(local).value()};
    auto checked = buildServerInfoModel(snapshot);
    if (!checked)
        return checked.error();
    return snapshot;
}
} // namespace phpvpn
