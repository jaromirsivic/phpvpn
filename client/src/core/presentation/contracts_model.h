#pragma once

#include "core/config/contracts_document.h"

namespace phpvpn {
enum class ContractColumn { enabled, key, random, password };
struct ContractFocus {
    Ipv4Address remoteIp;
    ContractColumn column = ContractColumn::key;
    bool operator==(const ContractFocus&) const = default;
};
struct ContractsIdentity {};
// Value captured when opening a child/confirmation. No mutable target or index.
class ContractTarget {
public:
    Ipv4Address remoteIp() const noexcept { return remoteIp_; }
    const std::string& contractId() const noexcept { return contractId_; }
    std::string passwordTitle() const { return "Password SHA - Contract " + contractId_; }
    std::string randomConfirmation() const {
        return "Replace the key for " + contractId_ + " with a new random key?";
    }
private:
    friend class ContractsModel;
    Ipv4Address remoteIp_;
    std::string contractId_;
    std::weak_ptr<const ContractsIdentity> identity_;
    std::uint64_t generation_ = 0;
};

class ContractRow {
public:
    Ipv4Address remoteIp() const noexcept { return remoteIp_; }
    bool enabled() const noexcept { return enabled_; }
    std::string_view keyText() const noexcept;
    bool keyValid() const noexcept { return keyValid_; }
    bool dirty() const noexcept;
private:
    friend class ContractsModel;
    Ipv4Address remoteIp_;
    bool enabled_ = false, originalEnabled_ = false, keyValid_ = true;
    SecureBuffer key_, originalKey_;
};

// Single-owner, externally serialized calls. No loading/transport/saveUnknown
// policy here: the later controller owns those states and all deadlines.
class ContractsModel {
public:
    static Result<std::unique_ptr<ContractsModel>> create(CryptoProvider& crypto,
        std::string_view document, std::string configPath = {}, std::string revisionToken = {});
    ContractsModel(const ContractsModel&) = delete;
    ContractsModel& operator=(const ContractsModel&) = delete;
    ~ContractsModel() = default;
    std::span<const ContractRow> rows() const noexcept { return rows_; }
    const ContractRow* row(Ipv4Address remoteIp) const noexcept;
    Ipv4Address localIp() const noexcept { return localIp_; }
    const std::string& configPath() const noexcept { return configPath_; }
    const std::string& revisionToken() const noexcept { return revisionToken_; } // informational only
    bool dirty() const noexcept;
    std::size_t blockingErrors() const noexcept;
    std::optional<ContractFocus> firstBlockingFocus() const noexcept;
    std::size_t keyTextBytes() const noexcept { return keyTextBytes_; }
    bool canPrepareSave() const noexcept { return !closed_ && !candidate_ && blockingErrors() == 0; }
    bool frozen() const noexcept { return candidate_ != nullptr; }
    bool closed() const noexcept { return closed_; }
    Status setEnabled(Ipv4Address remoteIp, bool enabled);
    Status setKey(Ipv4Address remoteIp, std::string_view key);
    Status setFocus(std::optional<ContractFocus> focus);
    const std::optional<ContractFocus>& focus() const noexcept { return focus_; }
    Result<ContractTarget> target(Ipv4Address remoteIp) const;
    Status confirmRandom(CryptoProvider& crypto, const ContractTarget& target, bool confirmed);
    Status applyPasswordKey(const ContractTarget& target, std::string_view key);
    // Failure preserves draft and generation. Success freezes editing, retires
    // existing child targets and returns one immutable, independently owned DOM.
    Result<std::shared_ptr<const ContractsCandidate>> prepareSave(const ContractsEnvelopeSizer& envelopeSizer);
    std::uint64_t saveGeneration() const noexcept { return generation_; }
    const std::shared_ptr<const ContractsCandidate>& candidate() const noexcept { return candidate_; }
    // Controller calls only after a correlated terminal rejection/not-sent.
    // For unknown it retains the candidate until an explicit user decision.
    bool resumeEditing(std::uint64_t saveGeneration);
    void close() noexcept; // caller has already handled dirty/unknown confirmation
private:
    ContractsModel() = default;
    ContractRow* editableRow(Ipv4Address remoteIp) noexcept;
    bool accepts(const ContractTarget& target) const noexcept;
    std::unique_ptr<ContractsDocument> document_;
    Ipv4Address localIp_;
    std::vector<ContractRow> rows_;
    std::string configPath_, revisionToken_;
    std::optional<ContractFocus> focus_;
    std::shared_ptr<const ContractsIdentity> identity_ = std::make_shared<ContractsIdentity>();
    std::shared_ptr<const ContractsCandidate> candidate_;
    std::uint64_t generation_ = 1;
    std::size_t keyTextBytes_ = 0;
    bool closed_ = false;
};
} // namespace phpvpn
