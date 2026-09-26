// GTK-free presentation rules of the Linux Communication contracts and Password SHA
// windows (tray_UX.md UX10-UX12), taken from the Win32 contracts_window.cpp,
// contracts_edit.h and password_sha_window.cpp. The portable ContractsModel,
// ContractsSaveMachine and PasswordKeyModel own the data; this header only adds the
// texts, footer states, pager items and the single-line edit preflight that the GTK
// adapters apply to GtkEntry insertions.
#pragma once
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts_save_machine.h"
#include "core/presentation/contracts_model.h"
#include "core/presentation/password_key_model.h"

namespace phpvpn::tray {
inline constexpr std::string_view kContractsTitle = "Communication contracts";
inline constexpr std::string_view kContractsDescription =
    "Manage secure communication between this computer and other virtual IP addresses using end-to-end encryption.";
inline constexpr std::string_view kContractsLocalLabel = "Your virtual IP";
inline constexpr std::string_view kContractsNextConnection = "Changes take effect on the next connection.";
inline constexpr std::string_view kContractsCheck = "Check current configuration";
inline constexpr std::string_view kContractsRepeat = "Save draft again";
inline constexpr std::string_view kContractsRepeatQuestion = "Replace the current whole configuration with your draft?";
inline constexpr std::string_view kContractsMatched = "The current configuration matches your changes.";
inline constexpr std::string_view kContractsDiscard = "Discard unsaved changes?";
inline constexpr std::string_view kContractsUnknownClose =
    "The file may already have changed. Close without checking the save result?";
inline constexpr std::string_view kContractsUnsupportedKey = "Key contains unsupported text.";
inline constexpr std::string_view kContractsKeyBudget =
    "The combined Key text exceeds 524288 bytes. The previous value has been kept.";
inline constexpr std::string_view kContractsPageFailed =
    "The page could not be displayed. Your draft has been kept. Try another page.";
inline constexpr std::string_view kContractsPasswordFailed = "Password SHA could not be opened.";
inline constexpr std::string_view kContractsInvalidKeyDescription =
    "Invalid: Key must contain exactly 64 lowercase hexadecimal characters.";
inline constexpr const char* kContractsColumns[4]{"Contract", "Enabled", "Key", "Actions"};
inline constexpr std::string_view kContractsChange = "Change";
// Native limit of a Key field in UTF-16 units, as EM_SETLIMITTEXT 128 on Windows.
inline constexpr std::size_t kContractsKeyBound = 128;

inline constexpr std::string_view kPasswordHeading = "Generate key from password";
inline constexpr std::string_view kPasswordDescription = "Enter a password to create a SHA-256 communication key.";
inline constexpr std::string_view kPasswordLabel = "Password";
inline constexpr std::string_view kPasswordHint = "The key updates automatically as you type.";
inline constexpr std::string_view kPasswordKeyLabel = "SHA-256 key";
inline constexpr std::string_view kPasswordCue = "CHANGE-THIS-PASSWORD";
inline constexpr std::string_view kPasswordRandomFailed =
    "A random password could not be generated. The current password has been kept.";
// 255 scalars are at most 510 UTF-16 units (tray_UX.md UX11).
inline constexpr std::size_t kPasswordBound = 510;

// "Save", "Save (1 change)", "Save (N changes)".
std::string contractsSaveLabel(std::size_t changes);

// Visibility and enabled states of the Win32 ContractsWindow::refresh().
struct ContractsFooter {
    bool saveVisible = true, saveEnabled = false;
    bool checkVisible = false, checkEnabled = true;
    bool repeatVisible = false;
    bool tableSensitive = true;
};
ContractsFooter contractsFooter(const ContractsLifecycleSnapshot& snapshot, bool saved, std::size_t changes,
                                bool canPrepareSave, bool canRepeat);

// The message above the footer: a local error first, then the first blocking peer,
// then the save machine notice. validation selects the strong error style.
struct ContractsMessage {
    std::string text;
    bool validation = false;
};
ContractsMessage contractsMessage(std::string_view localError, std::optional<ContractFocus> blocking,
                                  std::string_view notice);

// Pager of the shared table pagination model: first, previous, numbered (with
// ellipses), next and last page. page < 0 marks an ellipsis.
struct ContractsPagerItem {
    std::string label;
    int page = -1;
    std::string accessibleName;
    bool current = false;
    bool sensitive = false;
};
std::vector<ContractsPagerItem> contractsPagerItems(int count, int current);

// Win32 ContractsEdit::accept on UTF-16 text: rejects a whole over-budget insertion
// before it reaches the field. A NUL is passed as a lone "\0" and an over-budget
// proposal as bound + 1 'x' characters, so the check (Key model or Password SHA)
// reports the matching error. [first, last) is the replaced range of current.
using ContractsEditCheck = std::function<bool(std::u16string_view proposed)>;
bool contractsEditAccept(std::u16string_view current, std::size_t first, std::size_t last,
                         std::u16string_view insertion, bool replaceAll, std::size_t bound,
                         const ContractsEditCheck& check);
// UTF-16 offset of a character (code point) offset in valid UTF-8, as GtkEditable
// positions count characters. Offsets past the end clamp to the end.
std::size_t contractsUtf16Offset(std::string_view utf8, std::size_t characters);
// Lenient conversion for GTK text (always valid UTF-8); invalid input yields empty.
std::u16string contractsUtf16(std::string_view utf8);

// Key text accepted by acceptKey: valid Unicode without NUL, CR or LF. The UTF-8 is
// written to utf8 on success.
bool contractsKeyTextSupported(std::u16string_view text, std::string& utf8);

// Notice line of Password SHA; empty when there is nothing to report.
std::string_view passwordShaNotice(PasswordKeyError error, bool targetEnabled);
// 64 characters of "ghjkmnpqrstuvwxyz" from the CSPRNG without modulo bias. False
// (password untouched) when random bytes could not be obtained.
bool contractsRandomPassword(CryptoProvider& crypto, std::u16string& password);
}  // namespace phpvpn::tray
