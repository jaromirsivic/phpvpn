// GTK-free rules of the Linux Edit Config, Communication contracts and Password SHA
// windows (WP10 part b): editor presentation, save text, save classification
// notices, Reset reply validation and the editor lifecycle state; contracts footer,
// messages, pager, the single-line edit preflight of the Win32 ContractsEdit, key
// text rules, Password SHA notices and the random password; and the contracts save
// machine driven through the real asynchronous IPC channel against a Unix-socket
// server backed by the real ConfigStore.
#include "harness.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>

#include "contracts_save_machine.h"
#include "contracts_view.h"
#include "core/config/config_defaults.h"
#include "core/config/config_store.h"
#include "core/presentation/table_pagination.h"
#include "core/ipc/ipc_protocol.h"
#include "core/ipc/wizard_ipc.h"
#include "editor_model.h"
#include "ipc_async.h"
#include "platform/linux/config_files_linux.h"
#include "platform/linux/ipc_unix.h"

namespace phpvpn::tray {
namespace {
Ipv4Address ip(std::string_view text) { return *Ipv4Address::parse(text); }

// Real provider with a scripted randomBytes for the random password.
class ScriptedRandom final : public CryptoProvider {
public:
    std::unique_ptr<CryptoProvider> real = std::move(createPlatformCryptoProvider()).value();
    int calls = 0;
    bool failRandom = false;
    Status randomBytes(std::span<std::uint8_t> out) override {
        // The first batch is all 255 (rejected), then 0..63.
        for (std::size_t i = 0; i < out.size(); ++i) out[i] = calls == 0 ? 255 : static_cast<std::uint8_t>(i);
        ++calls;
        return failRandom ? fail(ErrorCode::crypto_failed) : ok();
    }
    Result<Sha256Digest> sha256(std::span<const std::uint8_t> data) override { return real->sha256(data); }
    Result<Sha256Digest> hmacSha256(std::span<const std::uint8_t> key, std::span<const std::uint8_t> data) override {
        return real->hmacSha256(key, data);
    }
    Status aesGcmSeal(std::span<const std::uint8_t> key, std::span<const std::uint8_t> iv, std::span<const std::uint8_t> aad,
                      std::span<const std::uint8_t> plain, std::vector<std::uint8_t>& cipher, GcmTag& tag) override {
        return real->aesGcmSeal(key, iv, aad, plain, cipher, tag);
    }
    Result<SecureBuffer> aesGcmOpen(std::span<const std::uint8_t> key, std::span<const std::uint8_t> iv,
                                    std::span<const std::uint8_t> aad, std::span<const std::uint8_t> cipher,
                                    std::span<const std::uint8_t> tag) override {
        return real->aesGcmOpen(key, iv, aad, cipher, tag);
    }
};

SaveResult result(const SaveOperation& operation, SaveOutcome outcome, ErrorCode error = ErrorCode::none,
                  std::string description = {}) {
    return {operation, outcome, error, std::move(description)};
}

Result<WizardReply> defaultReply(std::string document) {
    WizardReply reply;
    reply.result = JsonValue::makeObject({{"document", JsonValue::makeString(std::move(document))}});
    return reply;
}

// Thread-safe stand-in for the GLib main context (as in test_tray_linux_models).
class Pump {
public:
    Dispatcher dispatcher() {
        return [this](std::function<void()> task) {
            { std::lock_guard lock(mutex_); tasks_.push_back(std::move(task)); }
            changed_.notify_all();
        };
    }
    template <class Predicate>
    bool until(Predicate done, int limitMs = 5000) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(limitMs);
        while (!done()) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                if (!changed_.wait_until(lock, end, [&] { return !tasks_.empty(); })) return done();
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            task();
        }
        return true;
    }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::function<void()>> tasks_;
};

class TempDir {
public:
    TempDir() {
        char pattern[] = "/tmp/phpvpn-wp10b-XXXXXX";
        const char* made = mkdtemp(pattern);
        path_ = made ? made : "";
    }
    ~TempDir() { std::error_code error; std::filesystem::remove_all(path_, error); }
    const std::string& path() const { return path_; }
private:
    std::string path_;
};
}  // namespace

