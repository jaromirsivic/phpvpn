#include "core/runtime/tunnel_session.h"

#include <fstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <system_error>

#include "core/base/logger.h"
#include "core/base/utf8.h"
#include "core/protocol/json.h"

namespace phpvpn {
namespace {

constexpr const char* kComponent = "tunnel-session";

Result<std::string> readWholeFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return Error(ErrorCode::journal_missing, "the ownership journal could not be opened");
    }
    std::string content((std::istreambuf_iterator<char>(stream)),
                        std::istreambuf_iterator<char>());
    if (!stream.eof() && stream.fail()) {
        return Error(ErrorCode::journal_corrupt, "the ownership journal could not be read");
    }
    return content;
}

Status writeWholeFile(const std::filesystem::path& path, std::string_view content) {
    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);

    // The journal is written to a temporary file and then renamed, so a crash
    // during the write cannot leave a half written record behind.
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return Error(ErrorCode::config_write_failed, "the ownership journal could not be written");
        }
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        stream.flush();
        if (!stream) {
            return Error(ErrorCode::config_write_failed, "the ownership journal could not be written");
        }
    }
    std::filesystem::rename(temporary, path, code);
    if (code) {
        std::filesystem::remove(temporary, code);
        return Error(ErrorCode::config_write_failed, "the ownership journal could not be replaced");
    }
    return Status::success();
}

const JsonValue* requiredMember(const JsonValue& object, std::string_view name, JsonType type) {
    const JsonValue* value = object.find(name);
    if (value == nullptr || value->type() != type) {
        return nullptr;
    }
    return value;
}

}  // namespace

std::string OwnershipJournal::serialize() const {
    JsonObject members;
    members.emplace_back("adapterName", JsonValue::makeString(adapterName));
    members.emplace_back("adapterGuid", JsonValue::makeString(adapterGuid));
    members.emplace_back("interfaceLuid",
                         JsonValue::makeString(std::to_string(interfaceLuid)));
    members.emplace_back("adapterCreated", JsonValue::makeBool(adapterCreated));
    members.emplace_back("addressApplied", JsonValue::makeBool(addressApplied));
    members.emplace_back("routeApplied", JsonValue::makeBool(routeApplied));
    members.emplace_back("address", JsonValue::makeString(address.toString()));
    members.emplace_back("prefixLength", JsonValue::makeInteger(prefixLength));
    members.emplace_back("routeDestination", JsonValue::makeString(routeDestination.toString()));
    members.emplace_back("routeMetric", JsonValue::makeInteger(routeMetric));
    return Json::serializePretty(JsonValue::makeObject(std::move(members)));
}

