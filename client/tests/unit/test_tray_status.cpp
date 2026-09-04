#include "harness.h"
#include "tray_fixture.h"
#include "../../apps/tray/status_model.h"
#include <algorithm>

namespace {
using namespace phpvpn;
using namespace phpvpn::tray;
using namespace phpvpn::tray::test;
}

PHPVPN_TEST(TrayStatus, ParsesWholeFixtureAndReplacesOnlyValidSnapshots) {
    auto current = parseStatusResponse(kStatusFixture);
    PHPVPN_CHECK(current.valid);
    PHPVPN_CHECK_EQ(current.counters.dataPollsTotal, 3U);
    PHPVPN_CHECK_EQ(current.counters.serializedDataRequestBodyBytes, 456U);
    for (const char* text : {"", "{}", "null", "[]", "{", R"({"error":{"description":"unavailable"}})"}) {
        const auto invalid = parseStatusResponse(text);
        PHPVPN_CHECK(!invalid.valid);
        PHPVPN_CHECK(!applyStatusSnapshot(current, invalid));
        PHPVPN_CHECK_EQ(current.counters.dataPollsTotal, 3U);
    }
}

PHPVPN_TEST(TrayStatus, EveryRequiredMemberIsRequiredAndUnknownMembersAreRejected) {
    auto document = fixture();
    const auto names = status(document).objectValue();
    for (const auto& entry : names) {
        auto missing = fixture();
        auto& members = status(missing).objectValue();
        std::erase_if(members, [&](const auto& item) { return item.first == entry.first; });
        PHPVPN_CHECK(!parseStatusResponse(Json::serialize(missing)).valid);
    }
    for (const char* old : {"packetsSent", "packetsReceived", "serverUrl", "configuration"}) {
        auto extra = fixture();
        status(extra).objectValue().emplace_back(old, JsonValue::makeInteger(0));
        PHPVPN_CHECK(!parseStatusResponse(Json::serialize(extra)).valid);
    }
}

PHPVPN_TEST(TrayStatus, RejectsNegativeFractionalStringNullAndOverflowCounters) {
    const JsonValue invalid[]{JsonValue::makeInteger(-1), JsonValue::makeReal(0.5),
                              JsonValue::makeString("9"), JsonValue::makeNull(), JsonValue::makeBool(true)};
    for (const auto& field : kCounterFields) {
        for (const auto& value : invalid) {
            auto document = fixture();
            member(status(document), field.key) = value;
            PHPVPN_CHECK(!parseStatusResponse(Json::serialize(document)).valid);
        }
    }
    std::string oversized(kStatusFixture);
    const auto at = oversized.find("456");
    oversized.replace(at, 3, "18446744073709551616");
    PHPVPN_CHECK(!parseStatusResponse(oversized).valid);
    auto inconsistent = fixture();
    member(status(inconsistent), "dataPollsTotal") = JsonValue::makeInteger(4);
    PHPVPN_CHECK(!parseStatusResponse(Json::serialize(inconsistent)).valid);
}

PHPVPN_TEST(TrayStatus, RejectsInvalidEnumsVersionsAndMalformedError) {
    for (const char* key : {"state", "phase", "serverUrlCheck", "serverSecretCheck", "accessKeyCheck",
                            "serverReachable", "serverVerified", "tunnelReady"}) {
        auto document = fixture();
        member(status(document), key) = JsonValue::makeString("not-a-valid-value");
        PHPVPN_CHECK(!parseStatusResponse(Json::serialize(document)).valid);
    }
    auto document = fixture();
    member(document, "requestId") = JsonValue::makeString("not-an-uuid");
    PHPVPN_CHECK(!parseStatusResponse(Json::serialize(document)).valid);
    document = fixture();
    member(status(document), "stateChangedAt") = JsonValue::makeString("not-a-time");
    PHPVPN_CHECK(!parseStatusResponse(Json::serialize(document)).valid);
    document = fixture();
    member(document, "ipcVersion") = JsonValue::makeInteger(2);
    PHPVPN_CHECK(!parseStatusResponse(Json::serialize(document)).valid);
    document = fixture();
    member(status(document), "lastError") = JsonValue::makeObject({{"code", JsonValue::makeString("communication_failed")}});
    PHPVPN_CHECK(!parseStatusResponse(Json::serialize(document)).valid);
    PHPVPN_CHECK(!parseStatusResponse(R"({"ipcVersion":1,"ipcVersion":1})").valid);
}

PHPVPN_TEST(TrayStatus, PreservesPublicServiceErrorWithoutCheckpointInference) {
    auto document = fixture();
    member(status(document), "lastError") = JsonValue::makeObject({
        {"code", JsonValue::makeString("communication_timeout")},
        {"description", JsonValue::makeString("The server did not respond in time. Verification could not be completed.")},
        {"recoverable", JsonValue::makeBool(true)}});
    const auto parsed = parseStatusResponse(Json::serialize(document));
    PHPVPN_CHECK(parsed.valid);
    PHPVPN_CHECK(parsed.hasError);
    PHPVPN_CHECK_EQ(parsed.errorCode, "communication_timeout");
    PHPVPN_CHECK_EQ(parsed.errorDescription, "The server did not respond in time. Verification could not be completed.");
    PHPVPN_CHECK(parsed.errorRecoverable);
}
