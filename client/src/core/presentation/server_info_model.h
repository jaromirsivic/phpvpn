#pragma once
#include "core/protocol/response_parser.h"
namespace phpvpn {
enum class ServerClientState { valid, invalid, offline };
struct ServerInfoRow {
    Ipv4Address peer;
    std::string contract;
    std::int64_t lastSeenMs;
    ServerClientState state;
};
struct ServerInfoModel {
    ServerInfoSnapshot snapshot;
    std::vector<ServerInfoRow> rows;
    std::size_t validOnlineClients = 0;
};
const char *serverClientStateText(ServerClientState state);
Result<ServerInfoModel> buildServerInfoModel(ServerInfoSnapshot snapshot);
Result<ServerInfoSnapshot> parseServerInfoResult(std::string_view response);
} // namespace phpvpn