// --- Edit Config ---------------------------------------------------------------
PHPVPN_TEST(TrayLinuxConfig, editor_pretty_json_reflows_whitespace_only) {
    PHPVPN_CHECK_EQ(prettyEditorJson(R"({"a":1.000000000000000000001,"b":[],"c":{"d":"x\"y, z"}})"),
                    std::string("{\n  \"a\": 1.000000000000000000001,\n  \"b\": [],\n  \"c\": {\n    \"d\": \"x\\\"y, z\"\n  }\n}"));
    // Invalid JSON is repairable text and stays exactly as received.
    PHPVPN_CHECK_EQ(prettyEditorJson("{\"a\": }\r\n"), std::string("{\"a\": }\r\n"));
    PHPVPN_CHECK_EQ(prettyEditorJson("[1,{}]"), std::string("[\n  1,\n  {}\n]"));
}

PHPVPN_TEST(TrayLinuxConfig, editor_presentation_and_literal_mode) {
    EditorDocument plain;
    plain.load("{\"a\":1}");
    const auto shown = presentEditorDocument(plain, "{\"a\":1}");
    PHPVPN_CHECK(!shown.literal);
    PHPVPN_CHECK_EQ(shown.text, std::string("{\n  \"a\": 1\n}"));
    PHPVPN_CHECK_EQ(shown.title, std::string("PHPVPN configuration"));

    const std::string raw("{\"a\":\"x\0y\"}", 11);
    EditorDocument literal;
    literal.load(raw);
    const auto escaped = presentEditorDocument(literal, raw);
    PHPVPN_CHECK(escaped.literal);
    PHPVPN_CHECK(escaped.text.find("\\u0000") != std::string::npos);
    PHPVPN_CHECK_EQ(escaped.title, std::string("PHPVPN configuration - JSON string literal (raw NUL shown as \\u0000)"));
    // Unmodified text sends the original bytes, including the NUL.
    const auto unchanged = editorSaveText(literal, escaped.text, false);
    PHPVPN_CHECK(unchanged.document.has_value() && *unchanged.document == raw);
    // An edited literal must remain one complete JSON string.
    PHPVPN_CHECK(editorSaveText(literal, "{\"a\":1}", true).error == kEditorLiteralRequired);
    const auto edited = editorSaveText(literal, "\"{}\"", true);
    PHPVPN_CHECK(edited.document.has_value() && *edited.document == "{}");
}

PHPVPN_TEST(TrayLinuxConfig, editor_save_text_uses_original_bytes_and_rejects_invalid_unicode) {
    EditorDocument document;
    document.load("{\r\n\"a\":1}");
    const auto unmodified = editorSaveText(document, "{\n  \"a\": 1\n}", false);
    PHPVPN_CHECK(unmodified.document && *unmodified.document == "{\r\n\"a\":1}");
    const auto modified = editorSaveText(document, "{\"a\":2}", true);
    PHPVPN_CHECK(modified.document && *modified.document == "{\"a\":2}");
    // Syntactically invalid JSON in valid UTF-8 is still sent for service validation.
    const auto broken = editorSaveText(document, "{\"a\":", true);
    PHPVPN_CHECK(broken.document && *broken.document == "{\"a\":");
    const auto invalid = editorSaveText(document, std::string("{\"a\":\"\xC3\x28\"}"), true);
    PHPVPN_CHECK(!invalid.document);
    PHPVPN_CHECK(invalid.error == kEditorInvalidUnicode);
}

