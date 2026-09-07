#include "core/config/config.h"
#include "fake_config_files.h"
#include "core/runtime/vpn_engine.h"
#include "harness.h"
#include "platform/windows/paths_win.h"

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace phpvpn {
namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        static std::atomic<unsigned long> sequence{0};
        path_ = std::filesystem::temp_directory_path() /
                (L"phpvpn-default-config-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                 std::to_wstring(sequence.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace

PHPVPN_TEST(default_config, engine_start_and_connect_do_not_create_missing_file) {
    TemporaryDirectory directory;
    const std::filesystem::path path = directory.path() / "config.json";

    auto cryptoResult = createPlatformCryptoProvider();
    PHPVPN_REQUIRE_OK(cryptoResult);
    auto transportResult = createPlatformTransport();
    PHPVPN_REQUIRE_OK(transportResult);
    auto crypto = std::move(cryptoResult).value();
    auto transport = std::move(transportResult).value();
    auto clock = createPlatformClock();
    auto tunnel = createPlatformTunnelDriver();
    auto netConfig = createPlatformNetConfig();

    EnginePaths paths;
    paths.configFile = path;
    paths.journalFile = directory.path() / "ownership.json";
    paths.installDirectory = directory.path().wstring();
    auto backend = std::make_unique<FakeConfigFiles>(*crypto);
    auto* files = backend.get();
    ConfigStore store(*crypto, std::move(backend));
    VpnEngine engine(*crypto, *transport, *clock, *tunnel, *netConfig, std::move(paths), store);
    PHPVPN_REQUIRE_OK(engine.start());

    PHPVPN_CHECK_EQ(files->reads, 0u);
    PHPVPN_REQUIRE_ERROR(engine.connect(), ErrorCode::config_not_found);
    PHPVPN_CHECK_EQ(files->writes, 0u);
    PHPVPN_REQUIRE_ERROR(engine.storedConfigJson(), ErrorCode::config_not_found);
    PHPVPN_CHECK(!std::filesystem::exists(path));
    engine.shutdown();
}

PHPVPN_TEST(default_config, document_passes_the_current_validator) {
    ConfigWarnings warnings;
    auto parsed = parseAndValidateConfig(kDefaultConfigDocument, warnings);
    PHPVPN_REQUIRE_OK(parsed);

    PHPVPN_CHECK(parsed.value()->trustAnyTlsCertificate);
    PHPVPN_CHECK_EQ(parsed.value()->vpnCidr.toString(), std::string("10.99.99.0/24"));
    PHPVPN_CHECK_EQ(parsed.value()->virtualIp.toString(), std::string("10.99.99.2"));
    PHPVPN_CHECK_EQ(parsed.value()->serverSecret,
                    std::string("CHANGE_THIS_SERVER_SECRET"));
    PHPVPN_CHECK_EQ(parsed.value()->accessKey.size(), std::size_t(32));
}

}  // namespace phpvpn
