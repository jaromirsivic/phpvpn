#include "core/base/secure_buffer.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace phpvpn {

void secureWipe(void* data, std::size_t size) noexcept {
    if (data == nullptr || size == 0) {
        return;
    }
#if defined(_WIN32)
    ::SecureZeroMemory(data, size);
#else
    volatile unsigned char* p = static_cast<volatile unsigned char*>(data);
    while (size-- > 0) {
        *p++ = 0;
    }
#endif
}

bool constantTimeEquals(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) noexcept {
    // The length difference is folded into the accumulator instead of returning
    // early, so a caller cannot learn the length from the timing either.
    std::size_t common = a.size() < b.size() ? a.size() : b.size();
    unsigned int diff = static_cast<unsigned int>(a.size() ^ b.size());
    for (std::size_t i = 0; i < common; ++i) {
        diff |= static_cast<unsigned int>(a[i] ^ b[i]);
    }
    return diff == 0;
}

bool constantTimeEquals(std::string_view a, std::string_view b) noexcept {
    return constantTimeEquals(asBytes(a), asBytes(b));
}

std::span<const std::uint8_t> asBytes(std::string_view text) noexcept {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

std::string toString(std::span<const std::uint8_t> bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

}  // namespace phpvpn
