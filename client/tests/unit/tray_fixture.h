#pragma once
#include "core/protocol/json.h"
#include <string>

namespace phpvpn::tray::test {
inline constexpr const char* kStatusFixture = R"({
 "ipcVersion":1,"requestId":"3f2a7c1e-9b44-4b0e-8d2f-5c6a1e7b9d03",
 "status":{"state":"connecting","phase":"health","stateChangedAt":"2026-09-04T18:52:31.412Z",
 "connectionStartedAt":null,"serverUrlCheck":"pending","serverSecretCheck":"pending","accessKeyCheck":"pending",
 "serverReachable":"unknown","serverVerified":"unknown","tunnelReady":"unknown","lastError":null,
 "recoveryCount":0,"dataPollsTotal":3,"dataPollsEmpty":2,"dataPollsWithPayload":1,
 "serializedDataRequestBodyBytes":456,"serializedDataResponseBodyBytes":123,"ipv4PacketsSent":1,
 "ipv4PacketsReceived":2,"ipv4BytesSent":40,"ipv4BytesReceived":80,"clientVersion":"1.0.0","ipcVersion":1}})";
inline JsonValue fixture() { return Json::parse(kStatusFixture).value(); }
inline JsonValue& member(JsonValue& object, std::string_view key) {
    for (auto& entry : object.objectValue()) if (entry.first == key) return entry.second;
    throw std::runtime_error("fixture member missing");
}
inline JsonValue& status(JsonValue& document) { return member(document, "status"); }
}  // namespace phpvpn::tray::test
