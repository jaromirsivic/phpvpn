#include "core/presentation/contracts_model.h"

#include <algorithm>
#include <limits>
#include "core/base/hex.h"

namespace phpvpn {
namespace {
struct KeyTextWiper {
    std::string& text;
    ~KeyTextWiper() { secureWipe(text.data(), text.size()); }
};
} // namespace
std::string_view ContractRow::keyText() const noexcept {
    return key_.empty() ? std::string_view{} : std::string_view(reinterpret_cast<const char*>(key_.data()), key_.size());
}
bool ContractRow::dirty() const noexcept {
    return enabled_ != originalEnabled_ || !key_.equals(originalKey_);
}
Result<std::unique_ptr<ContractsModel>> ContractsModel::create(CryptoProvider&,
    std::string_view document, std::string configPath, std::string revisionToken) {
    auto parsed = ContractsDocument::parse(document);
    if (!parsed) return parsed.error();
    auto count = checkedContractPeerCount(parsed.value()->cidr(), parsed.value()->localIp());
    if (!count) return count.error();
    auto model = std::unique_ptr<ContractsModel>(new ContractsModel);
    model->document_ = std::move(*parsed);
    model->localIp_ = model->document_->localIp();
    model->configPath_ = std::move(configPath);
    model->revisionToken_ = std::move(revisionToken);
    model->rows_.reserve(*count);
    // uint64_t induction also handles the upper end of IPv4 without wraparound.
    for (std::uint64_t ip = model->document_->cidr().firstClientAddress().value();
         ip <= model->document_->cidr().lastClientAddress().value(); ++ip) {
        const Ipv4Address remote(static_cast<std::uint32_t>(ip));
        if (remote == model->localIp()) continue;
        ContractRow row;
        row.remoteIp_ = remote;
        if (const auto key = model->document_->existingKey(remote)) {
            row.key_.assign(asBytes(*key));
            row.enabled_ = row.originalEnabled_ = true;
        }
        // Missing JSON keys stay genuinely absent in both working and baseline
        // buffers. Default derivation is an explicit Password SHA action only.
        row.keyValid_ = isValidContractKey(row.keyText());
        row.originalKey_ = row.key_;
        model->keyTextBytes_ += row.key_.size();
        model->rows_.push_back(std::move(row));
    }
    return model;
}
const ContractRow* ContractsModel::row(Ipv4Address remoteIp) const noexcept {
    const auto found = std::lower_bound(rows_.begin(), rows_.end(), remoteIp,
        [](const ContractRow& item, Ipv4Address ip) { return item.remoteIp() < ip; });
    return found != rows_.end() && found->remoteIp() == remoteIp ? &*found : nullptr;
}
ContractRow* ContractsModel::editableRow(Ipv4Address remoteIp) noexcept {
    return closed_ || frozen() ? nullptr : const_cast<ContractRow*>(row(remoteIp));
}
bool ContractsModel::dirty() const noexcept {
    return std::any_of(rows_.begin(), rows_.end(), [](const auto& row) { return row.dirty(); });
}
std::size_t ContractsModel::blockingErrors() const noexcept {
    return static_cast<std::size_t>(std::count_if(rows_.begin(), rows_.end(),
        [](const auto& row) { return row.enabled() && !row.keyValid(); }));
}
std::optional<ContractFocus> ContractsModel::firstBlockingFocus() const noexcept {
    for (const auto& row : rows_) if (row.enabled() && !row.keyValid())
        return ContractFocus{row.remoteIp(), ContractColumn::key};
    return std::nullopt;
}
Status ContractsModel::setEnabled(Ipv4Address remoteIp, bool enabled) {
    auto* item = editableRow(remoteIp);
    if (!item) return fail(ErrorCode::invalid_argument);
    item->enabled_ = enabled;
    return ok();
}
Status ContractsModel::setKey(Ipv4Address remoteIp, std::string_view key) {
    auto* item = editableRow(remoteIp);
    if (!item) return fail(ErrorCode::invalid_argument);
    const auto size = checkedContractKeyTextSize(keyTextBytes_, item->key_.size(), key.size());
    if (!size) return size.error();
    if (!isStrictUtf8(key)) return fail(ErrorCode::config_encoding_error);
    // Allocate first, then replace. The input may alias this row's current key.
    SecureBuffer replacement(asBytes(key));
    const bool valid = isValidContractKey(key);
    item->key_ = std::move(replacement);
    item->keyValid_ = valid;
    keyTextBytes_ = *size;
    return ok();
}
Status ContractsModel::setFocus(std::optional<ContractFocus> focus) {
    if (closed_ || (focus && (!row(focus->remoteIp) ||
        static_cast<unsigned>(focus->column) > static_cast<unsigned>(ContractColumn::password))))
        return fail(ErrorCode::invalid_argument);
    focus_ = focus;
    return ok();
}
Result<ContractTarget> ContractsModel::target(Ipv4Address remoteIp) const {
    if (closed_ || frozen() || !row(remoteIp)) return Error(ErrorCode::invalid_argument);
    ContractTarget result;
    result.remoteIp_ = remoteIp;
    auto first = localIp(), second = remoteIp;
    if (second < first) std::swap(first, second);
    result.contractId_ = first.toString() + "_" + second.toString();
    result.identity_ = identity_;
    result.generation_ = generation_;
    return result;
}
bool ContractsModel::accepts(const ContractTarget& target) const noexcept {
    return !closed_ && !frozen() && target.identity_.lock() == identity_ &&
        target.generation_ == generation_ && row(target.remoteIp_) != nullptr;
}
Status ContractsModel::confirmRandom(CryptoProvider& crypto, const ContractTarget& target, bool confirmed) {
    if (!accepts(target)) return fail(ErrorCode::invalid_argument);
    if (!confirmed) return ok();
    SecureBuffer bytes(kAesKeySize);
    if (!crypto.randomBytes(bytes.mutableSpan())) return fail(ErrorCode::crypto_failed);
    auto key = toLowercaseHex(bytes.span());
    KeyTextWiper wipe{key};
    return applyPasswordKey(target, key);
}
Status ContractsModel::applyPasswordKey(const ContractTarget& target, std::string_view key) {
    if (!accepts(target)) return fail(ErrorCode::invalid_argument);
    if (!isValidContractKey(key)) return fail(ErrorCode::config_invalid_key, std::string(kContractKeyError));
    PHPVPN_TRY(setKey(target.remoteIp_, key));
    return ok();
}
Result<std::shared_ptr<const ContractsCandidate>> ContractsModel::prepareSave(const ContractsEnvelopeSizer& envelopeSizer) {
    if (!canPrepareSave()) return Error(blockingErrors() ? ErrorCode::config_invalid_key : ErrorCode::invalid_argument);
    if (generation_ == (std::numeric_limits<std::uint64_t>::max)()) return Error(ErrorCode::internal);
    std::vector<ContractEdit> edits;
    edits.reserve(rows_.size());
    for (const auto& row : rows_) edits.push_back({row.remoteIp(), row.enabled(), row.keyText()});
    auto built = document_->buildCandidate(edits, envelopeSizer);
    if (!built) return built.error();
    candidate_ = std::move(*built);
    ++generation_;
    return candidate_;
}
bool ContractsModel::resumeEditing(std::uint64_t saveGeneration) {
    if (closed_ || !candidate_ || saveGeneration != generation_) return false;
    candidate_.reset();
    return true;
}
void ContractsModel::close() noexcept {
    closed_ = true;
    identity_.reset();
    candidate_.reset();
    rows_.clear();
    document_.reset();
    focus_.reset();
    keyTextBytes_ = 0;
}
} // namespace phpvpn
