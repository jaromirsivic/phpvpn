// Owning byte buffer that wipes itself on destruction.
//
// Derived keys, access keys and payload keys live here so that a crash dump or
// a freed heap page cannot leak them, see client.md "Bezpecnostni model".
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace phpvpn {

// Overwrites `size` bytes at `data` in a way the optimiser may not remove.
void secureWipe(void* data, std::size_t size) noexcept;

// Length aware comparison that always inspects every byte of the shorter
// operand, so timing does not reveal where two tags start to differ.
bool constantTimeEquals(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) noexcept;
bool constantTimeEquals(std::string_view a, std::string_view b) noexcept;

class SecureBuffer {
public:
    SecureBuffer() = default;

    explicit SecureBuffer(std::size_t size) : bytes_(size, 0) {}

    explicit SecureBuffer(std::span<const std::uint8_t> data)
        : bytes_(data.begin(), data.end()) {}

    SecureBuffer(const SecureBuffer& other) : bytes_(other.bytes_) {}

    SecureBuffer(SecureBuffer&& other) noexcept : bytes_(std::move(other.bytes_)) {
        other.bytes_.clear();
    }

    SecureBuffer& operator=(const SecureBuffer& other) {
        if (this != &other) {
            wipe();
            bytes_ = other.bytes_;
        }
        return *this;
    }

    SecureBuffer& operator=(SecureBuffer&& other) noexcept {
        if (this != &other) {
            wipe();
            bytes_ = std::move(other.bytes_);
            other.bytes_.clear();
        }
        return *this;
    }

    ~SecureBuffer() { wipe(); }

    void assign(std::span<const std::uint8_t> data) {
        wipe();
        bytes_.assign(data.begin(), data.end());
    }

    void resize(std::size_t size) {
        if (size < bytes_.size()) {
            secureWipe(bytes_.data() + size, bytes_.size() - size);
        }
        bytes_.resize(size, 0);
    }

    void clear() { wipe(); }

    std::uint8_t* data() noexcept { return bytes_.data(); }
    const std::uint8_t* data() const noexcept { return bytes_.data(); }
    std::size_t size() const noexcept { return bytes_.size(); }
    bool empty() const noexcept { return bytes_.empty(); }

    std::span<std::uint8_t> mutableSpan() noexcept { return {bytes_.data(), bytes_.size()}; }
    std::span<const std::uint8_t> span() const noexcept { return {bytes_.data(), bytes_.size()}; }

    std::uint8_t& operator[](std::size_t index) noexcept { return bytes_[index]; }
    const std::uint8_t& operator[](std::size_t index) const noexcept { return bytes_[index]; }

    bool equals(const SecureBuffer& other) const noexcept {
        return constantTimeEquals(span(), other.span());
    }

private:
    void wipe() noexcept {
        if (!bytes_.empty()) {
            secureWipe(bytes_.data(), bytes_.size());
            bytes_.clear();
        }
    }

    std::vector<std::uint8_t> bytes_;
};

// Small helpers shared by every module that moves between text and bytes.
std::span<const std::uint8_t> asBytes(std::string_view text) noexcept;
std::string toString(std::span<const std::uint8_t> bytes);

}  // namespace phpvpn
