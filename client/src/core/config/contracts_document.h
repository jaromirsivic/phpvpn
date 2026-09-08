#pragma once

#include <functional>
#include <memory>
#include <span>
#include "core/config/config_document.h"

namespace phpvpn {
inline constexpr std::size_t kMaxContractRows = 252;
inline constexpr std::size_t kMaxContractKeyTextBytes = kMaxConfigFileBytes;
inline constexpr std::string_view kContractKeyError =
    "Key must contain exactly 64 lowercase hexadecimal characters.";

bool isValidContractKey(std::string_view text) noexcept;
Result<std::size_t> checkedContractPeerCount(const Ipv4Cidr& cidr, Ipv4Address localIp);
// Checked before allocating/replacing text, including size_t overflow cases.
Result<std::size_t> checkedContractKeyTextSize(std::size_t total, std::size_t oldSize,
                                            std::size_t newSize);
Status validateContractsEnvelopeSize(std::size_t encodedBytes);
// The integration adapter measures the actual encoded request, including its
// envelope. No transport, request schema or endpoint is owned by this module.
using ContractsEnvelopeSizer = std::function<Result<std::size_t>(std::string_view)>;

struct ContractEdit {
    Ipv4Address remoteIp;
    bool enabled = false;
    std::string_view keyText;
};

class ContractsCandidate {
public:
    ~ContractsCandidate();
    ContractsCandidate(const ContractsCandidate&) = delete;
    ContractsCandidate& operator=(const ContractsCandidate&) = delete;
    std::string_view text() const noexcept;
    const JsonValue& dom() const noexcept { return dom_; }
    // Object order is insignificant, types and absent/present remain significant.
    Result<bool> matchesDocument(std::string_view currentDocument) const;
private:
    friend class ContractsDocument;
    ContractsCandidate() = default;
    JsonValue dom_;
    SecureBuffer text_;
};

class ContractsDocument {
public:
    static Result<std::unique_ptr<ContractsDocument>> parse(std::string_view text);
    ~ContractsDocument();
    ContractsDocument(const ContractsDocument&) = delete;
    ContractsDocument& operator=(const ContractsDocument&) = delete;
    const JsonValue& dom() const noexcept { return dom_; }
    Ipv4Address localIp() const noexcept { return localIp_; }
    const Ipv4Cidr& cidr() const noexcept { return cidr_; }
    // Borrowed until this document is destroyed. Absent and present are distinct.
    std::optional<std::string_view> existingKey(Ipv4Address remoteIp) const;
    // Complete row snapshot required, in any order. Duplicates, omissions and
    // foreign/self peers are rejected. Disabled key text is never published.
    Result<std::shared_ptr<const ContractsCandidate>> buildCandidate(
        std::span<const ContractEdit> rows, const ContractsEnvelopeSizer& envelopeSizer) const;
private:
    ContractsDocument() = default;
    JsonValue dom_;
    Ipv4Address localIp_;
    Ipv4Cidr cidr_;
};
} // namespace phpvpn
