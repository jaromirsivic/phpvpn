#include "contracts_fixture.h"
#include <limits>
#include <type_traits>
#include "core/wizard/wizard_defaults.h"

namespace phpvpn {
using namespace contracts_test;
PHPVPN_TEST(ContractsModel, all_peers_numeric_order_self_and_address_boundaries) {
    Crypto crypto;
    for (const auto local : {"10.99.99.2", "10.99.99.128", "10.99.99.254"}) {
        auto model = ContractsModel::create(crypto, document(local), "synthetic/config", "old-revision");
        PHPVPN_REQUIRE_OK(model);
        PHPVPN_CHECK_EQ(model.value()->rows().size(), 252u);
        PHPVPN_CHECK(!model.value()->row(ip(local)));
        std::uint32_t previous = 0;
        for (const auto& row : model.value()->rows()) {
            PHPVPN_CHECK(previous < row.remoteIp().value());
            previous = row.remoteIp().value();
            PHPVPN_CHECK(!row.enabled() && row.keyText().empty() && !row.keyValid() && !row.keyErrorVisible() && !row.dirty());
        }
        PHPVPN_CHECK(!model.value()->dirty());
        PHPVPN_CHECK_EQ(model.value()->configPath(), "synthetic/config");
        PHPVPN_CHECK_EQ(model.value()->revisionToken(), "old-revision");
    }
    for (const auto& [cidr, local] : {std::pair{"0.0.0.0/29", "0.0.0.2"},
            {"172.16.2.0/29", "172.16.2.6"}, {"255.255.255.248/29", "255.255.255.254"}}) {
        auto model = ContractsModel::create(crypto, document(local, cidr));
        PHPVPN_REQUIRE_OK(model);
        PHPVPN_CHECK_EQ(model.value()->rows().size(), 4u);
    }
}
PHPVPN_TEST(ContractsModel, invalid_range_and_repair_rejected_before_default_hashes) {
    Crypto crypto;
    for (const auto cidr : {"0.0.0.0/0", "10.99.0.0/16", "10.99.98.0/23", "10.99.99.0/30",
            "10.99.99.0/31", "10.99.99.0/32", "10.99.99.1/24", "10.99.99.0/999999999999999"})
        PHPVPN_REQUIRE_ERROR(ContractsModel::create(crypto, document("10.99.99.2", cidr)), ErrorCode::config_invalid_cidr);
    PHPVPN_REQUIRE_ERROR(checkedContractPeerCount(Ipv4Cidr{}, Ipv4Address{}), ErrorCode::config_invalid_cidr);
    for (const auto local : {"10.99.99.0", "10.99.99.1", "10.99.99.255", "10.99.98.2"})
        PHPVPN_REQUIRE_ERROR(ContractsModel::create(crypto, document(local)), ErrorCode::config_invalid_address);
    auto dom = Json::parse(document()).value();
    for (auto& member : dom.objectValue()) if (member.first == "masterSetup")
        std::erase_if(member.second.objectValue(), [](const auto& item) { return item.first == "accessKey"; });
    PHPVPN_CHECK(!ContractsModel::create(crypto, Json::serialize(dom)));
    PHPVPN_CHECK_EQ(crypto.hashCalls, 0u);
}
PHPVPN_TEST(ContractsModel, all_enabled_252_rows_publish_and_multiple_errors_are_counted) {
    Crypto crypto;
    auto created = ContractsModel::create(crypto, document("10.20.30.10", "10.20.30.0/24"));
    PHPVPN_REQUIRE_OK(created);
    auto& m = *created.value();
    PHPVPN_CHECK_EQ(m.rows().front().remoteIp(), ip("10.20.30.2"));
    const auto pair = m.target(ip("10.20.30.2"));
    PHPVPN_REQUIRE_OK(pair);
    PHPVPN_CHECK_EQ(pair->contractId(), "10.20.30.2_10.20.30.10");
    for (const auto& row : m.rows()) { PHPVPN_REQUIRE_OK(m.setKey(row.remoteIp(), std::string(64, 'a'))); PHPVPN_REQUIRE_OK(m.setEnabled(row.remoteIp(), true)); }
    auto candidate = m.prepareSave(envelopeSize);
    PHPVPN_REQUIRE_OK(candidate);
    PHPVPN_CHECK_EQ(candidate.value()->dom().find("communicationContracts")->objectValue().size(), 252u);
    PHPVPN_CHECK(m.resumeEditing(m.saveGeneration()));
    const auto first = m.rows().front().remoteIp(), last = m.rows().back().remoteIp();
    PHPVPN_REQUIRE_OK(m.setKey(first, ""));
    PHPVPN_REQUIRE_OK(m.setKey(last, "bad"));
    PHPVPN_CHECK_EQ(m.blockingErrors(), 2u);
    PHPVPN_CHECK_EQ(m.firstBlockingFocus()->remoteIp, first);
    PHPVPN_REQUIRE_OK(m.setEnabled(first, false));
    PHPVPN_CHECK_EQ(m.blockingErrors(), 1u);
    PHPVPN_CHECK_EQ(m.firstBlockingFocus()->remoteIp, last);
    PHPVPN_REQUIRE_OK(m.setEnabled(last, false));
    PHPVPN_CHECK(m.canPrepareSave());
}
PHPVPN_TEST(ContractsModel, exact_existing_key_shared_defaults_and_pair_vectors) {
    Crypto crypto;
    auto model = ContractsModel::create(crypto, makeDefaultEditorDocument());
    PHPVPN_REQUIRE_OK(model);
    const auto& m = *model.value();
    PHPVPN_CHECK(m.row(ip("10.99.99.3"))->enabled());
    PHPVPN_CHECK_EQ(m.row(ip("10.99.99.3"))->keyText(), "918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d");
    PHPVPN_CHECK_EQ(m.row(ip("10.99.99.10"))->keyText(), "");
    PHPVPN_CHECK(!m.row(ip("10.99.99.10"))->enabled());
    PHPVPN_CHECK_EQ(crypto.hashCalls, 0u);
    auto reverse = ContractsModel::create(crypto, document("10.99.99.3"));
    PHPVPN_REQUIRE_OK(reverse);
    PHPVPN_CHECK(reverse.value()->row(ip("10.99.99.2"))->keyText().empty());
    auto changed = makeDefaultEditorDocument();
    const auto pos = changed.find("918c4885");
    changed.replace(pos, 64, std::string(64, 'b'));
    auto exact = ContractsModel::create(crypto, changed);
    PHPVPN_REQUIRE_OK(exact);
    PHPVPN_CHECK_EQ(exact.value()->row(ip("10.99.99.3"))->keyText(), std::string(64, 'b'));
    crypto.failHash = true;
    PHPVPN_REQUIRE_OK(ContractsModel::create(crypto, document()));
    auto& editable = *model.value();
    const auto peer = ip("10.99.99.3");
    const std::string stored(editable.row(peer)->keyText());
    PHPVPN_REQUIRE_OK(editable.setEnabled(peer, false));
    PHPVPN_CHECK_EQ(editable.row(peer)->keyText(), stored);
    PHPVPN_REQUIRE_OK(editable.setEnabled(peer, true));
    PHPVPN_CHECK(!editable.dirty());
    PHPVPN_REQUIRE_OK(editable.setEnabled(peer, false));
    auto saved = editable.prepareSave(envelopeSize); PHPVPN_REQUIRE_OK(saved);
    auto reloaded = ContractsModel::create(crypto, saved.value()->text()); PHPVPN_REQUIRE_OK(reloaded);
    PHPVPN_CHECK(reloaded.value()->row(peer)->keyText().empty());
    PHPVPN_CHECK(!reloaded.value()->row(peer)->enabled() && !reloaded.value()->dirty());
}
PHPVPN_TEST(ContractsModel, key_validation_all_rows_dirty_restore_and_focus) {
    Crypto crypto;
    auto created = ContractsModel::create(crypto, document());
    PHPVPN_REQUIRE_OK(created);
    auto& m = *created.value();
    const auto peer = ip("10.99.99.254");
    const std::string original(m.row(peer)->keyText());
    const ContractFocus focus{ip("10.99.99.10"), ContractColumn::password};
    PHPVPN_REQUIRE_OK(m.setFocus(focus));
    for (const auto& bad : {std::string(63, 'a'), std::string(65, 'a'), std::string(64, 'A'),
            std::string(64, 'g'), std::string(" ") + original, std::string("a\0b", 3)}) {
        PHPVPN_REQUIRE_OK(m.setKey(peer, bad));
        PHPVPN_CHECK_EQ(m.row(peer)->keyText(), bad);
        PHPVPN_CHECK(!m.row(peer)->keyValid());
        PHPVPN_CHECK(m.canPrepareSave());
        PHPVPN_REQUIRE_OK(m.setEnabled(peer, true));
        PHPVPN_CHECK(!m.canPrepareSave());
        PHPVPN_CHECK_EQ(m.blockingErrors(), 1u);
        PHPVPN_CHECK_EQ(m.firstBlockingFocus(), std::optional(ContractFocus{peer, ContractColumn::key}));
        PHPVPN_REQUIRE_ERROR(m.prepareSave(envelopeSize), ErrorCode::config_invalid_key);
        PHPVPN_REQUIRE_OK(m.setEnabled(peer, false));
    }
    PHPVPN_REQUIRE_OK(m.setKey(peer, std::string(64, 'b')));
    PHPVPN_CHECK(!m.row(peer)->dirty() && !m.dirty());
    PHPVPN_REQUIRE_OK(m.setEnabled(peer, true));
    PHPVPN_CHECK(m.row(peer)->dirty() && m.dirty());
    PHPVPN_REQUIRE_OK(m.setEnabled(peer, false));
    PHPVPN_CHECK(!m.dirty());
    PHPVPN_CHECK_EQ(m.row(peer)->keyText(), std::string(64, 'b'));
    PHPVPN_REQUIRE_OK(m.setKey(peer, original));
    PHPVPN_CHECK(!m.dirty());
    PHPVPN_CHECK_EQ(m.focus(), std::optional(focus));
    PHPVPN_REQUIRE_OK(m.setKey(peer, m.row(peer)->keyText())); // alias-safe replacement
    PHPVPN_CHECK(!m.dirty());
    PHPVPN_REQUIRE_ERROR(m.setKey(peer, "\xff"), ErrorCode::config_encoding_error);
    PHPVPN_REQUIRE_ERROR(m.setEnabled(m.localIp(), true), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(m.setFocus(ContractFocus{m.localIp(), ContractColumn::key}), ErrorCode::invalid_argument);
}
PHPVPN_TEST(ContractsModel, total_key_budget_exact_plus_one_and_size_overflow) {
    Crypto crypto;
    auto created = ContractsModel::create(crypto, document());
    PHPVPN_REQUIRE_OK(created);
    auto& m = *created.value();
    const auto peer = m.rows().front().remoteIp();
    const auto available = kMaxContractKeyTextBytes - m.keyTextBytes() + m.row(peer)->keyText().size();
    const std::string large(available, 'a');
    PHPVPN_REQUIRE_OK(m.setKey(peer, large));
    PHPVPN_CHECK_EQ(m.keyTextBytes(), kMaxContractKeyTextBytes);
    PHPVPN_REQUIRE_ERROR(m.setKey(peer, large + 'a'), ErrorCode::config_too_large);
    PHPVPN_CHECK_EQ(m.row(peer)->keyText(), large);
    PHPVPN_REQUIRE_ERROR(m.setKey(m.rows().back().remoteIp(), std::string(65, 'a')), ErrorCode::config_too_large);
    PHPVPN_CHECK(m.canPrepareSave()); // inactive oversized key never goes to JSON
    auto candidate = m.prepareSave(envelopeSize);
    PHPVPN_REQUIRE_OK(candidate);
    PHPVPN_CHECK(candidate.value()->text().size() < 1024);
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    PHPVPN_REQUIRE_ERROR(checkedContractKeyTextSize(64, 64, maximum), ErrorCode::config_too_large);
    PHPVPN_REQUIRE_ERROR(checkedContractKeyTextSize(maximum, 0, 1), ErrorCode::config_too_large);
    PHPVPN_REQUIRE_ERROR(checkedContractKeyTextSize(0, 1, 0), ErrorCode::config_too_large);
}
PHPVPN_TEST(ContractsModel, random_confirmation_failure_and_restored_key_is_clean) {
    Crypto crypto;
    auto created = ContractsModel::create(crypto, document());
    PHPVPN_REQUIRE_OK(created);
    auto& m = *created.value();
    auto target = m.target(ip("10.99.99.10"));
    PHPVPN_REQUIRE_OK(target);
    PHPVPN_CHECK_EQ(target->randomConfirmation(), "Replace the key for 10.99.99.2_10.99.99.10 with a new random key?");
    for (int noEscapeClose = 0; noEscapeClose < 3; ++noEscapeClose) PHPVPN_REQUIRE_OK(m.confirmRandom(crypto, *target, false));
    PHPVPN_CHECK_EQ(crypto.randomCalls, 0u);
    PHPVPN_CHECK(!m.dirty());
    crypto.failRandom = true;
    const std::string old(m.row(target->remoteIp())->keyText());
    const auto failed = m.confirmRandom(crypto, *target, true);
    PHPVPN_REQUIRE_ERROR(failed, ErrorCode::crypto_failed);
    PHPVPN_CHECK(failed.error().detail.empty());
    PHPVPN_CHECK_EQ(m.row(target->remoteIp())->keyText(), old);
    PHPVPN_CHECK(!m.dirty());
    crypto.failRandom = false;
    PHPVPN_REQUIRE_OK(m.confirmRandom(crypto, *target, true));
    PHPVPN_CHECK_EQ(crypto.randomSize, 32u);
    PHPVPN_CHECK_EQ(m.row(target->remoteIp())->keyText(), "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    PHPVPN_CHECK(!m.row(target->remoteIp())->enabled());
    PHPVPN_REQUIRE_OK(m.setKey(target->remoteIp(), old));
    PHPVPN_CHECK(!m.dirty());
}
PHPVPN_TEST(ContractsModel, multiple_targets_last_confirmed_frozen_candidate_and_stale_generations) {
    Crypto crypto;
    auto a = ContractsModel::create(crypto, document());
    PHPVPN_REQUIRE_OK(a.value()->setKey(ip("10.99.99.10"), std::string(64, 'd')));
    auto b = ContractsModel::create(crypto, document());
    PHPVPN_REQUIRE_OK(a); PHPVPN_REQUIRE_OK(b);
    auto& m = *a.value();
    auto first = m.target(ip("10.99.99.10")).value();
    auto same = m.target(first.remoteIp()).value();
    auto other = m.target(ip("10.99.99.3")).value();
    PHPVPN_CHECK_EQ(first.passwordTitle(), "Password SHA - Contract 10.99.99.2_10.99.99.10");
    PHPVPN_CHECK_EQ(same.passwordTitle(), first.passwordTitle());
    PHPVPN_CHECK_EQ(other.passwordTitle(), "Password SHA - Contract 10.99.99.2_10.99.99.3");
    PHPVPN_REQUIRE_OK(m.setFocus(ContractFocus{other.remoteIp(), ContractColumn::random}));
    const std::string original(m.row(first.remoteIp())->keyText());
    PHPVPN_REQUIRE_OK(m.applyPasswordKey(first, original));
    PHPVPN_CHECK(!m.dirty()); // disabled working keys do not affect the JSON
    PHPVPN_REQUIRE_OK(m.applyPasswordKey(first, std::string(64, 'a')));
    PHPVPN_REQUIRE_OK(m.applyPasswordKey(other, std::string(64, 'c')));
    PHPVPN_REQUIRE_OK(m.applyPasswordKey(same, std::string(64, 'b')));
    PHPVPN_CHECK_EQ(m.row(first.remoteIp())->keyText(), std::string(64, 'b'));
    PHPVPN_CHECK_EQ(m.row(other.remoteIp())->keyText(), std::string(64, 'c'));
    PHPVPN_REQUIRE_ERROR(b.value()->applyPasswordKey(first, original), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_OK(m.setEnabled(first.remoteIp(), true));
    const auto before = m.saveGeneration();
    auto frozen = m.prepareSave(envelopeSize);
    PHPVPN_REQUIRE_OK(frozen);
    PHPVPN_CHECK_EQ(m.saveGeneration(), before + 1);
    const std::string snapshot(frozen.value()->text());
    PHPVPN_CHECK(m.frozen());
    PHPVPN_REQUIRE_ERROR(m.setEnabled(first.remoteIp(), false), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(m.applyPasswordKey(other, original), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(m.prepareSave(envelopeSize), ErrorCode::invalid_argument);
    PHPVPN_CHECK(!m.resumeEditing(before));
    PHPVPN_CHECK(m.resumeEditing(before + 1));
    PHPVPN_REQUIRE_ERROR(m.applyPasswordKey(first, original), ErrorCode::invalid_argument);
    const auto fresh = m.target(first.remoteIp()).value();
    PHPVPN_REQUIRE_OK(m.applyPasswordKey(fresh, original));
    PHPVPN_CHECK_EQ(frozen.value()->text(), snapshot);
    m.close();
    PHPVPN_CHECK(m.closed() && m.rows().empty() && !m.candidate() && !m.focus());
    PHPVPN_REQUIRE_ERROR(m.applyPasswordKey(fresh, original), ErrorCode::invalid_argument);
    PHPVPN_CHECK_EQ(frozen.value()->text(), snapshot); // external reader still owns it
    PHPVPN_CHECK(b.value()->canPrepareSave());
}
PHPVPN_TEST(ContractsModel, candidate_local_failure_keeps_draft_generation_and_children) {
    Crypto crypto;
    auto created = ContractsModel::create(crypto, document());
    PHPVPN_REQUIRE_OK(created);
    auto& m = *created.value();
    const auto target = m.target(ip("10.99.99.3")).value();
    const auto generation = m.saveGeneration();
    PHPVPN_REQUIRE_ERROR(m.prepareSave({}), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(m.prepareSave([](std::string_view) -> Result<std::size_t> { return kIpcMaxMessageBytes + 1; }), ErrorCode::ipc_message_too_large);
    PHPVPN_REQUIRE_ERROR(m.prepareSave([](std::string_view) -> Result<std::size_t> { return Error(ErrorCode::invalid_argument); }), ErrorCode::invalid_argument);
    PHPVPN_CHECK_EQ(m.saveGeneration(), generation);
    PHPVPN_CHECK(!m.frozen());
    PHPVPN_REQUIRE_OK(m.applyPasswordKey(target, std::string(64, 'a')));
}
} // namespace phpvpn
