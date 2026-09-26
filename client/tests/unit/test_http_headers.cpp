#include "harness.h"
#include "core/protocol/http_headers.h"
#include "core/config/config_defaults.h"
#include "core/ipc/wizard_ipc.h"
#include "fake_config_files.h"
#include <algorithm>

namespace phpvpn {
namespace {
JsonValue& member(JsonValue& object, std::string_view name) {
    for (auto& [key, value] : object.objectValue()) if (key == name) return value;
    throw std::runtime_error("missing test member");
}
std::string withHeaders(JsonValue value, bool repair = false) {
    auto dom = Json::parse(kDefaultConfigDocument).value();
    auto& master = member(dom, "masterSetup");
    member(master, "httpHeaders") = std::move(value);
    if (repair) std::erase_if(master.objectValue(), [](const auto& item) { return item.first == "accessKey" || item.first == "virtualIp"; });
    return Json::serialize(dom);
}
}
PHPVPN_TEST(http_headers_config, positive_syntax_normalization_and_raw_preservation) {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"X: value", "X: value"}, {"X:", "X:"}, {"X: \t a  \t", "X: \t a  \t"},
        {"X: a\nY: b\n", "X: a\r\nY: b"}, {"X: a\r\nY: b\r\n", "X: a\r\nY: b"},
        {"X: first\nY: second\nZ: first", "X: first\r\nY: second\r\nZ: first"},
        {"!#$%&'*+-.^_`|~09AZaz: \"a\\b:c\"", "!#$%&'*+-.^_`|~09AZaz: \"a\\b:c\""},
        {"X: \xc5\xbe\xc3\xa1\xf0\x9f\x98\x80", "X: \xc5\xbe\xc3\xa1\xf0\x9f\x98\x80"},
        {"Cookie: a=1\nCache-Control: no-cache\nPragma: no-cache\nUser-Agent: custom", "Cookie: a=1\r\nCache-Control: no-cache\r\nPragma: no-cache\r\nUser-Agent: custom"}
    };
    for (const auto& [input, normalized] : cases) {
        auto result = normalizeHttpHeaders(input);
        PHPVPN_REQUIRE_OK(result);
        PHPVPN_CHECK_EQ(*result, normalized);
        PHPVPN_CHECK_EQ(buildApplicationRequestHeaders("application/json", *result), "Content-Type: application/json\r\n" + normalized);
        for (bool repair : {false, true}) {
            const auto text = withHeaders(JsonValue::makeString(input), repair);
            ConfigWarnings warnings;
            auto parsed = validateConfigValues(text, warnings, repair);
            PHPVPN_REQUIRE_OK(parsed);
            PHPVPN_CHECK_EQ(parsed->httpHeaders, input);
            PHPVPN_CHECK_EQ(parsed->rawJson, text);
            auto wizard = analyzeWizardDocument(text);
            PHPVPN_REQUIRE_OK(wizard);
            PHPVPN_CHECK_EQ(wizard->context.httpHeaders, input);
        }
    }
}
PHPVPN_TEST(http_headers_config, rejects_invalid_syntax_in_full_and_repair_without_values_in_errors) {
    std::vector<std::string> cases = {"", " ", "\t", "\n", "\r\n", "X", ": secret-marker", "X : secret-marker",
        " X: secret-marker", "\tX: secret-marker", "\xc3\xa9: secret-marker", "X: secret-marker\r", "X: a\rb",
        "\nX: secret-marker", "X: secret-marker\n\n", "X: a\r\n\r\n", "X: a\n\nY: b",
        "GET / HTTP/1.1", "HTTP/1.1 200 OK", "X: secret-marker\r\n\r\nbody"};
    for (unsigned c = 0; c <= 0x7f; ++c) if ((c < 0x20 && c != '\t' && c != '\r' && c != '\n') || c == 0x7f)
        cases.push_back("X: secret-marker" + std::string(1, static_cast<char>(c)));
    for (const auto& value : cases) {
        auto normalized = normalizeHttpHeaders(value);
        PHPVPN_REQUIRE_ERROR(normalized, ErrorCode::config_invalid_value);
        PHPVPN_CHECK(normalized.error().detail.find("masterSetup.httpHeaders line ") != std::string::npos);
        PHPVPN_CHECK(normalized.error().detail.find("secret-marker") == std::string::npos);
        for (bool repair : {false, true}) {
            const auto text = withHeaders(JsonValue::makeString(value), repair);
            ConfigWarnings warnings;
            PHPVPN_REQUIRE_ERROR(validateConfigValues(text, warnings, repair), ErrorCode::config_invalid_value);
            PHPVPN_REQUIRE_ERROR(analyzeWizardDocument(text), ErrorCode::config_invalid_value);
        }
    }
}
PHPVPN_TEST(http_headers_config, mandatory_string_type_and_order_independence) {
    for (bool repair : {false, true}) {
        for (auto bad : {"null", "true", "123", "[]", "{}"}) {
            ConfigWarnings warnings;
            auto text = withHeaders(Json::parse(bad).value(), repair);
            PHPVPN_CHECK(!validateConfigValues(text, warnings, repair));
            PHPVPN_CHECK(!analyzeWizardDocument(text));
        }
        auto dom = Json::parse(withHeaders(JsonValue::makeString("X: own"), repair)).value();
        auto& master = member(dom, "masterSetup");
        std::erase_if(master.objectValue(), [](const auto& item) { return item.first == "httpHeaders"; });
        ConfigWarnings warnings;
        auto missing = validateConfigValues(Json::serialize(dom), warnings, repair);
        PHPVPN_CHECK(!missing);
        PHPVPN_CHECK(missing.error().detail.find("httpHeaders") != std::string::npos);
        PHPVPN_CHECK(!analyzeWizardDocument(Json::serialize(dom)));
    }
    auto dom = Json::parse(withHeaders(JsonValue::makeString("X: own\nY: two"))).value();
    auto& master = member(dom, "masterSetup");
    std::reverse(master.objectValue().begin(), master.objectValue().end());
    ConfigWarnings warnings;
    auto reordered = parseAndValidateConfig(Json::serialize(dom), warnings);
    PHPVPN_REQUIRE_OK(reordered);
    PHPVPN_CHECK_EQ(reordered.value()->httpHeaders, "X: own\nY: two");
    PHPVPN_CHECK_EQ(reordered.value()->serverSecret, kDefaultServerSecret);
    PHPVPN_CHECK_EQ(reordered.value()->serverUrl.original, "http://127.0.0.1/phpvpn.php");
    PHPVPN_CHECK_EQ(reordered.value()->virtualIp.toString(), "10.99.99.2");
}
PHPVPN_TEST(http_headers_config, owned_names_reject_whole_document_and_preserve_saved_snapshot) {
    auto crypto = createPlatformCryptoProvider().value();
    auto backend = std::make_unique<FakeConfigFiles>(*crypto);
    auto* files = backend.get();
    ConfigStore store(*crypto, std::move(backend));
    PHPVPN_REQUIRE_OK(store.ensureDefaultAtStartup());
    const auto original = *files->bytes;
    auto active = store.readValid().value();
    const auto writes = files->writes;
    for (std::string name : {"Content-Type", "Content-Length", "Connection", "Host", "Transfer-Encoding"}) {
        for (int casing = 0; casing < 3; ++casing) {
            if (casing == 1) for (char& c : name) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (casing == 2) for (char& c : name) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
            const auto value = "X-Allowed: yes\n" + name + ": secret-marker";
            auto result = normalizeHttpHeaders(value);
            PHPVPN_REQUIRE_ERROR(result, ErrorCode::config_invalid_value);
            PHPVPN_CHECK(result.error().detail.find("masterSetup.httpHeaders line 2") != std::string::npos);
            PHPVPN_CHECK(result.error().detail.find(name) != std::string::npos);
            PHPVPN_CHECK(result.error().detail.find("managed by") != std::string::npos);
            PHPVPN_CHECK(result.error().detail.find("secret-marker") == std::string::npos);
            for (bool repair : {false, true}) {
                ConfigWarnings warnings;
                const auto text = withHeaders(JsonValue::makeString(value), repair);
                PHPVPN_REQUIRE_ERROR(validateConfigValues(text, warnings, repair), ErrorCode::config_invalid_value);
                PHPVPN_CHECK(!analyzeWizardDocument(text));
                PHPVPN_CHECK(!store.save(text, "00000000-0000-4000-8000-000000000001"));
                PHPVPN_CHECK_EQ(*files->bytes, original);
                PHPVPN_CHECK_EQ(files->writes, writes);
                PHPVPN_CHECK_EQ(active->httpHeaders, kDefaultHttpHeaders);
            }
        }
    }
}
PHPVPN_TEST(http_headers_config, exact_defaults_startup_reset_and_existing_file_preservation) {
    auto dom = Json::parse(kDefaultConfigDocument).value();
    const auto& members = dom.find("masterSetup")->objectValue();
    const std::vector<std::string> names = {"serverSecret", "serverUrl", "httpHeaders", "trustAnyTlsCertificate",
        "useDefaultServerSecretInWizard", "vpnCidr", "virtualIp", "autoDetectIpAddressInWizard", "accessKey", "useDefaultAccessKeyInWizard"};
    for (std::size_t i = 0; i < names.size(); ++i) PHPVPN_CHECK_EQ(members.at(i).first, names[i]);
    PHPVPN_CHECK_EQ(dom.find("masterSetup")->find("httpHeaders")->stringValue(), kDefaultHttpHeaders);
    PHPVPN_CHECK(kDefaultConfigDocument.find("no-cache\\r\\nPragma: no-cache\\r\\nUser-Agent:") != std::string_view::npos);
    auto fresh = makeNewWizardDocument();
    for (std::size_t i = 0; i < 4; ++i) PHPVPN_CHECK_EQ(fresh.dom.find("masterSetup")->objectValue().at(i).first, names[i]);
    PHPVPN_CHECK_EQ(fresh.context.httpHeaders, kDefaultHttpHeaders);
    PHPVPN_CHECK_EQ(fresh.dom.find("masterSetup")->find("httpHeaders")->stringValue(), kDefaultHttpHeaders);
    auto crypto = createPlatformCryptoProvider().value();
    auto backend = std::make_unique<FakeConfigFiles>(*crypto);
    auto* files = backend.get();
    ConfigStore store(*crypto, std::move(backend));
    PHPVPN_REQUIRE_OK(store.ensureDefaultAtStartup());
    PHPVPN_CHECK_EQ(*files->bytes, kDefaultConfigDocument);
    files->bytes = withHeaders(JsonValue::makeString("X: custom\nY: two"));
    const auto custom = *files->bytes;
    PHPVPN_REQUIRE_OK(store.ensureDefaultAtStartup());
    PHPVPN_CHECK_EQ(*files->bytes, custom);
    auto& master = member(dom, "masterSetup");
    std::erase_if(master.objectValue(), [](const auto& item) { return item.first == "httpHeaders"; });
    files->bytes = Json::serialize(dom);
    const auto legacy = *files->bytes;
    PHPVPN_REQUIRE_OK(store.ensureDefaultAtStartup());
    PHPVPN_CHECK_EQ(*files->bytes, legacy);
    PHPVPN_CHECK(!store.readValid());
    PHPVPN_REQUIRE_OK(store.save(makeDefaultEditorDocument(), "00000000-0000-4000-8000-000000000001"));
    const auto reset = store.readValid().value();
    PHPVPN_CHECK_EQ(reset->httpHeaders, kDefaultHttpHeaders);
}
PHPVPN_TEST(http_headers_wizard, preserves_raw_value_position_repair_contracts_and_ipc) {
    auto crypto = createPlatformCryptoProvider().value();
    const std::string headers = "X: first\nY: second\nZ: first\nX-Quote: \"a\\b\"\nX-Utf8: \xc5\xbe";
    for (bool repair : {false, true}) {
        auto dom = Json::parse(withHeaders(JsonValue::makeString(headers), repair)).value();
        auto& master = member(dom, "masterSetup");
        std::reverse(master.objectValue().begin(), master.objectValue().end());
        auto doc = analyzeWizardDocument(Json::serialize(dom));
        PHPVPN_REQUIRE_OK(doc);
        const auto before = master.objectValue();
        auto fields = analyzeWizardDocument(kDefaultConfigDocument)->fields;
        fields.httpHeaders = headers;
        fields.serverSecret = "edited secret";
        for (bool contracts : {false, true}) {
            auto patched = buildWizardDocument(*crypto, *doc, repair ? WizardMode::repair : WizardMode::edit, fields, contracts);
            PHPVPN_REQUIRE_OK(patched);
            auto parsed = Json::parse(*patched).value();
            const auto& after = parsed.find("masterSetup")->objectValue();
            PHPVPN_CHECK_EQ(parsed.find("masterSetup")->find("httpHeaders")->stringValue(), headers);
            for (std::size_t i = 0; i < before.size(); ++i) PHPVPN_CHECK_EQ(after.at(i).first, before[i].first);
            const auto reopened = analyzeWizardDocument(*patched);
            PHPVPN_REQUIRE_OK(reopened);
            PHPVPN_CHECK_EQ(reopened->context.httpHeaders, headers);
        }
        auto encoded = WizardIpc::contextValue(doc->context);
        auto roundtrip = WizardIpc::parseContext(Json::parse(Json::serialize(encoded)).value());
        PHPVPN_REQUIRE_OK(roundtrip);
        PHPVPN_CHECK_EQ(roundtrip->httpHeaders, headers);
        for (auto bad : {"null", "123", "\"Host: forbidden\"", "\"X: a\\n\\nbody\""}) {
            member(encoded, "httpHeaders") = Json::parse(bad).value();
            PHPVPN_CHECK(!WizardIpc::parseContext(encoded));
        }
        std::erase_if(encoded.objectValue(), [](const auto& item) { return item.first == "httpHeaders"; });
        PHPVPN_CHECK(!WizardIpc::parseContext(encoded));
    }
}
}