PHPVPN_TEST(TrayLinuxConfig, editor_save_notices_match_windows) {
    const SaveOperation operation{1, 2, 3, 4};
    const auto unknown = editorSaveNotice(result(operation, SaveOutcome::unknown, ErrorCode::response_invalid));
    PHPVPN_CHECK(unknown.warning);
    PHPVPN_CHECK_EQ(unknown.text, std::string(kEditorUnknownText));
    const auto notSent = editorSaveNotice(result(operation, SaveOutcome::notSent, ErrorCode::ipc_unavailable));
    PHPVPN_CHECK(!notSent.warning);
    PHPVPN_CHECK_EQ(notSent.text, std::string("The save was not sent. Your text has been preserved."));
    const auto rejected = editorSaveNotice(result(operation, SaveOutcome::rejected, ErrorCode::config_invalid_json));
    PHPVPN_CHECK_EQ(rejected.text,
                    std::string("The service rejected the configuration before publication. Your text has been preserved."));
    // Only a rejected result appends the closed validation description.
    const auto header = editorSaveNotice(result(operation, SaveOutcome::rejected, ErrorCode::config_invalid_value,
                                                "masterSetup.httpHeaders line 2: header name is invalid."));
    PHPVPN_CHECK_EQ(header.text, std::string(kEditorRejectedText) + "\n\nmasterSetup.httpHeaders line 2: header name is invalid.");
    const auto unknownWithText = editorSaveNotice(result(operation, SaveOutcome::unknown, ErrorCode::response_invalid, "x"));
    PHPVPN_CHECK_EQ(unknownWithText.text, std::string(kEditorUnknownText));
}

PHPVPN_TEST(TrayLinuxConfig, editor_save_notice_through_real_classification) {
    // The service's own error envelope for invalid JSON is a proven rejection.
    const SaveOperation operation{1, 1, 1, 10000};
    const std::string id = "12345678-1234-4234-9234-123456789abc";
    const auto invalidJson = classifySaveResponse(operation, id,
        IpcProtocol::buildError(id, Error(ErrorCode::config_invalid_json)), true, 5);
    PHPVPN_CHECK(invalidJson.outcome == SaveOutcome::rejected);
    PHPVPN_CHECK(editorSaveNotice(invalidJson).text == kEditorRejectedText);
    // A write failure never proves an unchanged file.
    const auto writeFailed = classifySaveResponse(operation, id,
        IpcProtocol::buildError(id, Error(ErrorCode::config_write_failed)), true, 5);
    PHPVPN_CHECK(writeFailed.outcome == SaveOutcome::unknown);
    PHPVPN_CHECK(editorSaveNotice(writeFailed).warning);
    // A reply after the original deadline is unknown even when it says ok.
    const auto late = classifySaveResponse(operation, id, IpcProtocol::buildAcknowledgement(id), true, 10000);
    PHPVPN_CHECK(late.outcome == SaveOutcome::unknown);
    const auto confirmed = classifySaveResponse(operation, id, IpcProtocol::buildAcknowledgement(id), true, 9999);
    PHPVPN_CHECK(confirmed.outcome == SaveOutcome::confirmed);
}

PHPVPN_TEST(TrayLinuxConfig, editor_default_reply_is_validated) {
    const auto valid = editorDefaultFromReply(defaultReply(makeDefaultEditorDocument()));
    PHPVPN_CHECK(valid.error.empty());
    PHPVPN_CHECK_EQ(valid.document, makeDefaultEditorDocument());
    const auto bom = editorDefaultFromReply(defaultReply("\xEF\xBB\xBF" + makeDefaultEditorDocument()));
    PHPVPN_CHECK(bom.error.empty() && bom.document == makeDefaultEditorDocument());
    const auto broken = editorDefaultFromReply(defaultReply("{\"masterSetup\":{}}"));
    PHPVPN_CHECK(broken.document.empty() && !broken.error.empty());
    const auto unavailable = editorDefaultFromReply(Result<WizardReply>(Error(ErrorCode::ipc_unavailable)));
    PHPVPN_CHECK_EQ(unavailable.error, std::string(errorCodeDescription(ErrorCode::ipc_unavailable)));
    WizardReply refused;
    refused.error = WizardIpc::safeError(Error(ErrorCode::busy));
    const auto busy = editorDefaultFromReply(Result<WizardReply>(refused));
    PHPVPN_CHECK_EQ(busy.error, refused.error->description + " " + refused.error->suggestedAction);
}

