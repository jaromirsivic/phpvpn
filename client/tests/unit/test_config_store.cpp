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
} // namespace phpvpn
