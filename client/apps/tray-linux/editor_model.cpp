#include "editor_model.h"

#include "core/base/secure_buffer.h"
#include "core/config/config_document.h"

namespace phpvpn::tray {
std::string prettyEditorJson(const std::string& text) {
    if (!Json::parse(text)) return text;
    // Reflow only whitespace after validation. Retain exact number tokens
    // (including decimals beyond double precision), escapes and member order.
    std::string result;
    result.reserve(text.size());
    int depth = 0;
    bool quoted = false, escaped = false;
    char previous = 0;
    const auto newline = [&] {
        result.push_back('\n');
        result.append(static_cast<std::size_t>(depth) * 2, ' ');
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            result.push_back(c);
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '}' || c == ']') {
            --depth;
            if (previous != '{' && previous != '[') newline();
        }
        result.push_back(c);
        if (c == '"') quoted = true;
        else if (c == '{' || c == '[') {
            ++depth;
            const auto next = text.find_first_not_of(" \t\r\n", i + 1);
            if (next != std::string::npos && text[next] != '}' && text[next] != ']') newline();
        } else if (c == ',') newline();
        else if (c == ':') result.push_back(' ');
        previous = c;
    }
    return result;
}

EditorPresentation presentEditorDocument(const EditorDocument& document, const std::string& original) {
    EditorPresentation presentation;
    presentation.literal = document.literal();
    presentation.text = presentation.literal ? document.presentation() : prettyEditorJson(original);
    presentation.title = std::string(kEditorTitle) + (presentation.literal ? std::string(kEditorLiteralSuffix) : std::string{});
    return presentation;
}

EditorSaveText editorSaveText(const EditorDocument& document, std::string edited, bool modified) {
    EditorSaveText result;
    auto text = document.edited(std::move(edited), modified);
    if (text) {
        result.document = std::move(text).value();
        return result;
    }
    result.error = text.code() == ErrorCode::config_encoding_error ? kEditorInvalidUnicode : kEditorLiteralRequired;
    return result;
}

EditorSaveNotice editorSaveNotice(const SaveResult& result) {
    EditorSaveNotice notice;
    notice.warning = result.outcome == SaveOutcome::unknown;
    notice.text = notice.warning ? std::string(kEditorUnknownText)
                : result.outcome == SaveOutcome::notSent ? std::string(kEditorNotSentText)
                                                         : std::string(kEditorRejectedText);
    // Only the closed header grammar reaches here (save_response.cpp): a path, a
    // line number or a header name, never the rejected configuration text.
    if (result.outcome == SaveOutcome::rejected && !result.validationDescription.empty())
        notice.text += "\n\n" + result.validationDescription;
    return notice;
}

EditorDefault editorDefaultFromReply(const Result<WizardReply>& reply) {
    EditorDefault result;
    if (!reply) {
        result.error = errorCodeDescription(reply.code());
        return result;
    }
    if (reply->error) {
        result.error = reply->error->description + " " + reply->error->suggestedAction;
        return result;
    }
    // The closed reply validation checks the field type; validate the shared
    // template too before replacing any user's draft.
    const auto* document = reply->result.find("document");
    if (!document || !document->isString()) {
        result.error = errorCodeDescription(ErrorCode::response_invalid);
        return result;
    }
    auto normalized = normalizeConfigText(document->stringValue());
    if (!normalized) {
        result.error = errorCodeDescription(normalized.code());
        return result;
    }
    ConfigWarnings warnings;
    auto config = parseAndValidateConfig(normalized.value(), warnings);
    if (!config) {
        auto& text = normalized.value();
        secureWipe(text.data(), text.size());
        result.error = errorCodeDescription(config.code());
        return result;
    }
    result.document = std::move(normalized).value();
    return result;
}

void EditorState::open() {
    open_ = true;
    ++windowGeneration_;
    ++draftGeneration_;
    saving_ = unknown_ = savedPending_ = resetting_ = false;
    operation_ = {};
    bump();
}

void EditorState::close() {
    open_ = false;
    ++windowGeneration_;
    ++draftGeneration_;
    ++operationGeneration_;
    saving_ = unknown_ = savedPending_ = resetting_ = false;
    bump();
}

void EditorState::endSession() {
    ending_ = true;
    close();
}

void EditorState::draftChanged() {
    ++draftGeneration_;
    bump();
}

void EditorState::resetStarted() {
    resetting_ = true;
    bump();
}

void EditorState::resetFinished() {
    resetting_ = false;
    bump();
}

std::optional<SaveOperation> EditorState::beginSave(std::uint64_t nowMs, std::uint64_t deadlineMs) {
    if (!open_ || saving_ || savedPending_ || unknown_ || ending_ || resetting_ || deadlineMs <= nowMs) return std::nullopt;
    operation_ = {windowGeneration_, draftGeneration_, ++operationGeneration_, deadlineMs};
    saving_ = true;
    bump();
    return operation_;
}

EditorState::Finish EditorState::finishSave(const SaveResult& result) {
    if (!open_ || ending_ || !saving_ || result.operation != operation_ ||
        result.operation.windowGeneration != windowGeneration_ || result.operation.draftGeneration != draftGeneration_)
        return Finish::ignored;
    saving_ = false;
    unknown_ = result.outcome == SaveOutcome::unknown;
    if (result.outcome == SaveOutcome::confirmed) savedPending_ = true;
    bump();
    return result.outcome == SaveOutcome::confirmed ? Finish::confirmed : Finish::failed;
}

LifecycleSnapshot EditorState::snapshot(bool modified) const {
    LifecycleSnapshot snapshot;
    snapshot.open = open_;
    snapshot.dirty = open_ && modified;
    snapshot.unknown = open_ && unknown_;
    snapshot.busy = open_ && saving_;
    snapshot.generation = revision_;
    return snapshot;
}
}  // namespace phpvpn::tray