PHPVPN_TEST(TrayLinuxConfig, editor_state_single_save_with_original_deadline) {
    EditorState state;
    PHPVPN_CHECK(!state.beginSave(0, 100));
    state.open();
    PHPVPN_CHECK(state.saveEnabled() && state.resetEnabled() && state.closeEnabled() && state.fieldEditable());
    PHPVPN_CHECK(!state.beginSave(100, 100));  // the deadline must lie in the future
    const auto before = state.snapshot(true);
    PHPVPN_CHECK(before.open && before.dirty && !before.busy && !before.unknown);
    const auto operation = state.beginSave(100, 100 + kEditorSaveDeadlineMs);
    PHPVPN_CHECK(operation.has_value());
    PHPVPN_CHECK_EQ(operation->deadlineMs, 100u + kEditorSaveDeadlineMs);
    PHPVPN_CHECK(!state.beginSave(101, 200000));  // one save at a time
    PHPVPN_CHECK(!state.saveEnabled() && !state.resetEnabled() && !state.closeEnabled() && !state.fieldSensitive());
    const auto saving = state.snapshot(true);
    PHPVPN_CHECK(saving.busy && saving.generation != before.generation);
    // Foreign operations are ignored and change nothing.
    auto foreign = *operation;
    foreign.operationGeneration += 1;
    PHPVPN_CHECK(state.finishSave(result(foreign, SaveOutcome::confirmed)) == EditorState::Finish::ignored);
    PHPVPN_CHECK(state.saving());
    PHPVPN_CHECK(state.finishSave(result(*operation, SaveOutcome::rejected, ErrorCode::config_invalid_json)) ==
                 EditorState::Finish::failed);
    // A rejected save keeps the text editable and allows a new attempt.
    PHPVPN_CHECK(state.saveEnabled() && state.fieldEditable() && state.fieldSensitive());
    PHPVPN_CHECK(state.finishSave(result(*operation, SaveOutcome::confirmed)) == EditorState::Finish::ignored);
    const auto second = state.beginSave(200, 200 + kEditorSaveDeadlineMs);
    PHPVPN_CHECK(second && second->operationGeneration > operation->operationGeneration);
    PHPVPN_CHECK(state.finishSave(result(*second, SaveOutcome::confirmed)) == EditorState::Finish::confirmed);
    PHPVPN_CHECK(state.savedPending() && !state.saveEnabled() && !state.resetEnabled() && state.closeEnabled());
    PHPVPN_CHECK(!state.fieldSensitive());
}

PHPVPN_TEST(TrayLinuxConfig, editor_state_unknown_is_read_only_and_never_retried) {
    EditorState state;
    state.open();
    const auto operation = state.beginSave(0, kEditorSaveDeadlineMs);
    PHPVPN_CHECK(state.finishSave(result(*operation, SaveOutcome::unknown)) == EditorState::Finish::failed);
    PHPVPN_CHECK(state.unknown() && !state.fieldEditable() && state.fieldSensitive());
    PHPVPN_CHECK(!state.saveEnabled() && !state.resetEnabled() && state.closeEnabled());
    PHPVPN_CHECK(!state.beginSave(1, kEditorSaveDeadlineMs));
    const auto snapshot = state.snapshot(false);
    PHPVPN_CHECK(snapshot.unknown && !snapshot.dirty && !snapshot.busy);
    // Reopening the editor starts a clean identity.
    state.open();
    PHPVPN_CHECK(!state.unknown() && state.saveEnabled());
}

PHPVPN_TEST(TrayLinuxConfig, editor_state_generations_revoke_late_results) {
    EditorState state;
    state.open();
    auto operation = state.beginSave(0, kEditorSaveDeadlineMs);
    state.close();
    PHPVPN_CHECK(state.finishSave(result(*operation, SaveOutcome::confirmed)) == EditorState::Finish::ignored);
    PHPVPN_CHECK(!state.snapshot(true).open && !state.snapshot(true).dirty);
    state.open();
    operation = state.beginSave(0, kEditorSaveDeadlineMs);
    // A result of another draft generation is not this save.
    auto otherDraft = *operation;
    otherDraft.draftGeneration += 1;
    PHPVPN_CHECK(state.finishSave(result(otherDraft, SaveOutcome::confirmed)) == EditorState::Finish::ignored);
    state.endSession();
    PHPVPN_CHECK(state.ending() && !state.isOpen());
    PHPVPN_CHECK(state.finishSave(result(*operation, SaveOutcome::confirmed)) == EditorState::Finish::ignored);
    state.open();
    PHPVPN_CHECK(!state.beginSave(0, kEditorSaveDeadlineMs));
}

