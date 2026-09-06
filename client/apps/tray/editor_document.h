#pragma once
#include "core/config/config_document.h"
namespace phpvpn::tray {
// Win32 EDIT cannot contain U+0000. Such malformed JSON is still valid UTF-8,
// so expose a labelled, lossless JSON string representation for repair.
class EditorDocument {
public:
    void load(std::string text) {
        original_ = std::move(text);
        literal_ = original_.find('\0') != std::string::npos;
    }
    bool literal() const { return literal_; }
    std::string presentation() const { return literal_ ? Json::escapeString(original_) : original_; }
    Result<std::string> edited(std::string text, bool modified) const {
        if (!modified) return original_;
        if (!isStrictUtf8(text)) return Error(ErrorCode::config_encoding_error);
        if (!literal_) return text;
        auto value = Json::parse(text);
        if (!value || !value->isString()) return Error(ErrorCode::config_invalid_value);
        return value->stringValue();
    }
private:
    std::string original_;
    bool literal_ = false;
};
} // namespace phpvpn::tray
