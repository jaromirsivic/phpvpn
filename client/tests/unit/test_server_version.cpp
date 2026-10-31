#include "harness.h"

#include <string>

#include "core/ipc/ipc_protocol.h"
#include "core/ipc/wizard_ipc.h"
#include "core/presentation/server_info_model.h"
#include "core/protocol/http_headers.h"
#include "core/protocol/json.h"
#include "core/protocol/server_version.h"
#include "core/runtime/status_snapshot.h"
#include "../../apps/tray/progress_model.h"

namespace {
using namespace phpvpn;
constexpr char kRequestId[] = "3f2a7c1e-9b44-4b0e-8d2f-5c6a1e7b9d03";

std::string otherMajor(int delta) {
    return std::to_string(static_cast<long long>(*semverMajor(PHPVPN_CLIENT_VERSION)) + delta) + ".0.0";
}
}  // namespace

PHPVPN_TEST(ServerVersion, MajorOfSemVerStrings) {
    const struct { const char* text; std::uint32_t major; } valid[]{
        {"4.8.3", 4}, {"4.1.5", 4}, {"0.9.1", 0}, {"1", 1}, {"1.0", 1}, {"1.0.0-rc.1+build.5", 1},
        {"10.0.0", 10}, {"2-beta", 2}, {"3+meta", 3}, {"123456789.0.0", 123456789}};
    for (const auto& item : valid) {
        PHPVPN_CHECK(semverMajor(item.text).has_value());
        PHPVPN_CHECK_EQ(semverMajor(item.text).value_or(0xffffffffu), item.major);
    }
    for (const char* text : {"", "v1.0.0", "01.0.0", "00", " 1.0.0", "1 .0", "1x", ".1", "-1.0.0", "1234567890.0.0",
                             "\xc2\xb9.0.0", "1\n", "test"})
        PHPVPN_CHECK(!semverMajor(text));
    static_assert(semverMajor("7.2.1") == 7u);
}

PHPVPN_TEST(ServerVersion, OnlyTheMajorVersionIsCompared) {
    PHPVPN_CHECK(requireCompatibleServerVersion("4.1.5", "4.8.3").ok());
    PHPVPN_CHECK(requireCompatibleServerVersion("4.8.3", "4.8.3").ok());
    PHPVPN_CHECK(requireCompatibleServerVersion("4.0.0-rc.1", "4.8.3").ok());
    for (const char* server : {"3.4.1", "5.1.1", "40.8.3", "0.4.1", "", "unknown", "v4.8.3"}) {
        const auto result = requireCompatibleServerVersion(server, "4.8.3");
        PHPVPN_CHECK(!result.ok());
        PHPVPN_CHECK_EQ(result.code(), ErrorCode::server_version_incompatible);
    }
    PHPVPN_CHECK_EQ(requireCompatibleServerVersion("5.1.1", "4.8.3").error().detail, std::string("5.1.1"));
    // The default compares with this client build.
    PHPVPN_CHECK(requireCompatibleServerVersion(PHPVPN_CLIENT_VERSION).ok());
    PHPVPN_CHECK(requireCompatibleServerVersion(otherMajor(1)).code() == ErrorCode::server_version_incompatible);
    if (*semverMajor(PHPVPN_CLIENT_VERSION) > 0)
        PHPVPN_CHECK(requireCompatibleServerVersion(otherMajor(-1)).code() == ErrorCode::server_version_incompatible);
}

PHPVPN_TEST(ServerVersion, OnlySafeServerVersionsAreDisplayed) {
    PHPVPN_CHECK_EQ(displayableServerVersion("2.0.1-rc.1+b5"), std::string("2.0.1-rc.1+b5"));
    PHPVPN_CHECK_EQ(displayableServerVersion(std::string(32, '9')), std::string(32, '9'));
    for (const std::string text : {std::string(), std::string(33, '9'), std::string("2.0 <b>"), std::string("2.0\n1"),
                                   std::string("2.0\x1b[31m"), std::string("2.0/../x"), std::string("2.0\0x", 5)})
        PHPVPN_CHECK_EQ(displayableServerVersion(text), std::string("unrecognised"));
    const auto forged = requireCompatibleServerVersion("9.0.0\r\nInjected: text", "1.0.0");
    PHPVPN_CHECK_EQ(forged.error().detail, std::string("unrecognised"));
    PHPVPN_CHECK_EQ(serverVersionMismatchText(forged.error()),
                    std::string("Client version " PHPVPN_CLIENT_VERSION ", server version unrecognised."));
    PHPVPN_CHECK_EQ(serverVersionMismatchText(Error(ErrorCode::server_version_incompatible, "2.0.0")),
                    std::string("Client version " PHPVPN_CLIENT_VERSION ", server version 2.0.0."));
}