PHPVPN_TEST(TrayLinuxConfig, editor_state_reset_blocks_save_and_changes_generation) {
    EditorState state;
    state.open();
    const auto clean = state.snapshot(false);
    state.draftChanged();
    PHPVPN_CHECK(state.snapshot(false).generation != clean.generation);
    state.resetStarted();
    PHPVPN_CHECK(!state.saveEnabled() && !state.resetEnabled() && !state.fieldSensitive() && state.closeEnabled());
    PHPVPN_CHECK(!state.beginSave(0, kEditorSaveDeadlineMs));
    state.resetFinished();
    PHPVPN_CHECK(state.saveEnabled() && state.fieldSensitive());
}

// --- Communication contracts ------------------------------------------------------
PHPVPN_TEST(TrayLinuxConfig, contracts_save_label_and_footer) {
    PHPVPN_CHECK_EQ(contractsSaveLabel(0), std::string("Save"));
    PHPVPN_CHECK_EQ(contractsSaveLabel(1), std::string("Save (1 change)"));
    PHPVPN_CHECK_EQ(contractsSaveLabel(252), std::string("Save (252 changes)"));
    ContractsLifecycleSnapshot editing{true, true};
    auto footer = contractsFooter(editing, false, 2, true, false);
    PHPVPN_CHECK(footer.saveVisible && footer.saveEnabled && !footer.checkVisible && !footer.repeatVisible && footer.tableSensitive);
    PHPVPN_CHECK(!contractsFooter(editing, false, 0, true, false).saveEnabled);
    PHPVPN_CHECK(!contractsFooter(editing, false, 2, false, false).saveEnabled);  // blocking errors
    PHPVPN_CHECK(!contractsFooter(editing, true, 2, true, false).saveEnabled);    // already saved
    ContractsLifecycleSnapshot saving{true, true, true};
    PHPVPN_CHECK(!contractsFooter(saving, false, 2, false, false).tableSensitive);
    ContractsLifecycleSnapshot unknown{true, true, false, true};
    footer = contractsFooter(unknown, false, 2, false, false);
    PHPVPN_CHECK(!footer.saveVisible && footer.checkVisible && footer.checkEnabled && !footer.tableSensitive);
    unknown.checking = true;
    PHPVPN_CHECK(!contractsFooter(unknown, false, 2, false, false).checkEnabled);
    unknown.checking = false;
    PHPVPN_CHECK(contractsFooter(unknown, false, 2, false, true).repeatVisible);
}

PHPVPN_TEST(TrayLinuxConfig, contracts_message_priority) {
    const auto blocking = std::optional<ContractFocus>(ContractFocus{ip("10.99.99.7"), ContractColumn::key});
    const auto local = contractsMessage("Key contains unsupported text.", blocking, "Saving configuration...");
    PHPVPN_CHECK_EQ(local.text, std::string("Key contains unsupported text."));
    PHPVPN_CHECK(!local.validation);
    const auto validation = contractsMessage("", blocking, "notice");
    PHPVPN_CHECK_EQ(validation.text,
                    std::string("Key must contain exactly 64 lowercase hexadecimal characters.\nFirst invalid peer: 10.99.99.7"));
    PHPVPN_CHECK(validation.validation);
    const auto notice = contractsMessage("", std::nullopt, "Saving configuration...");
    PHPVPN_CHECK_EQ(notice.text, std::string("Saving configuration..."));
    PHPVPN_CHECK(!notice.validation);
}

