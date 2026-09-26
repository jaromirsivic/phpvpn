#include "harness.h"
#include "../../apps/tray/tray_arguments.h"
#include <array>
#include <string>
#include <vector>

namespace {
using namespace phpvpn::tray;
}

PHPVPN_TEST(TrayArguments, WideArgumentsKeepExactMatchSemantics) {
    const wchar_t* ordinary[] = {L"phpvpn-tray"};
    const wchar_t* unrelated[] = {L"phpvpn-tray", L"--installation-complete-no", L"installation-complete"};
    const wchar_t* installed[] = {L"phpvpn-tray", L"--installation-complete"};
    const wchar_t* programOnly[] = {L"--installation-complete"};
    PHPVPN_CHECK(!requestsInstallationNotice(1, ordinary));
    PHPVPN_CHECK(!requestsInstallationNotice(3, unrelated));
    PHPVPN_CHECK(requestsInstallationNotice(2, installed));
    PHPVPN_CHECK(!requestsInstallationNotice(1, programOnly));
    PHPVPN_CHECK(!requestsInstallationNotice(0, installed));
}

PHPVPN_TEST(TrayArguments, Utf8ArgvMatchesWideSemantics) {
    const char* ordinary[] = {"/usr/bin/phpvpn-tray"};
    const char* unrelated[] = {"phpvpn-tray", "--installation-complete-no", "installation-complete",
        "--Installation-Complete", "--installation-complete ", "--installation-complete=1"};
    const char* installed[] = {"/usr/bin/phpvpn-tray", "--other", "--installation-complete"};
    const char* programOnly[] = {"--installation-complete"};
    PHPVPN_CHECK(!requestsInstallationNotice(1, ordinary));
    PHPVPN_CHECK(!requestsInstallationNotice(6, unrelated));
    PHPVPN_CHECK(requestsInstallationNotice(3, installed));
    // argc bounds the scan exactly like the wide variant.
    PHPVPN_CHECK(!requestsInstallationNotice(2, installed));
    PHPVPN_CHECK(!requestsInstallationNotice(1, programOnly));
    PHPVPN_CHECK(!requestsInstallationNotice(0, installed));
    // Mutable argv from main() converts without a cast.
    std::string program = "phpvpn-tray", flag = "--installation-complete";
    std::array<char*, 2> mutableArgv{program.data(), flag.data()};
    PHPVPN_CHECK(requestsInstallationNotice(2, mutableArgv.data()));
    // Non-ASCII UTF-8 path of the executable does not affect matching.
    const char* utf8Path[] = {"/home/u\xC5\xBEivatel/phpvpn-tray", "--installation-complete"};
    PHPVPN_CHECK(requestsInstallationNotice(2, utf8Path));
}

PHPVPN_TEST(TrayArguments, Utf8SpanMatchesArgvSemantics) {
    const std::vector<std::string_view> empty;
    const std::vector<std::string_view> ordinary{"phpvpn-tray"};
    const std::vector<std::string_view> programOnly{"--installation-complete"};
    const std::vector<std::string_view> unrelated{"phpvpn-tray", "--installation-complete-no", "installation-complete"};
    const std::vector<std::string_view> installed{"phpvpn-tray", "--installation-complete"};
    // An embedded NUL is not an exact match even when the prefix is.
    const std::vector<std::string_view> embedded{"phpvpn-tray", std::string_view("--installation-complete\0x", 25)};
    PHPVPN_CHECK(!requestsInstallationNotice(std::span<const std::string_view>(empty)));
    PHPVPN_CHECK(!requestsInstallationNotice(std::span<const std::string_view>(ordinary)));
    PHPVPN_CHECK(!requestsInstallationNotice(std::span<const std::string_view>(programOnly)));
    PHPVPN_CHECK(!requestsInstallationNotice(std::span<const std::string_view>(unrelated)));
    PHPVPN_CHECK(requestsInstallationNotice(std::span<const std::string_view>(installed)));
    PHPVPN_CHECK(!requestsInstallationNotice(std::span<const std::string_view>(embedded)));
}