Result<OwnershipJournal> OwnershipJournal::parse(std::string_view text) {
    auto document = Json::parse(text);
    if (!document.ok()) {
        return Error(ErrorCode::journal_corrupt, "the ownership journal is not valid JSON");
    }
    const JsonValue& root = document.value();
    if (!root.isObject()) {
        return Error(ErrorCode::journal_corrupt, "the ownership journal is not an object");
    }

    const JsonValue* adapterName = requiredMember(root, "adapterName", JsonType::string);
    const JsonValue* adapterGuid = requiredMember(root, "adapterGuid", JsonType::string);
    const JsonValue* luid = requiredMember(root, "interfaceLuid", JsonType::string);
    const JsonValue* adapterCreated = requiredMember(root, "adapterCreated", JsonType::boolean);
    const JsonValue* addressApplied = requiredMember(root, "addressApplied", JsonType::boolean);
    const JsonValue* routeApplied = requiredMember(root, "routeApplied", JsonType::boolean);
    const JsonValue* address = requiredMember(root, "address", JsonType::string);
    const JsonValue* prefixLength = requiredMember(root, "prefixLength", JsonType::integer);
    const JsonValue* routeDestination = requiredMember(root, "routeDestination", JsonType::string);
    const JsonValue* routeMetric = requiredMember(root, "routeMetric", JsonType::integer);

    if (adapterName == nullptr || adapterGuid == nullptr || luid == nullptr ||
        adapterCreated == nullptr || addressApplied == nullptr || routeApplied == nullptr ||
        address == nullptr || prefixLength == nullptr || routeDestination == nullptr ||
        routeMetric == nullptr) {
        return Error(ErrorCode::journal_corrupt, "the ownership journal is incomplete");
    }

    OwnershipJournal journal;
    journal.adapterName = adapterName->stringValue();
    journal.adapterGuid = adapterGuid->stringValue();
    try {
        journal.interfaceLuid = std::stoull(luid->stringValue());
    } catch (const std::exception&) {
        return Error(ErrorCode::journal_corrupt, "the recorded interface identifier is not a number");
    }
    journal.adapterCreated = adapterCreated->boolValue();
    journal.addressApplied = addressApplied->boolValue();
    journal.routeApplied = routeApplied->boolValue();

    const auto parsedAddress = Ipv4Address::parse(address->stringValue());
    if (!parsedAddress.has_value()) {
        return Error(ErrorCode::journal_corrupt, "the recorded address is not a valid IPv4 address");
    }
    journal.address = *parsedAddress;

    if (prefixLength->integerValue() < 0 || prefixLength->integerValue() > 32) {
        return Error(ErrorCode::journal_corrupt, "the recorded prefix length is out of range");
    }
    journal.prefixLength = static_cast<int>(prefixLength->integerValue());

    const auto parsedDestination = Ipv4Cidr::parse(routeDestination->stringValue());
    if (!parsedDestination.has_value()) {
        return Error(ErrorCode::journal_corrupt, "the recorded route destination is not a valid prefix");
    }
    journal.routeDestination = *parsedDestination;

    if (routeMetric->integerValue() < 0 || routeMetric->integerValue() > 0xFFFFFFFF) {
        return Error(ErrorCode::journal_corrupt, "the recorded route metric is out of range");
    }
    journal.routeMetric = static_cast<std::uint32_t>(routeMetric->integerValue());
    return journal;
}

TunnelSession::TunnelSession(TunnelDriver& driver, NetConfig& netConfig)
    : driver_(driver), netConfig_(netConfig) {}

TunnelSession::~TunnelSession() {
    if (active()) {
        auto stopped = stop();
        (void)stopped;
    }
}

Status TunnelSession::recoverPreviousRun(const TunnelSessionSettings& settings) {
    std::error_code code;
    const bool exists = std::filesystem::exists(settings.journalPath, code);
    if (code) return Error(ErrorCode::journal_missing);
    if (!exists) return Status::success();

    std::string content;
    PHPVPN_TRY_ASSIGN(content, readWholeFile(settings.journalPath));

    auto parsed = OwnershipJournal::parse(content);
    if (!parsed.ok()) {
        // A journal that cannot be read is never guessed at, because removing
        // an address the service does not own would break the host network.
        PHPVPN_LOG_ERROR(kComponent, "the ownership journal is unusable and needs manual cleanup");
        return parsed.error();
    }

    auto lowerAscii = [](std::string text) {
        for (auto& character : text) if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
        return text;
    };
    // The journal stores UTF-8. Settings that do not convert can never match.
    const auto adapterName = toUtf8(settings.adapterName);
    const auto requestedGuid = toUtf8(settings.requestedGuid);
    if (!parsed->interfaceLuid || !adapterName.ok() || !requestedGuid.ok() ||
        parsed->adapterName != adapterName.value() ||
        lowerAscii(parsed->adapterGuid) != lowerAscii(requestedGuid.value()))
        return Error(ErrorCode::journal_ambiguous, "the ownership journal does not match this installation");

    // Startup must not open/create a Wintun adapter before authentication.
    // Normally process death already removes its driver-owned adapter. Only
    // retire the journal after positively proving that exact LUID and all its
    // rows are absent. A surviving/reused identity requires administrator
    // reconciliation, never an unverified delete or loss of ownership proof.
    bool present = false;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(std::clamp<std::int64_t>(settings.recoveryWaitMs, 0, 5000));
    do {
        PHPVPN_TRY_ASSIGN(present, netConfig_.interfaceExists(parsed->interfaceLuid));
        if (!present) break;
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } while (true);
    if (present) {
        // The platform may remove only a non-present Wintun PnP record after
        // matching its actual GUID, alias and LUID. A live or ambiguous device
        // remains untouched and the journal is retained for an administrator.
        PHPVPN_TRY(netConfig_.removeOwnedPhantomInterface(settings.adapterName, settings.requestedGuid, parsed->interfaceLuid));
        const auto removedDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
        do {
            PHPVPN_TRY_ASSIGN(present, netConfig_.interfaceExists(parsed->interfaceLuid));
            if (!present) break;
            if (std::chrono::steady_clock::now() >= removedDeadline)
                return Error(ErrorCode::journal_ambiguous, "the removed phantom interface still has an OS identity");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } while (true);
    }
    NetworkSnapshot snapshot;
    PHPVPN_TRY_ASSIGN(snapshot, netConfig_.snapshot());
    if (std::any_of(snapshot.addresses.begin(), snapshot.addresses.end(), [&](const auto& row) { return row.interfaceLuid == parsed->interfaceLuid; }) ||
        std::any_of(snapshot.routes.begin(), snapshot.routes.end(), [&](const auto& row) { return row.interfaceLuid == parsed->interfaceLuid; }))
        return Error(ErrorCode::journal_ambiguous, "recorded interface rows remain after identity disappearance");
    PHPVPN_LOG_WARN(kComponent, "retiring the journal of a driver-owned interface already removed by the OS");

    std::filesystem::remove(settings.journalPath, code);
    if (code) {
        return Error(ErrorCode::config_write_failed, "the ownership journal could not be removed");
    }
    return Status::success();
}