PHPVPN_TEST(TrayLinuxConfig, contracts_pager_items) {
    PHPVPN_CHECK(contractsPagerItems(1, 0).empty());
    // 252 rows are 13 pages of 20.
    PHPVPN_CHECK_EQ(tablePageCount(252), 13);
    const auto first = contractsPagerItems(13, 0);
    std::vector<std::string> labels;
    for (const auto& item : first) labels.push_back(item.label);
    const std::vector<std::string> expected{"«", "‹", "1", "2", "3", "4", "5", "6", "…", "13", "›", "»"};
    PHPVPN_CHECK(labels == expected);
    PHPVPN_CHECK(!first[0].sensitive && !first[1].sensitive);   // already on the first page
    PHPVPN_CHECK(first[2].current && first[2].sensitive);        // current number stays focusable
    PHPVPN_CHECK_EQ(first[2].accessibleName, std::string("Page 1 of 13, current page"));
    PHPVPN_CHECK_EQ(first[3].accessibleName, std::string("Page 2 of 13"));
    PHPVPN_CHECK(first[8].page < 0 && !first[8].sensitive);      // ellipsis
    PHPVPN_CHECK_EQ(first.back().accessibleName, std::string("Last page"));
    PHPVPN_CHECK_EQ(first.back().page, 12);
    PHPVPN_CHECK_EQ(first[10].page, 1);
    const auto middle = contractsPagerItems(13, 6);
    PHPVPN_CHECK_EQ(middle[1].page, 5);
    PHPVPN_CHECK_EQ(middle[2].label, std::string("1"));
    PHPVPN_CHECK_EQ(middle[3].label, std::string("…"));
    const auto last = contractsPagerItems(13, 12);
    PHPVPN_CHECK(!last[last.size() - 1].sensitive && !last[last.size() - 2].sensitive);
}

PHPVPN_TEST(TrayLinuxConfig, contracts_edit_preflight_matches_contracts_edit) {
    std::vector<std::u16string> seen;
    const auto accept = [&](std::u16string_view proposed) {
        seen.emplace_back(proposed);
        return proposed.size() <= 8;
    };
    // Insertion at the caret.
    PHPVPN_CHECK(contractsEditAccept(u"abcd", 2, 2, u"xy", false, 8, accept));
    PHPVPN_CHECK(seen.back() == u"abxycd");
    // Replacing a selection counts only the remaining text.
    PHPVPN_CHECK(contractsEditAccept(u"abcdefgh", 0, 8, u"12345678", false, 8, accept));
    PHPVPN_CHECK(seen.back() == u"12345678");
    // An over-budget insertion is rejected as a whole with the bound + 1 sentinel.
    PHPVPN_CHECK(!contractsEditAccept(u"abcdefg", 7, 7, u"xy", false, 8, accept));
    PHPVPN_CHECK(seen.back() == std::u16string(9, u'x'));
    PHPVPN_CHECK(!contractsEditAccept(u"", 0, 0, std::u16string(9, u'a'), true, 8, accept));
    PHPVPN_CHECK(seen.back() == std::u16string(9, u'x'));
    // A NUL is always reported as a lone NUL, even at the length limit.
    const auto nul = std::u16string(u"a\0b", 3);
    PHPVPN_CHECK(!contractsEditAccept(u"12345678", 8, 8, nul, false, 8, [&](std::u16string_view proposed) {
        seen.emplace_back(proposed);
        return false;
    }));
    PHPVPN_CHECK(seen.back() == std::u16string(u"\0", 1));
    // Out-of-range selections clamp.
    PHPVPN_CHECK(contractsEditAccept(u"ab", 5, 9, u"c", false, 8, accept));
    PHPVPN_CHECK(seen.back() == u"abc");
    PHPVPN_CHECK(!contractsEditAccept(u"ab", 0, 0, u"c", false, 8, {}));
}

PHPVPN_TEST(TrayLinuxConfig, contracts_utf16_offsets_and_key_text) {
    const std::string text = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z";  // a é € 😀 z
    PHPVPN_CHECK_EQ(contractsUtf16Offset(text, 0), 0u);
    PHPVPN_CHECK_EQ(contractsUtf16Offset(text, 3), 3u);
    PHPVPN_CHECK_EQ(contractsUtf16Offset(text, 4), 5u);   // the emoji is a surrogate pair
    PHPVPN_CHECK_EQ(contractsUtf16Offset(text, 99), 6u);
    PHPVPN_CHECK_EQ(contractsUtf16(text).size(), 6u);
    PHPVPN_CHECK(contractsUtf16("\xC3\x28").empty());
    std::string utf8;
    PHPVPN_CHECK(contractsKeyTextSupported(u"00ff", utf8) && utf8 == "00ff");
    PHPVPN_CHECK(!contractsKeyTextSupported(u"ab\ncd", utf8));
    PHPVPN_CHECK(!contractsKeyTextSupported(u"ab\rcd", utf8));
    PHPVPN_CHECK(!contractsKeyTextSupported(std::u16string(u"a\0", 2), utf8));
    const char16_t lone[]{u'a', static_cast<char16_t>(0xD800)};
    PHPVPN_CHECK(!contractsKeyTextSupported(std::u16string_view(lone, 2), utf8));
}

