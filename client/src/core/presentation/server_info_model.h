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
    std::size_t onlineClients = 0;
    std::size_t validOnlineClients = 0;
};
std::string serverInfoCountText(const ServerInfoModel& model);
const char *serverClientStateText(ServerClientState state);
Result<ServerInfoModel> buildServerInfoModel(ServerInfoSnapshot snapshot);
// server_version_incompatible for an IPC error envelope with that code, so the
// window can name the cause; any other envelope is ipc_malformed.
Result<ServerInfoSnapshot> parseServerInfoResult(std::string_view response);
inline constexpr std::string_view kServerInfoIncompatibleServer =
    "The server uses a different major version than this client (" PHPVPN_CLIENT_VERSION
    "). Server information cannot be loaded.";
} // namespace phpvpn