Status TunnelSession::preflight(const ClientConfig& config, std::uint64_t ownLuid) {
    NetworkSnapshot snapshot;
    PHPVPN_TRY_ASSIGN(snapshot, netConfig_.snapshot());

    for (const UnicastAddressEntry& entry : snapshot.addresses) {
        if (entry.interfaceLuid == ownLuid) {
            continue;
        }
        if (config.vpnCidr.contains(entry.address)) {
            return Error(ErrorCode::netconfig_preflight_conflict,
                         "an address of the VPN prefix already exists on another interface");
        }
    }

    for (const RouteEntry& entry : snapshot.routes) {
        if (entry.interfaceLuid == ownLuid) {
            continue;
        }
        if (entry.destination.prefixLength() == config.vpnCidr.prefixLength() &&
            entry.destination.network() == config.vpnCidr.network()) {
            return Error(ErrorCode::netconfig_preflight_conflict,
                         "a route into the VPN prefix already exists on another interface");
        }
    }

    return Status::success();
}

Status TunnelSession::applyJournal(const TunnelSessionSettings& settings) {
    return writeWholeFile(settings.journalPath, journal_.serialize());
}

Status TunnelSession::start(const ClientConfig& config, const TunnelSessionSettings& settings) {
    if (active()) {
        return Error(ErrorCode::state_conflict, "the tunnel session is already running");
    }

    journalPath_ = settings.journalPath;
    journal_ = OwnershipJournal{};

    // Converted before anything is created, so an unrepresentable name cannot
    // fail after the adapter already exists.
    std::string adapterName;
    PHPVPN_TRY_ASSIGN(adapterName, toUtf8(settings.adapterName));

    PHPVPN_TRY(driver_.loadAndVerify(settings.installDirectory));

    // The baseline is taken before anything is applied, so the verification
    // afterwards compares against the untouched host configuration.
    PHPVPN_TRY_ASSIGN(baseline_, netConfig_.snapshot());
    hasBaseline_ = true;

    PHPVPN_TRY(preflight(config, 0));

    TunnelAdapterInfo adapter;
    PHPVPN_TRY_ASSIGN(adapter, driver_.createAdapter(settings.adapterName, settings.tunnelType,
                                                     settings.requestedGuid));

    std::uint64_t luid = adapter.interfaceLuid;
    if (luid == 0) {
        PHPVPN_TRY_ASSIGN(luid, netConfig_.interfaceLuidForAlias(settings.adapterName));
    }

    journal_.adapterName = std::move(adapterName);
    journal_.adapterGuid = adapter.adapterGuid;
    journal_.interfaceLuid = luid;
    journal_.adapterCreated = adapter.created;
    journal_.address = config.virtualIp;
    journal_.prefixLength = config.vpnCidr.prefixLength();
    journal_.routeDestination = config.vpnCidr;
    journal_.routeMetric = settings.routeMetric;

    // The journal is written before the first change, so an abrupt termination
    // still leaves a record of what has to be removed.
    PHPVPN_TRY(applyJournal(settings));

    UnicastAddressEntry addressEntry;
    addressEntry.interfaceLuid = luid;
    addressEntry.address = config.virtualIp;
    addressEntry.prefixLength = config.vpnCidr.prefixLength();

    auto addressApplied = netConfig_.addUnicastAddress(addressEntry);
    if (!addressApplied.ok()) {
        auto cleanup = removeApplied(journal_);
        (void)cleanup;
        driver_.closeAdapter();
        return addressApplied.error();
    }
    journal_.addressApplied = true;
    PHPVPN_TRY(applyJournal(settings));

    RouteEntry routeEntry;
    routeEntry.interfaceLuid = luid;
    routeEntry.destination = config.vpnCidr;
    routeEntry.nextHop = Ipv4Address(0);
    routeEntry.metric = settings.routeMetric;

    auto routeApplied = netConfig_.addRoute(routeEntry);
    if (!routeApplied.ok()) {
        auto cleanup = removeApplied(journal_);
        (void)cleanup;
        driver_.closeAdapter();
        return routeApplied.error();
    }
    journal_.routeApplied = true;
    PHPVPN_TRY(applyJournal(settings));

    // Nothing outside the tunnel may have changed.
    NetworkSnapshot after;
    auto afterSnapshot = netConfig_.snapshot();
    if (!afterSnapshot.ok()) {
        auto cleanup = removeApplied(journal_);
        (void)cleanup;
        driver_.closeAdapter();
        return afterSnapshot.error();
    }
    after = std::move(afterSnapshot).value();
    if (!baseline_.sameDefaultRouteAndDns(after)) {
        auto cleanup = removeApplied(journal_);
        (void)cleanup;
        driver_.closeAdapter();
        return Error(ErrorCode::netconfig_verify_failed,
                     "the default route or the DNS configuration changed unexpectedly");
    }

    auto session = driver_.startSession(settings.ringCapacity);
    if (!session.ok()) {
        auto cleanup = removeApplied(journal_);
        (void)cleanup;
        driver_.closeAdapter();
        return session.error();
    }
    tunnel_ = std::move(session).value();

    PHPVPN_LOG_INFO(kComponent, "the virtual interface is ready");
    return Status::success();
}

