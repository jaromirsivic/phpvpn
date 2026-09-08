#include "harness.h"
#include "fake_config_files.h"
#include "core/config/config_store.h"
#include "core/config/config_defaults.h"
#include "core/base/hex.h"
#include <thread>

namespace phpvpn {
namespace {
constexpr char idA[] = "00000000-0000-4000-8000-000000000001";
constexpr char idB[] = "00000000-0000-4000-8000-000000000002";
struct StoreFixture {
    std::unique_ptr<CryptoProvider> crypto = std::move(createPlatformCryptoProvider()).value();
    FakeConfigFiles* files = nullptr;
    std::unique_ptr<ConfigStore> store;
    StoreFixture() {
        auto backend = std::make_unique<FakeConfigFiles>(*crypto);
        files = backend.get();
        store = std::make_unique<ConfigStore>(*crypto, std::move(backend));
    }
};
}
PHPVPN_TEST(config_store, fresh_action_invalidates_old_config_and_preserves_editor_text) {
    StoreFixture fixture;
    auto& disk = *fixture.files;
    PHPVPN_CHECK(fixture.store->read()->availability == ConfigAvailability::missing);
    disk.bytes = makeDefaultEditorDocument();
    auto valid = fixture.store->read();
    PHPVPN_REQUIRE_OK(valid);
    PHPVPN_CHECK(valid->config != nullptr);
    const auto active = valid->config;
    disk.bytes = "{ broken JSON";
    auto invalid = fixture.store->read();
    PHPVPN_CHECK_EQ(invalid->availability, ConfigAvailability::invalid);
    PHPVPN_CHECK_EQ(*invalid->document, *disk.bytes);
    PHPVPN_CHECK(!invalid->config && !invalid->wizard);
    PHPVPN_CHECK(invalid->revisionToken != valid->revisionToken);
    PHPVPN_CHECK(active->virtualIp.toString() == "10.99.99.2");
    disk.bytes.reset();
    PHPVPN_CHECK_EQ(fixture.store->readValid().code(), ErrorCode::config_not_found);
    PHPVPN_CHECK_EQ(disk.writes, 0u);
}
PHPVPN_TEST(config_store, rejected_input_has_no_editor_document_or_stale_revision) {
    StoreFixture fixture;
    fixture.files->bytes = makeDefaultEditorDocument();
    PHPVPN_REQUIRE_OK(fixture.store->readValid());
    for (const auto error : {ErrorCode::config_unreadable, ErrorCode::config_too_large}) {
        fixture.files->readError = error;
        auto result = fixture.store->read();
        PHPVPN_CHECK_EQ(*result->reasonCode, error);
        PHPVPN_CHECK(!result->document && !result->wizard && !result->config);
        PHPVPN_CHECK(result->revisionToken.empty() && result->sha256.empty());
    }
    fixture.files->readError.reset();
    fixture.files->bytes = "\xff\xfe";
    auto result = fixture.store->read();
    PHPVPN_CHECK_EQ(result->availability, ConfigAvailability::encoding_error);
    PHPVPN_CHECK(!result->document && result->revisionToken.empty());
}
PHPVPN_TEST(config_store, revision_uses_exact_bytes_and_identity_not_timestamps) {
    StoreFixture fixture;
    fixture.files->bytes = makeDefaultEditorDocument();
    auto first = fixture.store->read();
    auto same = fixture.store->read();
    PHPVPN_CHECK_EQ(first->revisionToken, same->revisionToken);
    fixture.files->bytes = "\xef\xbb\xbf" + *fixture.files->bytes;
    auto bom = fixture.store->read();
    PHPVPN_CHECK_EQ(first->document, bom->document);
    PHPVPN_CHECK(first->revisionToken != bom->revisionToken);
    ++fixture.files->identity;
    auto replaced = fixture.store->read();
    PHPVPN_CHECK(replaced->revisionToken != bom->revisionToken);
}
PHPVPN_TEST(config_store, last_save_wins_and_create_never_overwrites_even_invalid_text) {
    StoreFixture fixture;
    auto created = fixture.store->createDefault();
    PHPVPN_REQUIRE_OK(created);
    PHPVPN_CHECK(created->created && created->snapshot.config != nullptr);
    fixture.files->bytes = "{external invalid text";
    auto raced = fixture.store->createDefault();
    PHPVPN_REQUIRE_OK(raced);
    PHPVPN_CHECK(!raced->created);
    PHPVPN_CHECK_EQ(*raced->snapshot.document, "{external invalid text");
    const auto original = makeDefaultEditorDocument();
    auto changed = original + "\n";
    auto first = fixture.store->save(changed, idA);
    auto last = fixture.store->save(original, idB);
    PHPVPN_REQUIRE_OK(first);
    PHPVPN_REQUIRE_OK(last);
    PHPVPN_CHECK(first->revisionToken != last->revisionToken);
    PHPVPN_CHECK_EQ(*fixture.files->bytes, original);
    const auto saved = fixture.store->read();
    PHPVPN_CHECK_EQ(saved->revisionToken, last->revisionToken);
    fixture.files->outcome = ConfigPublishState::unknown;
    auto unknown = fixture.store->save(changed, idA);
    PHPVPN_REQUIRE_OK(unknown);
    PHPVPN_CHECK_EQ(unknown->state, ConfigPublishState::unknown);
    PHPVPN_CHECK(unknown->revisionToken.empty());
    PHPVPN_CHECK_EQ(*fixture.files->bytes, original);
}
PHPVPN_TEST(config_store, concurrent_complete_saves_remain_serialized) {
    StoreFixture fixture;
    const auto a = makeDefaultEditorDocument();
    const auto b = a + "\n";
    bool successA = false, successB = false;
    std::thread first([&] { successA = fixture.store->save(a, idA).ok(); });
    std::thread second([&] { successB = fixture.store->save(b, idB).ok(); });
    first.join(); second.join();
    PHPVPN_CHECK(successA && successB);
    PHPVPN_CHECK(*fixture.files->bytes == a || *fixture.files->bytes == b);
    PHPVPN_CHECK_EQ(fixture.files->writes, 2u);
    PHPVPN_REQUIRE_OK(fixture.store->readValid());
}

PHPVPN_TEST(config_store, directory_security_failure_propagates_without_default_or_save) {
    StoreFixture fixture;
    fixture.files->publishError = ErrorCode::config_directory_security_invalid;
    PHPVPN_REQUIRE_ERROR(fixture.store->createDefault(), ErrorCode::config_directory_security_invalid);
    PHPVPN_CHECK(!fixture.files->bytes);
    fixture.files->bytes = makeDefaultEditorDocument();
    PHPVPN_REQUIRE_ERROR(fixture.store->save(makeDefaultEditorDocument() + "\n", idA),
        ErrorCode::config_directory_security_invalid);
    PHPVPN_CHECK_EQ(*fixture.files->bytes, makeDefaultEditorDocument());
}
PHPVPN_TEST(config_store, exactly_one_bom_is_removed_per_input_boundary) {
    StoreFixture fixture;
    const auto normal = makeDefaultEditorDocument();
    const auto one = std::string("\xef\xbb\xbf") + normal;
    fixture.files->bytes = std::string("\xef\xbb\xbf") + one;
    auto twice = fixture.store->read();
    PHPVPN_REQUIRE_OK(twice);
    PHPVPN_CHECK_EQ(twice->availability, ConfigAvailability::invalid);
    PHPVPN_CHECK_EQ(*twice->document, one);
    PHPVPN_CHECK(!twice->config && !twice->wizard);
    PHPVPN_CHECK(!fixture.store->save(*fixture.files->bytes, idA));
    PHPVPN_CHECK_EQ(fixture.files->writes, 0u);
    PHPVPN_REQUIRE_OK(fixture.store->save(one, idA));
    PHPVPN_CHECK_EQ(*fixture.files->bytes, normal);
}

PHPVPN_TEST(config_store, cidr_gate_precedes_publication_and_startup_never_migrates) {
    StoreFixture fixture;
    PHPVPN_REQUIRE_OK(fixture.store->ensureDefaultAtStartup());
    PHPVPN_REQUIRE_OK(fixture.store->readValid());
    PHPVPN_CHECK_EQ(fixture.files->writes, 1u);
    for (const auto prefix : {16, 17, 18, 19, 20, 21, 22, 23, 30, 31, 32}) {
        auto invalid = makeDefaultEditorDocument();
        invalid.replace(invalid.find("10.99.99.0/24"), 13, "10.99.0.0/" + std::to_string(prefix));
        fixture.files->bytes = invalid;
        auto read = fixture.store->read();
        PHPVPN_REQUIRE_OK(read);
        PHPVPN_CHECK_EQ(read->availability, ConfigAvailability::invalid);
        PHPVPN_CHECK_EQ(read->reasonCode, ErrorCode::config_invalid_cidr);
        PHPVPN_CHECK(!read->config && !read->wizard);
        PHPVPN_CHECK_EQ(*read->document, invalid);
        PHPVPN_REQUIRE_ERROR(fixture.store->readValid(), ErrorCode::config_invalid_cidr);
        PHPVPN_REQUIRE_ERROR(fixture.store->save(invalid, idA), ErrorCode::config_invalid_cidr);
        const auto startup = fixture.store->ensureDefaultAtStartup();
        PHPVPN_REQUIRE_OK(startup);
        PHPVPN_CHECK_EQ(*startup, StartupConfigState::existing);
        PHPVPN_CHECK_EQ(*fixture.files->bytes, invalid);
        PHPVPN_CHECK_EQ(fixture.files->writes, 1u);
    }
    // Explicit valid publication can repair that file, including the /29 edge.
    for (const auto prefix : {24, 29}) {
        auto valid = makeDefaultEditorDocument();
        valid.replace(valid.find("/24"), 3, "/" + std::to_string(prefix));
        PHPVPN_REQUIRE_OK(fixture.store->save(valid, idB));
        const auto read = fixture.store->readValid();
        PHPVPN_REQUIRE_OK(read);
        PHPVPN_CHECK_EQ(read.value()->maxRemotePeers(), prefix == 24 ? 252u : 4u);
    }
}
} // namespace phpvpn
