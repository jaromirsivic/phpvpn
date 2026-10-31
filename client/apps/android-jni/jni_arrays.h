// Array helpers of the JNI entry (plan-android-v001 step 4).
//
// Text crosses JNI only as byte[] (UTF-8) or char[] (UTF-16 that the Kotlin
// side keeps in a wipeable CharArray), never as jstring: modified UTF-8 would
// change U+0000 and supplementary characters. Every helper bounds the size
// before it copies and reports a JNI failure (pending OutOfMemoryError) as an
// empty result, never as an exception that unwinds into the VM.
#pragma once

#include <jni.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/base/secure_buffer.h"

namespace phpvpn::jni {

// UTF-8 or binary bytes of a byte[] of at most `maxBytes`; nullopt for null,
// for a larger array (nothing is copied) and on a JNI failure.
inline std::optional<std::string> readBytes(JNIEnv* env, jbyteArray array, std::size_t maxBytes) {
    if (array == nullptr) return std::nullopt;
    const jsize length = env->GetArrayLength(array);
    if (length < 0 || static_cast<std::size_t>(length) > maxBytes) return std::nullopt;
    std::string text(static_cast<std::size_t>(length), '\0');
    if (length > 0) env->GetByteArrayRegion(array, 0, length, reinterpret_cast<jbyte*>(text.data()));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        secureWipe(text.data(), text.size());
        return std::nullopt;
    }
    return text;
}

// A new byte[] with `bytes`; null with a pending OutOfMemoryError.
inline jbyteArray newBytes(JNIEnv* env, std::string_view bytes) {
    const auto length = static_cast<jsize>(bytes.size());
    jbyteArray array = env->NewByteArray(length);
    if (array == nullptr) return nullptr;
    if (length > 0) env->SetByteArrayRegion(array, 0, length, reinterpret_cast<const jbyte*>(bytes.data()));
    return array;
}

// UTF-16 code units of a char[] of at most `maxUnits`, without conversion.
inline std::optional<std::u16string> readChars(JNIEnv* env, jcharArray array, std::size_t maxUnits) {
    if (array == nullptr) return std::nullopt;
    const jsize length = env->GetArrayLength(array);
    if (length < 0 || static_cast<std::size_t>(length) > maxUnits) return std::nullopt;
    std::u16string text(static_cast<std::size_t>(length), u'\0');
    if (length > 0) env->GetCharArrayRegion(array, 0, length, reinterpret_cast<jchar*>(text.data()));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        secureWipe(text.data(), text.size() * sizeof(char16_t));
        return std::nullopt;
    }
    return text;
}

// Strict UTF-16 to UTF-8: a lone surrogate fails (nullopt), nothing is
// replaced. The caller wipes the result when it held a secret.
inline std::optional<std::string> utf16ToUtf8(std::u16string_view text) {
    std::string out;
    out.reserve(text.size() * 3);
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::uint32_t code = text[i];
        if (code >= 0xd800 && code <= 0xdbff) {
            if (i + 1 == text.size() || text[i + 1] < 0xdc00 || text[i + 1] > 0xdfff) {
                secureWipe(out.data(), out.size());
                return std::nullopt;
            }
            code = 0x10000 + ((code - 0xd800) << 10) + (static_cast<std::uint32_t>(text[++i]) - 0xdc00);
        } else if (code >= 0xdc00 && code <= 0xdfff) {
            secureWipe(out.data(), out.size());
            return std::nullopt;
        }
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xc0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xe0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        }
    }
    return out;
}

// Strict UTF-8 to UTF-16 (no overlong forms, no surrogates, at most
// U+10FFFF); nullopt for invalid input.
inline std::optional<std::u16string> utf8ToUtf16(std::string_view text) {
    std::u16string out;
    out.reserve(text.size());
    std::size_t i = 0;
    const auto fail = [&out]() -> std::optional<std::u16string> {
        secureWipe(out.data(), out.size() * sizeof(char16_t));
        return std::nullopt;
    };
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::uint32_t code = 0;
        std::size_t extra = 0;
        if (lead < 0x80) {
            code = lead;
        } else if (lead >= 0xc2 && lead <= 0xdf) {
            code = lead & 0x1f;
            extra = 1;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            code = lead & 0x0f;
            extra = 2;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            code = lead & 0x07;
            extra = 3;
        } else {
            return fail();
        }
        if (extra > 0 && text.size() - i <= extra) return fail();  // truncated sequence
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xc0) != 0x80) return fail();
            code = (code << 6) | (next & 0x3f);
        }
        if ((extra == 2 && code < 0x800) || (extra == 3 && (code < 0x10000 || code > 0x10ffff)) ||
            (code >= 0xd800 && code <= 0xdfff))
            return fail();
        if (code >= 0x10000) {
            code -= 0x10000;
            out.push_back(static_cast<char16_t>(0xd800 + (code >> 10)));
            out.push_back(static_cast<char16_t>(0xdc00 + (code & 0x3ff)));
        } else {
            out.push_back(static_cast<char16_t>(code));
        }
        i += extra + 1;
    }
    return out;
}

// A new char[] with UTF-16 units; null with a pending OutOfMemoryError.
inline jcharArray newChars(JNIEnv* env, std::u16string_view units) {
    const auto length = static_cast<jsize>(units.size());
    jcharArray array = env->NewCharArray(length);
    if (array == nullptr) return nullptr;
    if (length > 0) env->SetCharArrayRegion(array, 0, length, reinterpret_cast<const jchar*>(units.data()));
    return array;
}

// A new char[] for UTF-8 text (a secret is wiped by the caller); null for
// invalid UTF-8 or a JNI failure. The intermediate UTF-16 copy is wiped here.
inline jcharArray newCharsFromUtf8(JNIEnv* env, std::string_view utf8) {
    auto units = utf8ToUtf16(utf8);
    if (!units) return nullptr;
    jcharArray array = newChars(env, *units);
    secureWipe(units->data(), units->size() * sizeof(char16_t));
    return array;
}

// RAII wipe of a temporary secret copy.
struct WipeOnExit {
    std::string& text;
    ~WipeOnExit() { secureWipe(text.data(), text.size()); }
};
struct WipeUnitsOnExit {
    std::u16string& text;
    ~WipeUnitsOnExit() { secureWipe(text.data(), text.size() * sizeof(char16_t)); }
};

}  // namespace phpvpn::jni