Status TunnelSession::removeApplied(const OwnershipJournal& journal) {
    Status result = Status::success();

    if (journal.routeApplied) {
        RouteEntry routeEntry;
        routeEntry.interfaceLuid = journal.interfaceLuid;
        routeEntry.destination = journal.routeDestination;
        routeEntry.nextHop = Ipv4Address(0);
        routeEntry.metric = journal.routeMetric;
        auto removed = netConfig_.removeRoute(routeEntry);
        if (!removed.ok() && result.ok()) {
            result = removed.error();
        }
    }

    if (journal.addressApplied) {
        UnicastAddressEntry addressEntry;
        addressEntry.interfaceLuid = journal.interfaceLuid;
        addressEntry.address = journal.address;
        addressEntry.prefixLength = journal.prefixLength;
        auto removed = netConfig_.removeUnicastAddress(addressEntry);
        if (!removed.ok() && result.ok()) {
            result = removed.error();
        }
    }

    return result;
}

Status TunnelSession::stop() {
    if (tunnel_) {
        tunnel_->signalStop();
    }
    driver_.endSession();
    tunnel_.reset();

    Status removal = removeApplied(journal_);

    driver_.closeAdapter();

    if (removal.ok() && !journalPath_.empty()) {
        std::error_code code;
        std::filesystem::remove(journalPath_, code);
        if (code) {
            removal = Error(ErrorCode::config_write_failed,
                            "the ownership journal could not be removed");
        }
    }

    journal_ = OwnershipJournal{};
    hasBaseline_ = false;

    if (removal.ok()) {
        PHPVPN_LOG_INFO(kComponent, "the virtual interface was released");
    }
    return removal;
}

}  // namespace phpvpn
