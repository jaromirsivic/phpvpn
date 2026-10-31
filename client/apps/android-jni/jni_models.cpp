#include "jni_models.h"

#include <array>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../tray/contracts_save_machine.h"
#include "../tray/save_response.h"
#include "core/base/secure_buffer.h"
#include "core/config/config.h"
#include "core/config/config_document.h"
#include "core/crypto/crypto_provider.h"
#include "core/ipc/ipc_protocol.h"
#include "core/ipc/wizard_ipc.h"
#include "core/presentation/contracts_model.h"
#include "core/presentation/password_key_model.h"
#include "core/presentation/server_info_model.h"
#include "core/presentation/table_pagination.h"
#include "core/protocol/json.h"
#include "jni_arrays.h"

namespace phpvpn {
namespace {

// Largest configuration document the models accept (ConfigStore limit is
// 512 KiB of UTF-8; a UTF-16 char[] of a valid document is never longer).
constexpr std::size_t kMaxDocumentUnits = kIpcMaxMessageBytes;
// Paths and identifiers are far below this.
constexpr std::size_t kMaxTextBytes = 64 * 1024;
// A key may use the whole 512 KiB key budget of the model (and one byte more,
// which the model refuses with config_too_large itself).
constexpr std::size_t kMaxKeyUnits = kIpcMaxMessageBytes;

// ----------------------------------------------------------------- shared

// One crypto provider for every model of the process (the OpenSSL provider is
// stateless). Null when the platform provider is unavailable.
CryptoProvider* crypto() {
    static std::mutex mutex;
    static std::unique_ptr<CryptoProvider> provider;
    std::lock_guard lock(mutex);
    if (!provider) {
        auto created = createPlatformCryptoProvider();
        if (created) provider = std::move(created).value();
    }
    return provider.get();
}

jclass byteArrayClass = nullptr;  // global reference to byte[], set in registerModelNatives

// Null for success, the stable error name otherwise (ErrorCode.fromWire).
jbyteArray errorName(JNIEnv* env, const Status& status) {
    if (status.ok()) return nullptr;
    return jni::newBytes(env, errorCodeName(status.code()));
}
jbyteArray errorName(JNIEnv* env, ErrorCode code) { return jni::newBytes(env, errorCodeName(code)); }

ErrorCode errorCodeFromName(std::string_view name) {
    // errorCodeName maps every unknown value to "internal", so the first
    // match of a bounded scan is the code (none is 0).
    for (int value = 0; value < 1024; ++value) {
        const auto code = static_cast<ErrorCode>(value);
        if (name == errorCodeName(code)) return code;
    }
    return ErrorCode::internal;
}

std::string bytesOr(JNIEnv* env, jbyteArray array, std::string fallback = {}) {
    auto text = jni::readBytes(env, array, kMaxTextBytes);
    return text ? std::move(*text) : std::move(fallback);
}

// UTF-8 of a secret char[]; nullopt for null, oversized or a lone surrogate.
std::optional<std::string> secretUtf8(JNIEnv* env, jcharArray chars, std::size_t maxUnits) {
    auto units = jni::readChars(env, chars, maxUnits);
    if (!units) return std::nullopt;
    jni::WipeUnitsOnExit wipe{*units};
    return jni::utf16ToUtf8(*units);
}

void wipeJson(JsonValue& value) {
    if (value.isString()) {
        auto& text = const_cast<std::string&>(value.stringValue());
        secureWipe(text.data(), text.size());
    } else if (value.isObject()) {
        for (auto& entry : value.objectValue()) wipeJson(entry.second);
    } else if (value.isArray()) {
        for (auto& item : const_cast<JsonArray&>(value.arrayValue())) wipeJson(item);
    }
}

jobjectArray byteArrays(JNIEnv* env, std::initializer_list<std::string_view> values) {
    jobjectArray array = env->NewObjectArray(static_cast<jsize>(values.size()), byteArrayClass, nullptr);
    if (array == nullptr) return nullptr;
    jsize index = 0;
    for (const auto value : values) {
        jbyteArray element = jni::newBytes(env, value);
        if (element == nullptr) return nullptr;
        env->SetObjectArrayElement(array, index++, element);
        env->DeleteLocalRef(element);
    }
    return array;
}

void setOutError(JNIEnv* env, jobjectArray out, ErrorCode code) {
    if (out == nullptr || env->GetArrayLength(out) < 1) return;
    jbyteArray name = errorName(env, code);
    if (name == nullptr) return;
    env->SetObjectArrayElement(out, 0, name);
    env->DeleteLocalRef(name);
}

// ----------------------------------------------------------------- contracts model

// A ContractsModel seen through JNI: standalone (owned) or the model of a
// save machine, which may be replaced by load and close. Targets are kept here
// because ContractTarget carries a weak identity and a generation that Kotlin
// cannot hold; a target of a replaced model simply stops being accepted.
struct ContractsModelSource {
    std::unique_ptr<ContractsModel> owned;
    tray::ContractsSaveMachine* machine = nullptr;
    std::unordered_map<jlong, ContractTarget> targets;
    jlong nextTarget = 1;