PHPVPN_TEST(TrayLinuxConfig, password_sha_notices_and_random_password) {
    PHPVPN_CHECK(passwordShaNotice(PasswordKeyError::none, true).empty());
    PHPVPN_CHECK_EQ(std::string(passwordShaNotice(PasswordKeyError::tooLong, true)),
                    std::string("Password must contain at most 255 Unicode characters."));
    PHPVPN_CHECK_EQ(std::string(passwordShaNotice(PasswordKeyError::unsupportedControl, true)),
                    std::string("Password contains an unsupported control character."));
    PHPVPN_CHECK_EQ(std::string(passwordShaNotice(PasswordKeyError::cryptoFailed, true)),
                    std::string("The SHA-256 key could not be generated."));
    PHPVPN_CHECK_EQ(std::string(passwordShaNotice(PasswordKeyError::tooLong, false)),
                    std::string("Enable this contract to apply the key. Your password is kept."));

    ScriptedRandom crypto;
    std::u16string password = u"kept";
    PHPVPN_CHECK(contractsRandomPassword(crypto, password));
    PHPVPN_CHECK_EQ(crypto.calls, 2);
    PHPVPN_CHECK_EQ(password.size(), 64u);
    PHPVPN_CHECK(password.substr(0, 3) == u"ghj");
    PHPVPN_CHECK(password.find_first_not_of(u"ghjkmnpqrstuvwxyz") == std::u16string::npos);
    crypto.failRandom = true;
    std::u16string untouched = u"kept";
    PHPVPN_CHECK(!contractsRandomPassword(crypto, untouched));
    PHPVPN_CHECK(untouched == u"kept");
    // The pair default hashes to the Wizard's deterministic key.
    PasswordKeyModel model;
    PHPVPN_CHECK(model.update(crypto, u"") == PasswordKeyError::none);
    PHPVPN_CHECK_EQ(std::string(model.key()), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

// --- Contracts save machine over the real IPC channel and ConfigStore ---------------
PHPVPN_TEST(TrayLinuxConfig, contracts_save_confirmed_and_unknown_check_through_ipc) {
    TempDir dir;
    PHPVPN_CHECK(!dir.path().empty());
    // The store and the server thread use their own provider, the model another one.
    auto crypto = std::move(createPlatformCryptoProvider()).value();
    auto storeCrypto = std::move(createPlatformCryptoProvider()).value();
    LinuxConfigFilePolicy policy;
    policy.expectedOwner = geteuid();
    auto files = createLinuxConfigFiles(dir.path() + "/config.json", policy);
    PHPVPN_REQUIRE_OK(files);
    ConfigStore store(*storeCrypto, std::move(files).value());
    PHPVPN_REQUIRE_OK(store.recover());
    PHPVPN_REQUIRE_OK(store.createDefault());
    bool foreign = false;
    std::mutex mutex;
    IpcSocketServerOptions options;
    options.socketPath = dir.path() + "/control.sock";
    options.expectedOwnerUid = geteuid();
    IpcSocketServer server([&](std::string_view request) -> std::string {
        std::lock_guard lock(mutex);
        if (auto ipc = IpcProtocol::parseRequest(request); ipc && ipc->command == IpcCommand::set_config) {
            auto commitId = storeCrypto->randomUuidV4();
            if (!commitId) return IpcProtocol::buildError(ipc->requestId, commitId.error());
            auto commit = store.save(ipc->config, commitId.value());
            if (!commit) return IpcProtocol::buildError(ipc->requestId, commit.error());
            return IpcProtocol::buildAcknowledgement(foreign ? "00000000-0000-4000-8000-000000000001" : ipc->requestId);
        }
        if (auto wizard = WizardIpc::parseRequest(request); wizard && wizard->command == WizardCommand::get_config_document) {
            auto snapshot = store.read();
            return WizardIpc::buildResult(wizard->requestId, JsonValue::makeObject({
                {"configPath", JsonValue::makeString(dir.path() + "/config.json")},
                {"availability", JsonValue::makeString(configAvailabilityName(snapshot->availability))},
                {"reasonCode", JsonValue::makeNull()}, {"revisionToken", JsonValue::makeString(snapshot->revisionToken)},
                {"document", JsonValue::makeString(*snapshot->document)}}));
        }
        return "{}";
    }, options);
    PHPVPN_REQUIRE_OK(server.start());
    Pump pump;
    const IpcEndpoint endpoint{options.socketPath, geteuid()};
    IpcChannel writer(endpoint, pump.dispatcher()), checker(endpoint, pump.dispatcher());

    auto document = store.read();
    PHPVPN_REQUIRE_OK(document);
    ContractsSaveMachine machine;
    PHPVPN_REQUIRE_OK(machine.load(*crypto, *document->document, dir.path() + "/config.json", document->revisionToken));
    auto* model = machine.model();
    const auto peer = ip("10.99.99.3");
    PHPVPN_REQUIRE_OK(model->setKey(peer, std::string(64, 'b')));
    PHPVPN_REQUIRE_OK(model->setEnabled(peer, true));
    machine.changed();
    PHPVPN_REQUIRE_OK(machine.prepare(false, checker.busy()));
    ContractsEvent event = ContractsEvent::none;
    PHPVPN_CHECK(machine.dispatch(static_cast<std::uint64_t>(monotonicMs()), [&](const SaveOperation& operation, const std::string& payload) {
        return writer.requestSave(operation, payload, [&](const SaveResult& saved) { event = machine.saveFinished(saved); });
    }));
    PHPVPN_CHECK(machine.saving() && machine.snapshot(false).saving);
    PHPVPN_CHECK(pump.until([&] { return event != ContractsEvent::none; }));
    PHPVPN_CHECK(event == ContractsEvent::saved);
    const auto stored = store.read();
    PHPVPN_CHECK(stored && stored->document->find("\"10.99.99.3\"") != std::string::npos);
    machine.close();

    // Lost confirmation: the file changed, the reply is foreign -> unknown; the
    // explicit check compares the whole DOM and finds the published draft.
    foreign = true;
    document = store.read();
    PHPVPN_REQUIRE_OK(machine.load(*crypto, *document->document, dir.path() + "/config.json", document->revisionToken));
    model = machine.model();
    PHPVPN_REQUIRE_OK(model->setEnabled(peer, false));
    machine.changed();
    PHPVPN_REQUIRE_OK(machine.prepare(false, checker.busy()));
    event = ContractsEvent::none;
    PHPVPN_CHECK(machine.dispatch(static_cast<std::uint64_t>(monotonicMs()), [&](const SaveOperation& operation, const std::string& payload) {
        return writer.requestSave(operation, payload, [&](const SaveResult& saved) { event = machine.saveFinished(saved); });
    }));
    PHPVPN_CHECK(pump.until([&] { return event != ContractsEvent::none; }));
    PHPVPN_CHECK(event == ContractsEvent::unknown);
    PHPVPN_CHECK(machine.snapshot(false).unknown);
    PHPVPN_CHECK(contractsFooter(machine.snapshot(false), false, 1, false, machine.canRepeat(false)).checkVisible);
    event = ContractsEvent::none;
    PHPVPN_CHECK(machine.check(checker.busy(), [&](std::uint64_t generation) {
        WizardRequest request;
        request.command = WizardCommand::get_config_document;
        return checker.requestWizard(request, [&, generation](WizardResult& reply) { event = machine.checkFinished(generation, reply.reply); });
    }));
    PHPVPN_CHECK(pump.until([&] { return event != ContractsEvent::none; }));
    PHPVPN_CHECK(event == ContractsEvent::matched);
    PHPVPN_CHECK_EQ(machine.notice(), std::string("The current configuration matches your changes."));
    machine.close();
    server.stop();
}
}  // namespace phpvpn::tray
