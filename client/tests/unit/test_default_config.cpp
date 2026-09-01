#include "core/config/config.h"
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

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void writeExistingFile(const std::filesystem::path& path, std::string_view content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
}

}  // namespace

PHPVPN_TEST(default_config, missing_file_is_created_and_loaded) {
    TemporaryDirectory directory;
    const std::filesystem::path path = directory.path() / "config.json";

    PHPVPN_REQUIRE_OK(ensureDefaultConfigFile(path));
    const std::string stored = readFile(path);
    PHPVPN_CHECK_EQ(stored, std::string(kDefaultConfigDocument));

    ConfigWarnings warnings;
    auto loaded = parseAndValidateConfig(stored, warnings);
    PHPVPN_REQUIRE_OK(loaded);
    PHPVPN_CHECK_EQ(loaded.value()->rawJson, stored);
    PHPVPN_CHECK_EQ(loaded.value()->serverUrl.original,
                    std::string("https://MODIFY-THIS/phpvpn.php"));
    PHPVPN_CHECK_EQ(loaded.value()->contracts.size(), std::size_t(0));

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
    VpnEngine engine(*crypto, *transport, *clock, *tunnel, *netConfig, std::move(paths));
    PHPVPN_REQUIRE_OK(engine.start());

    auto storedConfig = engine.storedConfigJson();
    PHPVPN_REQUIRE_OK(storedConfig);
    PHPVPN_CHECK_EQ(storedConfig.value(), stored);
    engine.shutdown();
}

PHPVPN_TEST(default_config, existing_file_is_never_replaced) {
    TemporaryDirectory directory;
    const std::filesystem::path path = directory.path() / "config.json";
    const std::string existing = "this existing document is intentionally invalid\n";
    writeExistingFile(path, existing);

    PHPVPN_REQUIRE_OK(ensureDefaultConfigFile(path));
    PHPVPN_CHECK_EQ(readFile(path), existing);
}

PHPVPN_TEST(default_config, document_passes_the_current_validator) {
    ConfigWarnings warnings;
    auto parsed = parseAndValidateConfig(kDefaultConfigDocument, warnings);
    PHPVPN_REQUIRE_OK(parsed);

    PHPVPN_CHECK(parsed.value()->trustAnyTlsCertificate);
    PHPVPN_CHECK_EQ(parsed.value()->vpnCidr.toString(), std::string("10.99.99.0/24"));
    PHPVPN_CHECK_EQ(parsed.value()->virtualIp.toString(), std::string("10.99.99.2"));
    PHPVPN_CHECK_EQ(parsed.value()->serverSecret,
                    std::string(64, '0'));
    PHPVPN_CHECK_EQ(parsed.value()->accessKey.size(), std::size_t(32));
}

}  // namespace phpvpn