    ContractsModel* model() const { return owned ? owned.get() : machine ? machine->model() : nullptr; }
};

struct MachineHandle {
    tray::ContractsSaveMachine machine;
    ContractsModelSource source;
    MachineHandle() { source.machine = &machine; }
};

ContractsModelSource* source(jlong handle) { return reinterpret_cast<ContractsModelSource*>(handle); }
MachineHandle* machine(jlong handle) { return reinterpret_cast<MachineHandle*>(handle); }

jint columnValue(ContractColumn column) { return static_cast<jint>(column); }
std::optional<ContractColumn> columnOf(jint value) {
    if (value < 0 || value > static_cast<jint>(ContractColumn::password)) return std::nullopt;
    return static_cast<ContractColumn>(value);
}
Ipv4Address address(jint value) { return Ipv4Address(static_cast<std::uint32_t>(value)); }
jint addressValue(Ipv4Address value) { return static_cast<jint>(value.value()); }

jlong contractsCreate(JNIEnv* env, jclass, jcharArray document, jbyteArray path, jbyteArray revision, jobjectArray error) {
    auto* provider = crypto();
    if (provider == nullptr) {
        setOutError(env, error, ErrorCode::internal);
        return 0;
    }
    auto text = secretUtf8(env, document, kMaxDocumentUnits);
    if (!text) {
        setOutError(env, error, ErrorCode::config_encoding_error);
        return 0;
    }
    jni::WipeOnExit wipe{*text};
    auto created = ContractsModel::create(*provider, *text, bytesOr(env, path), bytesOr(env, revision));
    if (!created) {
        setOutError(env, error, created.code());
        return 0;
    }
    auto* holder = new ContractsModelSource;
    holder->owned = std::move(created).value();
    return reinterpret_cast<jlong>(holder);
}

void contractsDestroy(JNIEnv*, jclass, jlong handle) {
    auto* holder = source(handle);
    if (holder == nullptr || holder->machine != nullptr) return;  // a machine's source dies with the machine
    if (holder->owned) holder->owned->close();
    delete holder;
}

jintArray contractsRowAddresses(JNIEnv* env, jclass, jlong handle) {
    const auto* model = source(handle)->model();
    if (model == nullptr) return env->NewIntArray(0);
    const auto rows = model->rows();
    jintArray array = env->NewIntArray(static_cast<jsize>(rows.size()));
    if (array == nullptr) return nullptr;
    std::vector<jint> values;
    values.reserve(rows.size());
    for (const auto& row : rows) values.push_back(addressValue(row.remoteIp()));
    env->SetIntArrayRegion(array, 0, static_cast<jsize>(values.size()), values.data());
    return array;
}

// Bit 0 enabled, 1 keyValid, 2 dirty, 3 keyErrorVisible (NativeContractsModel).
jintArray contractsRowFlags(JNIEnv* env, jclass, jlong handle) {
    const auto* model = source(handle)->model();
    if (model == nullptr) return env->NewIntArray(0);
    const auto rows = model->rows();
    jintArray array = env->NewIntArray(static_cast<jsize>(rows.size()));
    if (array == nullptr) return nullptr;
    std::vector<jint> values;
    values.reserve(rows.size());
    for (const auto& row : rows)
        values.push_back((row.enabled() ? 1 : 0) | (row.keyValid() ? 2 : 0) | (row.dirty() ? 4 : 0) |
                         (row.keyErrorVisible() ? 8 : 0));
    env->SetIntArrayRegion(array, 0, static_cast<jsize>(values.size()), values.data());
    return array;
}

jcharArray contractsRowKey(JNIEnv* env, jclass, jlong handle, jint remoteIp) {
    const auto* model = source(handle)->model();
    const auto* row = model ? model->row(address(remoteIp)) : nullptr;
    if (row == nullptr) return nullptr;
    return jni::newCharsFromUtf8(env, row->keyText());
}

// [dirty, blockingErrors, keyTextBytes, canPrepareSave, frozen, closed,
//  saveGeneration, firstBlockingFocus present, its IPv4, its column,
//  focus present, its IPv4, its column, localIp]; null without a model.
jlongArray contractsState(JNIEnv* env, jclass, jlong handle) {
    const auto* model = source(handle)->model();
    if (model == nullptr) return nullptr;
    const auto first = model->firstBlockingFocus();
    const auto& focus = model->focus();
    const std::array<jlong, 14> values{
        model->dirty() ? 1 : 0,
        static_cast<jlong>(model->blockingErrors()),
        static_cast<jlong>(model->keyTextBytes()),
        model->canPrepareSave() ? 1 : 0,
        model->frozen() ? 1 : 0,
        model->closed() ? 1 : 0,
        static_cast<jlong>(model->saveGeneration()),
        first ? 1 : 0,
        first ? static_cast<jlong>(first->remoteIp.value()) : 0,
        first ? columnValue(first->column) : -1,
        focus ? 1 : 0,
        focus ? static_cast<jlong>(focus->remoteIp.value()) : 0,
        focus ? columnValue(focus->column) : -1,
        static_cast<jlong>(model->localIp().value()),
    };
    jlongArray array = env->NewLongArray(static_cast<jsize>(values.size()));
    if (array == nullptr) return nullptr;
    env->SetLongArrayRegion(array, 0, static_cast<jsize>(values.size()), values.data());
    return array;
}

// 0 configPath, 1 revisionToken (informational only).
jbyteArray contractsText(JNIEnv* env, jclass, jlong handle, jint which) {
    const auto* model = source(handle)->model();
    if (model == nullptr) return nullptr;
    return jni::newBytes(env, which == 0 ? model->configPath() : model->revisionToken());
}

ContractsModel* editable(jlong handle) { return source(handle)->model(); }

jbyteArray contractsSetEnabled(JNIEnv* env, jclass, jlong handle, jint remoteIp, jboolean enabled) {
    auto* model = editable(handle);
    if (model == nullptr) return errorName(env, ErrorCode::state_conflict);
    return errorName(env, model->setEnabled(address(remoteIp), enabled == JNI_TRUE));
}

jbyteArray contractsSetKey(JNIEnv* env, jclass, jlong handle, jint remoteIp, jcharArray key) {
    auto* model = editable(handle);
    if (model == nullptr) return errorName(env, ErrorCode::state_conflict);
    auto text = secretUtf8(env, key, kMaxKeyUnits);
    if (!text) return errorName(env, ErrorCode::config_invalid_key);
    jni::WipeOnExit wipe{*text};
    return errorName(env, model->setKey(address(remoteIp), *text));
}

jbyteArray contractsSetFocus(JNIEnv* env, jclass, jlong handle, jint remoteIp, jint column) {
    auto* model = editable(handle);
    if (model == nullptr) return errorName(env, ErrorCode::state_conflict);
    if (column < 0) return errorName(env, model->setFocus(std::nullopt));
    const auto parsed = columnOf(column);
    if (!parsed) return errorName(env, ErrorCode::invalid_argument);
    return errorName(env, model->setFocus(ContractFocus{address(remoteIp), *parsed}));
}

jlong contractsTarget(JNIEnv*, jclass, jlong handle, jint remoteIp) {
    auto* holder = source(handle);
    auto* model = holder->model();
    if (model == nullptr) return 0;
    auto target = model->target(address(remoteIp));
    if (!target) return 0;
    const jlong id = holder->nextTarget++;
    holder->targets.emplace(id, std::move(target).value());
    return id;
}

const ContractTarget* findTarget(jlong handle, jlong id) {
    const auto& targets = source(handle)->targets;
    const auto found = targets.find(id);
    return found == targets.end() ? nullptr : &found->second;
}

jbyteArray contractsTargetContractId(JNIEnv* env, jclass, jlong handle, jlong target) {
    const auto* value = findTarget(handle, target);
    return value ? jni::newBytes(env, value->contractId()) : nullptr;
}

void contractsTargetRelease(JNIEnv*, jclass, jlong handle, jlong target) { source(handle)->targets.erase(target); }

jbyteArray contractsConfirmRandom(JNIEnv* env, jclass, jlong handle, jlong target, jboolean confirmed) {
    auto* model = editable(handle);
    const auto* value = findTarget(handle, target);
    auto* provider = crypto();
    if (model == nullptr) return errorName(env, ErrorCode::state_conflict);
    // An unknown or released target is refused like a stale one (accepts()).
    if (value == nullptr) return errorName(env, ErrorCode::invalid_argument);
    if (provider == nullptr) return errorName(env, ErrorCode::internal);
    return errorName(env, model->confirmRandom(*provider, *value, confirmed == JNI_TRUE));
}

jbyteArray contractsApplyPasswordKey(JNIEnv* env, jclass, jlong handle, jlong target, jcharArray key) {
    auto* model = editable(handle);
    const auto* value = findTarget(handle, target);
    if (model == nullptr) return errorName(env, ErrorCode::state_conflict);
    if (value == nullptr) return errorName(env, ErrorCode::invalid_argument);
    auto text = secretUtf8(env, key, kMaxKeyUnits);
    if (!text) return errorName(env, ErrorCode::config_invalid_key);
    jni::WipeOnExit wipe{*text};
    return errorName(env, model->applyPasswordKey(*value, *text));
}

jboolean contractsResumeEditing(JNIEnv*, jclass, jlong handle, jlong generation) {
    auto* model = editable(handle);
    return model != nullptr && model->resumeEditing(static_cast<std::uint64_t>(generation)) ? JNI_TRUE : JNI_FALSE;
}

// ----------------------------------------------------------------- save machine

tray::SaveOperation operationFrom(JNIEnv* env, jlongArray values) {
    tray::SaveOperation operation;
    if (values == nullptr || env->GetArrayLength(values) != 4) return operation;
    std::array<jlong, 4> raw{};
    env->GetLongArrayRegion(values, 0, 4, raw.data());
    operation.windowGeneration = static_cast<std::uint64_t>(raw[0]);
    operation.draftGeneration = static_cast<std::uint64_t>(raw[1]);
    operation.operationGeneration = static_cast<std::uint64_t>(raw[2]);
    operation.deadlineMs = static_cast<std::uint64_t>(raw[3]);
    return operation;
}

void operationInto(JNIEnv* env, const tray::SaveOperation& operation, jlongArray out) {
    if (out == nullptr || env->GetArrayLength(out) != 4) return;
    const std::array<jlong, 4> raw{static_cast<jlong>(operation.windowGeneration),
                                   static_cast<jlong>(operation.draftGeneration),
                                   static_cast<jlong>(operation.operationGeneration),
                                   static_cast<jlong>(operation.deadlineMs)};
    env->SetLongArrayRegion(out, 0, 4, raw.data());
}

jlong machineCreate(JNIEnv*, jclass) { return reinterpret_cast<jlong>(new MachineHandle); }

void machineDestroy(JNIEnv*, jclass, jlong handle) {
    auto* value = machine(handle);
    if (value == nullptr) return;
    value->machine.close();
    delete value;
}

jbyteArray machineLoad(JNIEnv* env, jclass, jlong handle, jcharArray document, jbyteArray path, jbyteArray revision) {
    auto* provider = crypto();
    if (provider == nullptr) return errorName(env, ErrorCode::internal);
    auto text = secretUtf8(env, document, kMaxDocumentUnits);
    if (!text) return errorName(env, ErrorCode::config_encoding_error);
    jni::WipeOnExit wipe{*text};
    auto* value = machine(handle);
    const auto loaded = value->machine.load(*provider, *text, bytesOr(env, path), bytesOr(env, revision));
    if (loaded.ok()) value->source.targets.clear();
    return errorName(env, loaded);
}

jint machineStage(JNIEnv*, jclass, jlong handle) { return static_cast<jint>(machine(handle)->machine.stage()); }
jboolean machineSaving(JNIEnv*, jclass, jlong handle) { return machine(handle)->machine.saving() ? JNI_TRUE : JNI_FALSE; }
jboolean machineHasModel(JNIEnv*, jclass, jlong handle) {
    return machine(handle)->machine.model() != nullptr ? JNI_TRUE : JNI_FALSE;
}
jlong machineModel(JNIEnv*, jclass, jlong handle) { return reinterpret_cast<jlong>(&machine(handle)->source); }

// [open, dirty, saving, unknown, checking, windowGeneration, draftGeneration,
//  operationGeneration, checkGeneration, deadlineMs].
jlongArray machineSnapshot(JNIEnv* env, jclass, jlong handle, jboolean checking) {
    const auto snapshot = machine(handle)->machine.snapshot(checking == JNI_TRUE);
    const std::array<jlong, 10> values{snapshot.open, snapshot.dirty, snapshot.saving, snapshot.unknown, snapshot.checking,
                                       static_cast<jlong>(snapshot.windowGeneration),
                                       static_cast<jlong>(snapshot.draftGeneration),
                                       static_cast<jlong>(snapshot.operationGeneration),
                                       static_cast<jlong>(snapshot.checkGeneration),
                                       static_cast<jlong>(snapshot.deadlineMs)};
    jlongArray array = env->NewLongArray(static_cast<jsize>(values.size()));
    if (array == nullptr) return nullptr;
    env->SetLongArrayRegion(array, 0, static_cast<jsize>(values.size()), values.data());
    return array;
}

void machineChanged(JNIEnv*, jclass, jlong handle) { machine(handle)->machine.changed(); }

jbyteArray machinePrepare(JNIEnv* env, jclass, jlong handle, jboolean confirmedRepeat, jboolean checking) {
    return errorName(env, machine(handle)->machine.prepare(confirmedRepeat == JNI_TRUE, checking == JNI_TRUE));
}

// The C++ SaveSender becomes a return value: the payload as char[] for the
// set-config request (wiped by Kotlin after sending) and the operation in
// `operation`. Null when the machine sends nothing.
jcharArray machineDispatch(JNIEnv* env, jclass, jlong handle, jlong nowMs, jlongArray operation) {
    jcharArray payload = nullptr;
    tray::SaveOperation sent;
    const bool started = machine(handle)->machine.dispatch(
        static_cast<std::uint64_t>(nowMs), [&](const tray::SaveOperation& value, const std::string& text) {
            payload = jni::newCharsFromUtf8(env, text);
            if (payload == nullptr && env->ExceptionCheck()) env->ExceptionClear();
            sent = value;
            return payload != nullptr;
        });
    if (!started) return nullptr;
    operationInto(env, sent, operation);
    return payload;
}

jint machineSaveFinished(JNIEnv* env, jclass, jlong handle, jlongArray operation, jint outcome, jbyteArray error,
                         jbyteArray validationDescription) {
    tray::SaveResult result;
    result.operation = operationFrom(env, operation);
    result.outcome = outcome >= 0 && outcome <= static_cast<jint>(tray::SaveOutcome::unknown)
        ? static_cast<tray::SaveOutcome>(outcome)
        : tray::SaveOutcome::unknown;
    result.error = errorCodeFromName(bytesOr(env, error, "response_invalid"));
    result.validationDescription = bytesOr(env, validationDescription);
    return static_cast<jint>(machine(handle)->machine.saveFinished(result));
}

jlong machineCheck(JNIEnv*, jclass, jlong handle, jboolean checking) {
    std::uint64_t generation = 0;
    const bool sent = machine(handle)->machine.check(checking == JNI_TRUE, [&](std::uint64_t value) {
        generation = value;
        return true;
    });
    return sent ? static_cast<jlong>(generation) : -1;
}

// The raw get-config-document reply parsed here with WizardIpc::parseReply
// (null response = no reply, ipc_unavailable, as the tray WizardReader).
jint machineCheckFinished(JNIEnv* env, jclass, jlong handle, jlong generation, jbyteArray requestId,
                          jbyteArray response) {
    const std::string id = bytesOr(env, requestId);
    Result<WizardReply> reply = Error(ErrorCode::ipc_unavailable);
    std::optional<std::string> text;
    if (response != nullptr) {
        text = jni::readBytes(env, response, kIpcMaxMessageBytes);
        if (text) reply = WizardIpc::parseReply(WizardCommand::get_config_document, id, *text);
        else reply = Error(ErrorCode::ipc_message_too_large);
    }
    const auto event = machine(handle)->machine.checkFinished(static_cast<std::uint64_t>(generation), reply);
    if (reply) wipeJson(reply->result);
    if (text) secureWipe(text->data(), text->size());
    return static_cast<jint>(event);
}

jboolean machineCanRepeat(JNIEnv*, jclass, jlong handle, jboolean checking) {
    return machine(handle)->machine.canRepeat(checking == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

jbyteArray machineNotice(JNIEnv* env, jclass, jlong handle) { return jni::newBytes(env, machine(handle)->machine.notice()); }

void machineClose(JNIEnv*, jclass, jlong handle) {
    auto* value = machine(handle);
    value->machine.close();
    value->source.targets.clear();
}

// [outcome as one ASCII digit, error name, validation description].
jobjectArray classifySave(JNIEnv* env, jclass, jlongArray operation, jbyteArray requestId, jbyteArray response,
                          jboolean writeAttempted, jlong completedAtMs) {
    const auto parsedOperation = operationFrom(env, operation);
    const std::string id = bytesOr(env, requestId);
    std::string text;
    if (response != nullptr) {
        // An oversized reply stays unknown, like the desktop size check.
        auto read = jni::readBytes(env, response, kIpcMaxMessageBytes);
        text = read ? std::move(*read) : std::string(kIpcMaxMessageBytes + 1, ' ');
    }
    jni::WipeOnExit wipe{text};
    const auto result = tray::classifySaveResponse(parsedOperation, id, text, writeAttempted == JNI_TRUE,
                                                   static_cast<std::uint64_t>(completedAtMs));
    const char outcome[2] = {static_cast<char>('0' + static_cast<int>(result.outcome)), '\0'};
    return byteArrays(env, {std::string_view(outcome, 1), errorCodeName(result.error), result.validationDescription});
}

// ----------------------------------------------------------------- password key

PasswordKeyModel* password(jlong handle) { return reinterpret_cast<PasswordKeyModel*>(handle); }

jlong passwordCreate(JNIEnv*, jclass) { return reinterpret_cast<jlong>(new PasswordKeyModel); }

void passwordDestroy(JNIEnv*, jclass, jlong handle) {
    auto* model = password(handle);
    if (model == nullptr) return;
    model->clear();
    delete model;
}

// The UTF-16 units go to the model unconverted; the model itself encodes and
// hashes them. Longer input than any valid value is refused without a copy.
jint passwordUpdate(JNIEnv* env, jclass, jlong handle, jcharArray chars) {
    auto* provider = crypto();
    auto* model = password(handle);
    if (provider == nullptr) return static_cast<jint>(PasswordKeyError::cryptoFailed);
    constexpr std::size_t kLimit = PasswordKeyModel::kMaxScalars * 2;
    const jsize length = chars == nullptr ? 0 : env->GetArrayLength(chars);
    if (length >= 0 && static_cast<std::size_t>(length) > kLimit) {
        // The model rejects on the length alone, before it reads a unit.
        static const std::u16string tooLong(kLimit + 1, u'a');
        return static_cast<jint>(model->update(*provider, tooLong));
    }
    auto units = chars == nullptr ? std::optional<std::u16string>(std::u16string()) : jni::readChars(env, chars, kLimit);
    if (!units) return static_cast<jint>(PasswordKeyError::invalidUtf16);
    jni::WipeUnitsOnExit wipe{*units};
    return static_cast<jint>(model->update(*provider, *units));
}

// [valid, scalarCount, error].
jintArray passwordState(JNIEnv* env, jclass, jlong handle) {
    const auto* model = password(handle);
    const std::array<jint, 3> values{model->valid() ? 1 : 0, static_cast<jint>(model->scalarCount()),
                                     static_cast<jint>(model->error())};
    jintArray array = env->NewIntArray(3);
    if (array == nullptr) return nullptr;
    env->SetIntArrayRegion(array, 0, 3, values.data());
    return array;
}

// The lowercase hex key the UI shows and applies; the caller wipes it.
jcharArray passwordKey(JNIEnv* env, jclass, jlong handle) {
    const auto* model = password(handle);
    if (!model->valid()) return nullptr;
    return jni::newCharsFromUtf8(env, model->key());
}

void passwordClear(JNIEnv*, jclass, jlong handle) { password(handle)->clear(); }

// The "Random" action of the password dialog (apps/tray/password_sha_window.cpp
// randomPassword): 64 characters of "ghjkmnpqrstuvwxyz" from the crypto
// provider without modulo bias. Null when no random bytes are available.
jcharArray passwordRandom(JNIEnv* env, jclass) {
    auto* provider = crypto();
    if (provider == nullptr) return nullptr;
    constexpr std::u16string_view alphabet = u"ghjkmnpqrstuvwxyz";
    constexpr auto limit = 256 - (256 % alphabet.size());
    SecureBuffer bytes(64);
    std::u16string value(64, u'\0');
    jni::WipeUnitsOnExit wipe{value};
    std::size_t used = 0;
    while (used < value.size()) {
        if (!provider->randomBytes(bytes.mutableSpan())) return nullptr;
        for (const auto byte : bytes.span()) {
            // 255 is refused, so each of the 17 characters has 15 byte values.
            if (byte >= limit) continue;
            value[used++] = alphabet[byte % alphabet.size()];
            if (used == value.size()) break;
        }
    }
    return jni::newChars(env, value);
}

// ----------------------------------------------------------------- server info

ServerInfoModel* serverInfo(jlong handle) { return reinterpret_cast<ServerInfoModel*>(handle); }

// The classification of one get-server-info reply; 0 with the error name in
// error[0] (server_version_incompatible, ipc_malformed, ...).
jlong serverInfoParse(JNIEnv* env, jclass, jbyteArray response, jobjectArray error) {
    auto text = jni::readBytes(env, response, kIpcMaxMessageBytes);
    if (!text) {
        setOutError(env, error, response == nullptr ? ErrorCode::ipc_malformed : ErrorCode::ipc_message_too_large);
        return 0;
    }
    auto snapshot = parseServerInfoResult(*text);
    if (!snapshot) {
        setOutError(env, error, snapshot.code());
        return 0;
    }
    auto model = buildServerInfoModel(std::move(snapshot).value());
    if (!model) {
        setOutError(env, error, model.code());
        return 0;
    }
    return reinterpret_cast<jlong>(new ServerInfoModel(std::move(model).value()));
}

void serverInfoDestroy(JNIEnv*, jclass, jlong handle) { delete serverInfo(handle); }

// [rows, onlineClients, validOnlineClients, localAddress].
jlongArray serverInfoCounts(JNIEnv* env, jclass, jlong handle) {
    const auto* model = serverInfo(handle);
    const std::array<jlong, 4> values{static_cast<jlong>(model->rows.size()), static_cast<jlong>(model->onlineClients),
                                      static_cast<jlong>(model->validOnlineClients),
                                      static_cast<jlong>(model->snapshot.localAddress.value())};
    jlongArray array = env->NewLongArray(4);
    if (array == nullptr) return nullptr;
    env->SetLongArrayRegion(array, 0, 4, values.data());
    return array;
}

jintArray serverInfoRowAddresses(JNIEnv* env, jclass, jlong handle) {
    const auto& rows = serverInfo(handle)->rows;
    std::vector<jint> values;
    for (const auto& row : rows) values.push_back(addressValue(row.peer));
    jintArray array = env->NewIntArray(static_cast<jsize>(values.size()));
    if (array == nullptr) return nullptr;
    env->SetIntArrayRegion(array, 0, static_cast<jsize>(values.size()), values.data());
    return array;
}

// ServerClientState: 0 valid, 1 invalid, 2 offline.
jintArray serverInfoRowStates(JNIEnv* env, jclass, jlong handle) {
    const auto& rows = serverInfo(handle)->rows;
    std::vector<jint> values;
    for (const auto& row : rows) values.push_back(static_cast<jint>(row.state));
    jintArray array = env->NewIntArray(static_cast<jsize>(values.size()));
    if (array == nullptr) return nullptr;
    env->SetIntArrayRegion(array, 0, static_cast<jsize>(values.size()), values.data());
    return array;
}

jlongArray serverInfoRowLastSeen(JNIEnv* env, jclass, jlong handle) {
    const auto& rows = serverInfo(handle)->rows;
    std::vector<jlong> values;
    for (const auto& row : rows) values.push_back(static_cast<jlong>(row.lastSeenMs));
    jlongArray array = env->NewLongArray(static_cast<jsize>(values.size()));
    if (array == nullptr) return nullptr;
    env->SetLongArrayRegion(array, 0, static_cast<jsize>(values.size()), values.data());
    return array;
}

jbyteArray serverInfoRowContract(JNIEnv* env, jclass, jlong handle, jint index) {
    const auto& rows = serverInfo(handle)->rows;
    if (index < 0 || static_cast<std::size_t>(index) >= rows.size()) return nullptr;
    return jni::newBytes(env, rows[static_cast<std::size_t>(index)].contract);
}

// 0 the count text "(N online clients, M valid contracts)", 1 the verified
// plaintext exactly as received.
jbyteArray serverInfoText(JNIEnv* env, jclass, jlong handle, jint which) {
    const auto* model = serverInfo(handle);
    return which == 0 ? jni::newBytes(env, serverInfoCountText(*model)) : jni::newBytes(env, model->snapshot.infoPlaintext);
}

jbyteArray serverInfoStateText(JNIEnv* env, jclass, jint state) {
    if (state < 0 || state > static_cast<jint>(ServerClientState::offline)) return nullptr;
    return jni::newBytes(env, serverClientStateText(static_cast<ServerClientState>(state)));
}

jint serverInfoPageCount(JNIEnv*, jclass, jint rows) {
    return tablePageCount(rows < 0 ? 0 : static_cast<std::size_t>(rows));
}

jintArray serverInfoPageNumbers(JNIEnv* env, jclass, jint count, jint current) {
    const auto pages = tablePageNumbers(count, current);
    jintArray array = env->NewIntArray(static_cast<jsize>(pages.size()));
    if (array == nullptr) return nullptr;
    env->SetIntArrayRegion(array, 0, static_cast<jsize>(pages.size()), reinterpret_cast<const jint*>(pages.data()));
    return array;
}

// ----------------------------------------------------------------- configuration document check

// documentMatches of core/ipc/wizard_ipc.cpp: the declared availability must
// be the one the full validator derives from the text.
bool documentMatches(std::string_view document, std::string_view declared) {
    if (!normalizeConfigText(document)) return false;
    const bool jsonSyntax = Json::parse(document).ok();
    ConfigWarnings warnings;
    const bool valid = jsonSyntax && parseAndValidateConfig(document, warnings).ok();
    if (declared == "valid") return valid;
    const bool repairable = jsonSyntax && !valid && analyzeWizardDocument(document).ok();
    return declared == "repairable" ? repairable : declared == "invalid" && !valid && !repairable;
}

jboolean configDocumentMatches(JNIEnv* env, jclass, jbyteArray document, jbyteArray availability) {
    auto text = jni::readBytes(env, document, kIpcMaxMessageBytes);
    if (!text) return JNI_FALSE;
    jni::WipeOnExit wipe{*text};
    return documentMatches(*text, bytesOr(env, availability)) ? JNI_TRUE : JNI_FALSE;
}

jboolean configValidTemplate(JNIEnv* env, jclass, jbyteArray document) {
    auto text = jni::readBytes(env, document, kIpcMaxMessageBytes);
    if (!text) return JNI_FALSE;
    jni::WipeOnExit wipe{*text};
    ConfigWarnings warnings;
    return parseAndValidateConfig(*text, warnings).ok() ? JNI_TRUE : JNI_FALSE;
}

// ----------------------------------------------------------------- registration

#define PHPVPN_NATIVE(name, signature) \
    JNINativeMethod { const_cast<char*>(#name), const_cast<char*>(signature), reinterpret_cast<void*>(&name) }

const JNINativeMethod kModelMethods[] = {
    PHPVPN_NATIVE(contractsCreate, "([C[B[B[[B)J"),
    PHPVPN_NATIVE(contractsDestroy, "(J)V"),
    PHPVPN_NATIVE(contractsRowAddresses, "(J)[I"),
    PHPVPN_NATIVE(contractsRowFlags, "(J)[I"),
    PHPVPN_NATIVE(contractsRowKey, "(JI)[C"),
    PHPVPN_NATIVE(contractsState, "(J)[J"),
    PHPVPN_NATIVE(contractsText, "(JI)[B"),
    PHPVPN_NATIVE(contractsSetEnabled, "(JIZ)[B"),
    PHPVPN_NATIVE(contractsSetKey, "(JI[C)[B"),
    PHPVPN_NATIVE(contractsSetFocus, "(JII)[B"),
    PHPVPN_NATIVE(contractsTarget, "(JI)J"),
    PHPVPN_NATIVE(contractsTargetContractId, "(JJ)[B"),
    PHPVPN_NATIVE(contractsTargetRelease, "(JJ)V"),
    PHPVPN_NATIVE(contractsConfirmRandom, "(JJZ)[B"),
    PHPVPN_NATIVE(contractsApplyPasswordKey, "(JJ[C)[B"),
    PHPVPN_NATIVE(contractsResumeEditing, "(JJ)Z"),
    PHPVPN_NATIVE(machineCreate, "()J"),
    PHPVPN_NATIVE(machineDestroy, "(J)V"),
    PHPVPN_NATIVE(machineLoad, "(J[C[B[B)[B"),
    PHPVPN_NATIVE(machineStage, "(J)I"),
    PHPVPN_NATIVE(machineSaving, "(J)Z"),
    PHPVPN_NATIVE(machineHasModel, "(J)Z"),
    PHPVPN_NATIVE(machineModel, "(J)J"),
    PHPVPN_NATIVE(machineSnapshot, "(JZ)[J"),
    PHPVPN_NATIVE(machineChanged, "(J)V"),
    PHPVPN_NATIVE(machinePrepare, "(JZZ)[B"),
    PHPVPN_NATIVE(machineDispatch, "(JJ[J)[C"),
    PHPVPN_NATIVE(machineSaveFinished, "(J[JI[B[B)I"),
    PHPVPN_NATIVE(machineCheck, "(JZ)J"),
    PHPVPN_NATIVE(machineCheckFinished, "(JJ[B[B)I"),
    PHPVPN_NATIVE(machineCanRepeat, "(JZ)Z"),
    PHPVPN_NATIVE(machineNotice, "(J)[B"),
    PHPVPN_NATIVE(machineClose, "(J)V"),
    PHPVPN_NATIVE(classifySave, "([J[B[BZJ)[[B"),
    PHPVPN_NATIVE(passwordCreate, "()J"),
    PHPVPN_NATIVE(passwordDestroy, "(J)V"),
    PHPVPN_NATIVE(passwordUpdate, "(J[C)I"),
    PHPVPN_NATIVE(passwordState, "(J)[I"),
    PHPVPN_NATIVE(passwordKey, "(J)[C"),
    PHPVPN_NATIVE(passwordClear, "(J)V"),
    PHPVPN_NATIVE(passwordRandom, "()[C"),
    PHPVPN_NATIVE(serverInfoParse, "([B[[B)J"),
    PHPVPN_NATIVE(serverInfoDestroy, "(J)V"),
    PHPVPN_NATIVE(serverInfoCounts, "(J)[J"),
    PHPVPN_NATIVE(serverInfoRowAddresses, "(J)[I"),
    PHPVPN_NATIVE(serverInfoRowStates, "(J)[I"),
    PHPVPN_NATIVE(serverInfoRowLastSeen, "(J)[J"),
    PHPVPN_NATIVE(serverInfoRowContract, "(JI)[B"),
    PHPVPN_NATIVE(serverInfoText, "(JI)[B"),
    PHPVPN_NATIVE(serverInfoStateText, "(I)[B"),
    PHPVPN_NATIVE(serverInfoPageCount, "(I)I"),
    PHPVPN_NATIVE(serverInfoPageNumbers, "(II)[I"),
    PHPVPN_NATIVE(configDocumentMatches, "([B[B)Z"),
    PHPVPN_NATIVE(configValidTemplate, "([B)Z"),
};

#undef PHPVPN_NATIVE

}  // namespace

bool registerModelNatives(JNIEnv* env) {
    if (byteArrayClass == nullptr) {
        jclass local = env->FindClass("[B");
        if (local == nullptr) return false;
        byteArrayClass = static_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
        if (byteArrayClass == nullptr) return false;
    }
    jclass natives = env->FindClass("phpvpn/android/bridge/jni/ModelNatives");
    if (natives == nullptr) return false;  // NoClassDefFoundError is pending
    const jint registered = env->RegisterNatives(natives, kModelMethods, static_cast<jint>(std::size(kModelMethods)));
    env->DeleteLocalRef(natives);
    return registered == JNI_OK;
}

}  // namespace phpvpn
