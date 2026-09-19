#include "core/ipc/ipc_protocol.h"
#include "core/presentation/server_info_model.h"
#include "core/presentation/table_pagination.h"
#include "core/protocol/iso8601.h"
#include "harness.h"
namespace phpvpn {
namespace {
ServerInfoSnapshot fixture(std::size_t count) {
    const auto src = *Ipv4Address::parse("10.99.99.2");
    const auto now = *Iso8601::parse("2026-09-13T12:00:00.000Z");
    JsonArray clients{JsonValue::makeObject({{"address", JsonValue::makeString(src.toString())},
                                             {"lastSeen", JsonValue::makeString(Iso8601::format(now))},
                                             {"maskedCommunicationContracts", JsonValue::makeArray({})}})};
    ServerInfoSnapshot snapshot;
    snapshot.localAddress = src;
    for (std::size_t i = 0; i < count; ++i) {
        const auto peer = Ipv4Address(src.value() + static_cast<std::uint32_t>(i) + 1);
        const auto age = i % 3 == 0 ? 60000 : i % 3 == 1 ? 59999 : 60001;
        std::vector<MaskedCommunicationContract> published{{src, std::string(64, 'a')}};
        clients.push_back(JsonValue::makeObject({{"address", JsonValue::makeString(peer.toString())},
                                                 {"lastSeen", JsonValue::makeString(Iso8601::format(now - age))},
                                                 {"maskedCommunicationContracts", maskedContractsJson(published)}}));
        snapshot.localMaskedCommunicationContracts.push_back({peer, std::string(64, i % 3 == 0 ? 'a' : 'b')});
    }
    snapshot.infoPlaintext = Json::serialize(JsonValue::makeObject(
        {{"serverVersion", JsonValue::makeString("test")},
         {"generatedAt", JsonValue::makeString(Iso8601::format(now))},
         {"clients", JsonValue::makeArray(std::move(clients))},
         {"limits", JsonValue::makeObject({{"maxDataRecordsPerRequest", JsonValue::makeInteger(16)},
                                           {"maxRequestBodyBytes", JsonValue::makeInteger(983040)},
                                           {"maxQueueBytesPerClient", JsonValue::makeInteger(16777216)},
                                           {"queueTtlSeconds", JsonValue::makeInteger(60)},
                                           {"minDataPollIntervalMs", JsonValue::makeInteger(50)},
                                           {"maxDataPollIntervalMs", JsonValue::makeInteger(3200)}})}}));
    return snapshot;
}
} // namespace
PHPVPN_TEST(ServerInfo, states_boundaries_count_sort_and_pages) {
    for (auto count : {0u, 1u, 20u, 21u, 141u, 252u}) {
        auto snapshot = fixture(count);
        auto model = buildServerInfoModel(snapshot);
        PHPVPN_REQUIRE_OK(model);
        PHPVPN_CHECK_EQ(model->rows.size(), static_cast<std::size_t>(count));
        PHPVPN_CHECK_EQ(model->validOnlineClients, static_cast<std::size_t>((count + 2) / 3));
        PHPVPN_CHECK_EQ(model->onlineClients, static_cast<std::size_t>(count - count / 3));
        PHPVPN_CHECK_EQ(model->snapshot.infoPlaintext, snapshot.infoPlaintext);
        for (std::size_t i = 1; i < model->rows.size(); ++i) {
            PHPVPN_CHECK(model->rows[i - 1].state <= model->rows[i].state);
            if (model->rows[i - 1].state == model->rows[i].state)
                PHPVPN_CHECK(model->rows[i - 1].peer < model->rows[i].peer);
        }
        PHPVPN_CHECK(tablePageNumbers(tablePageCount(count), 0).size() <= 7);
    }
    auto snapshot = fixture(3);
    auto model = buildServerInfoModel(snapshot);
    PHPVPN_REQUIRE_OK(model);
    PHPVPN_CHECK_EQ(model->rows[0].state, ServerClientState::valid);
    PHPVPN_CHECK_EQ(model->rows[1].state, ServerClientState::invalid);
    PHPVPN_CHECK_EQ(model->rows[2].state, ServerClientState::offline);
    snapshot.localMaskedCommunicationContracts[1].hash = std::string(64, 'a');
    auto refreshed = buildServerInfoModel(snapshot);
    PHPVPN_REQUIRE_OK(refreshed);
    PHPVPN_CHECK_EQ(refreshed->validOnlineClients, std::size_t(2));
    PHPVPN_CHECK_EQ(model->snapshot.infoPlaintext, refreshed->snapshot.infoPlaintext);
}
PHPVPN_TEST(ServerInfo, ipc_exact_schema_and_private_snapshot) {
    const auto id = "8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c30";
    auto snapshot = fixture(252);
    auto body = IpcProtocol::buildServerInfoResponse(id, snapshot);
    PHPVPN_CHECK(body.size() < kIpcMaxMessageBytes);
    auto parsed = parseServerInfoResult(body);
    PHPVPN_REQUIRE_OK(parsed);
    PHPVPN_CHECK_EQ(parsed->infoPlaintext, snapshot.infoPlaintext);
    auto command = IpcProtocol::parseRequest(IpcProtocol::buildRequest(IpcCommand::get_server_info, id));
    PHPVPN_REQUIRE_OK(command);
    PHPVPN_CHECK_EQ(command->command, IpcCommand::get_server_info);
    auto duplicate = snapshot;
    duplicate.localMaskedCommunicationContracts.push_back(duplicate.localMaskedCommunicationContracts.front());
    PHPVPN_CHECK(!parseServerInfoResult(IpcProtocol::buildServerInfoResponse(id, duplicate)));
    auto json = Json::parse(body).value();
    json.objectValue().push_back({"unexpected", JsonValue::makeBool(true)});
    PHPVPN_CHECK(!parseServerInfoResult(Json::serialize(json)));
}
PHPVPN_TEST(ServerInfo, future_time_missing_contract_and_exact_status_text) {
    auto snapshot = fixture(1);
    const auto oldTime = std::string("2026-09-13T11:59:00.000Z");
    const auto position = snapshot.infoPlaintext.find(oldTime);
    PHPVPN_CHECK(position != std::string::npos);
    snapshot.infoPlaintext.replace(position, oldTime.size(), "2026-09-13T12:01:00.000Z");
    auto model = buildServerInfoModel(snapshot);
    PHPVPN_REQUIRE_OK(model);
    PHPVPN_CHECK_EQ(model->validOnlineClients, std::size_t(1));
    snapshot.localMaskedCommunicationContracts.clear();
    model = buildServerInfoModel(snapshot);
    PHPVPN_REQUIRE_OK(model);
    PHPVPN_CHECK_EQ(model->rows.front().state, ServerClientState::invalid);
    PHPVPN_CHECK_EQ(model->onlineClients, std::size_t(1));
    PHPVPN_CHECK_EQ(model->validOnlineClients, std::size_t(0));
    PHPVPN_CHECK_EQ(serverInfoCountText(*model), "(1 online client, 0 valid contracts)");
    PHPVPN_CHECK_EQ(std::string(serverClientStateText(ServerClientState::valid)),
                    "Online - valid communication contract");
    PHPVPN_CHECK_EQ(std::string(serverClientStateText(ServerClientState::invalid)),
                    "Online - invalid communication contract");
    PHPVPN_CHECK_EQ(std::string(serverClientStateText(ServerClientState::offline)), "Offline");
}
PHPVPN_TEST(ServerInfo, independent_count_pluralization) {
    const struct { std::size_t online, valid; const char* text; } cases[]{
        {0,0,"(0 online clients, 0 valid contracts)"},
        {1,0,"(1 online client, 0 valid contracts)"},
        {1,1,"(1 online client, 1 valid contract)"},
        {4,0,"(4 online clients, 0 valid contracts)"},
        {4,1,"(4 online clients, 1 valid contract)"},
        {4,4,"(4 online clients, 4 valid contracts)"},
    };
    for (const auto& value : cases) {
        ServerInfoModel model;
        model.onlineClients = value.online; model.validOnlineClients = value.valid;
        PHPVPN_CHECK_EQ(serverInfoCountText(model), value.text);
    }
    const auto model = buildServerInfoModel(fixture(252));
    PHPVPN_REQUIRE_OK(model);
    PHPVPN_CHECK_EQ(serverInfoCountText(*model), "(168 online clients, 84 valid contracts)");
    const auto empty = buildServerInfoModel(fixture(0));
    PHPVPN_REQUIRE_OK(empty);
    PHPVPN_CHECK_EQ(serverInfoCountText(*empty), "(0 online clients, 0 valid contracts)");
}
} // namespace phpvpn