PHPVPN_TEST(ServerVersion, ErrorIsPermanentAndNotTransient) {
    const Error error(ErrorCode::server_version_incompatible, "2.0.0");
    PHPVPN_CHECK_EQ(std::string(error.name()), std::string("server_version_incompatible"));
    PHPVPN_CHECK(!error.recoverable());
    PHPVPN_CHECK(!error.transient());
    PHPVPN_CHECK_EQ(std::string(errorCodeDescription(error.code)),
                    std::string("The server uses a different major version than this client."));
    // The curated IPC description names both versions, nothing else of the detail.
    PHPVPN_CHECK_EQ(httpHeadersErrorDescription(error),
                    std::string("The server uses a different major version than this client. Client version "
                                PHPVPN_CLIENT_VERSION ", server version 2.0.0."));
    const auto envelope = Json::parse(IpcProtocol::buildError(kRequestId, error));
    PHPVPN_REQUIRE_OK(envelope);
    const auto* payload = envelope->find("error");
    PHPVPN_CHECK(payload != nullptr);
    if (payload == nullptr) return;
    PHPVPN_CHECK_EQ(payload->find("code")->stringValue(), std::string("server_version_incompatible"));
    PHPVPN_CHECK(payload->find("description")->stringValue().find("server version 2.0.0") != std::string::npos);
    PHPVPN_CHECK(!payload->find("recoverable")->boolValue());
}

PHPVPN_TEST(ServerVersion, ConnectionErrorKeepsItsOwnCodeInEveryPhase) {
    const Error error(ErrorCode::server_version_incompatible, "2.0.0");
    for (const auto phase : {ConnectionPhase::idle, ConnectionPhase::health, ConnectionPhase::info,
                             ConnectionPhase::access_key_check, ConnectionPhase::communication_contracts_check,
                             ConnectionPhase::running, ConnectionPhase::reconnecting}) {
        for (const bool syntaxUsable : {false, true}) {
            const auto safe = SafeError::fromConnection(error, phase, syntaxUsable);
            PHPVPN_CHECK_EQ(safe.code, std::string("server_version_incompatible"));
            PHPVPN_CHECK_EQ(safe.description, std::string("The server uses a different major version than this client."));
            PHPVPN_CHECK(!safe.recoverable);
        }
    }
    PHPVPN_CHECK_EQ(SafeError::from(error).code, std::string("server_version_incompatible"));
}

PHPVPN_TEST(ServerVersion, WizardErrorNamesBothVersionsAndTheRemedy) {
    for (const char* field : {"serverUrl", "serverSecret", "accessKey"}) {
        const auto error = WizardIpc::safeError(Error(ErrorCode::server_version_incompatible, "5.1.1"), std::nullopt,
                                                field, std::string("https://vpn.example.test/phpvpn.php"));
        PHPVPN_CHECK_EQ(error.code, std::string("server_version_incompatible"));
        PHPVPN_CHECK(!error.recoverable);
        PHPVPN_CHECK_EQ(*error.field, std::string(field));
        PHPVPN_CHECK(!error.httpStatus);
        PHPVPN_CHECK(error.description.find("Client version " PHPVPN_CLIENT_VERSION ", server version 5.1.1.") !=
                     std::string::npos);
        PHPVPN_CHECK_EQ(error.suggestedAction,
                        std::string("The server is reachable, but this client cannot communicate with it. Install a "
                                    "PHPVPN client and server with the same major version."));
        const auto parsed = WizardIpc::parseError(WizardIpc::errorValue(error));
        PHPVPN_REQUIRE_OK(parsed);
        PHPVPN_CHECK_EQ(parsed->description, error.description);
        PHPVPN_CHECK_EQ(parsed->suggestedAction, error.suggestedAction);
    }
}

PHPVPN_TEST(ServerVersion, ConnectProgressHasADedicatedPanel) {
    const auto& text = tray::progressErrorText("server_version_incompatible");
    PHPVPN_CHECK_EQ(text.code, std::string_view("server_version_incompatible"));
    PHPVPN_CHECK_EQ(text.panel, std::string_view("Incompatible server version"));
    PHPVPN_CHECK(text.action.find("The server is reachable, but this client cannot communicate with it.") == 0);
    PHPVPN_CHECK(text.action.find("same major version") != std::string_view::npos);
    PHPVPN_CHECK(text.action.find("This client is version " PHPVPN_CLIENT_VERSION ".") != std::string_view::npos);
    // The fallback for unknown codes is unchanged.
    PHPVPN_CHECK_EQ(tray::progressErrorText("not_a_code").code, std::string_view("communication_failed"));
}

PHPVPN_TEST(ServerVersion, ServerInfoNamesAnIncompatibleServer) {
    const auto incompatible = parseServerInfoResult(
        IpcProtocol::buildError(kRequestId, Error(ErrorCode::server_version_incompatible, "2.0.0")));
    PHPVPN_CHECK_EQ(incompatible.code(), ErrorCode::server_version_incompatible);
    PHPVPN_CHECK_EQ(parseServerInfoResult(IpcProtocol::buildError(kRequestId, Error(ErrorCode::busy))).code(),
                    ErrorCode::ipc_malformed);
    PHPVPN_CHECK(kServerInfoIncompatibleServer.find("(" PHPVPN_CLIENT_VERSION ")") != std::string_view::npos);
}