namespace phpvpn {
PHPVPN_TEST(http_headers_wizard, edited_fields_roundtrip_and_finish_only_publication) {
    auto crypto = createPlatformCryptoProvider().value();
    auto backend = std::make_unique<FakeConfigFiles>(*crypto);
    auto* files = backend.get();
    ConfigStore store(*crypto, std::move(backend));
    PHPVPN_REQUIRE_OK(store.ensureDefaultAtStartup());
    const auto bytes = *files->bytes;
    const auto writes = files->writes;
    auto doc = analyzeWizardDocument(bytes).value();
    auto fields = doc.fields;
    fields.httpHeaders = normalizeHttpHeaders("Cookie: synthetic=1\nX-Quote: \"a\\b\"\nX-Utf8: \xc5\xbe").value();
    auto roundtrip = WizardIpc::parseFields(Json::parse(Json::serialize(WizardIpc::fieldsValue(fields))).value());
    PHPVPN_REQUIRE_OK(roundtrip);
    PHPVPN_CHECK_EQ(*roundtrip, fields);
    auto built = buildWizardDocument(*crypto, doc, WizardMode::edit, fields, false);
    PHPVPN_REQUIRE_OK(built);
    PHPVPN_CHECK_EQ(analyzeWizardDocument(*built)->fields.httpHeaders, fields.httpHeaders);
    PHPVPN_CHECK_EQ(doc.context.httpHeaders, kDefaultHttpHeaders);
    PHPVPN_CHECK_EQ(*files->bytes, bytes);
    PHPVPN_CHECK_EQ(files->writes, writes);
    PHPVPN_REQUIRE_OK(store.save(*built, "00000000-0000-4000-8000-000000000001"));
    PHPVPN_CHECK_EQ(files->writes, writes + 1);
    PHPVPN_CHECK_EQ(store.readValid().value()->httpHeaders, fields.httpHeaders);
    fields.httpHeaders = "hOsT: synthetic-secret";
    PHPVPN_CHECK(!buildWizardDocument(*crypto, doc, WizardMode::edit, fields, false));
    PHPVPN_CHECK(!WizardIpc::parseFields(WizardIpc::fieldsValue(fields)));
}
PHPVPN_TEST(http_headers_wizard, final_json_limit_counts_escaping_utf8_and_normalization) {
    auto crypto = createPlatformCryptoProvider().value();
    const auto doc = analyzeWizardDocument(kDefaultConfigDocument).value();
    auto fields = doc.fields;
    fields.httpHeaders = "X: \"\\\xc5\xbe\r\nY: tail";
    const auto baseline = buildWizardDocument(*crypto, doc, WizardMode::edit, fields, false).value();
    const auto remaining = kMaxConfigFileBytes - baseline.size();
    fields.httpHeaders.insert(3, remaining, 'x');
    const auto exact = buildWizardDocument(*crypto, doc, WizardMode::edit, fields, false);
    PHPVPN_REQUIRE_OK(exact);
    PHPVPN_CHECK_EQ(exact->size(), kMaxConfigFileBytes);
    PHPVPN_CHECK_EQ(analyzeWizardDocument(*exact)->fields.httpHeaders, fields.httpHeaders);
    fields.httpHeaders.insert(3, 1, 'x');
    PHPVPN_REQUIRE_ERROR(buildWizardDocument(*crypto, doc, WizardMode::edit, fields, false), ErrorCode::config_too_large);
    fields.httpHeaders.erase(3, 1);
    const auto newline = fields.httpHeaders.find("\r\n");
    fields.httpHeaders.erase(newline, 1);
    const auto raw = buildWizardDocument(*crypto, doc, WizardMode::edit, fields, false);
    PHPVPN_REQUIRE_OK(raw);
    PHPVPN_CHECK_EQ(raw->size(), kMaxConfigFileBytes - 2);
    fields.httpHeaders = normalizeHttpHeaders(fields.httpHeaders).value();
    PHPVPN_CHECK_EQ(buildWizardDocument(*crypto, doc, WizardMode::edit, fields, false)->size(), kMaxConfigFileBytes);
    // Adding generated contracts uses the same final full-document boundary.
    PHPVPN_REQUIRE_ERROR(buildWizardDocument(*crypto, doc, WizardMode::edit, fields, true), ErrorCode::config_too_large);
}
PHPVPN_TEST(http_headers_config, duplicates_case_insensitive_reject_without_partial_publication) {
    auto crypto = createPlatformCryptoProvider().value();
    auto backend = std::make_unique<FakeConfigFiles>(*crypto);
    auto* files = backend.get();
    ConfigStore store(*crypto, std::move(backend));
    PHPVPN_REQUIRE_OK(store.ensureDefaultAtStartup());
    const auto original = *files->bytes;
    const auto writes = files->writes;
    for (std::string name : {"X-Test", "User-Agent", "Cookie", "Cache-Control", "Pragma", "Authorization", "Accept"}) {
        auto lower = name;
        for (char& c : lower) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        for (const auto& second : {name, lower}) {
            for (const auto separator : {"\n", "\r\n"}) {
                const auto value = name + ": first-secret" + separator + "X-Other: own" + separator + second + ": second-secret";
                auto normalized = normalizeHttpHeaders(value);
                PHPVPN_REQUIRE_ERROR(normalized, ErrorCode::config_invalid_value);
                PHPVPN_CHECK(normalized.error().detail.find("masterSetup.httpHeaders line 3") != std::string::npos);
                PHPVPN_CHECK(normalized.error().detail.find(second) != std::string::npos);
                PHPVPN_CHECK(normalized.error().detail.find("duplicate") != std::string::npos);
                PHPVPN_CHECK(normalized.error().detail.find("secret") == std::string::npos);
                for (bool repair : {false, true}) {
                    ConfigWarnings warnings;
                    const auto text = withHeaders(JsonValue::makeString(value), repair);
                    PHPVPN_REQUIRE_ERROR(validateConfigValues(text, warnings, repair), ErrorCode::config_invalid_value);
                    PHPVPN_CHECK(!analyzeWizardDocument(text));
                    PHPVPN_CHECK(!store.save(text, "00000000-0000-4000-8000-000000000001"));
                    PHPVPN_CHECK_EQ(*files->bytes, original);
                    PHPVPN_CHECK_EQ(files->writes, writes);
                }
                auto context = makeNewWizardDocument().context;
                context.httpHeaders = value;
                PHPVPN_CHECK(!WizardIpc::parseContext(WizardIpc::contextValue(context)));
            }
        }
    }
    // Similar prefixes and occurrences inside values are not duplicate names.
    PHPVPN_REQUIRE_OK(normalizeHttpHeaders("X: Host: value\nX-Other: X: value\nHostname: own"));
}
}
