// Result<T> carries either a value or a stable Error across every role
// boundary. Decision 14 of plan_client.md forbids propagating exceptions
// between roles, so the whole core returns this type instead.
#pragma once

#include <cassert>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "core/base/error.h"

namespace phpvpn {

template <class T>
class [[nodiscard]] Result {
public:
    using value_type = T;

    Result(T value) : storage_(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : storage_(std::in_place_index<1>, std::move(error)) {}

    static Result failure(ErrorCode code, std::string detail = {}) {
        return Result(Error{code, std::move(detail)});
    }

    bool ok() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return ok(); }

    const Error& error() const noexcept {
        assert(!ok());
        return std::get<1>(storage_);
    }

    ErrorCode code() const noexcept { return ok() ? ErrorCode::none : error().code; }

    T& value() & noexcept {
        assert(ok());
        return std::get<0>(storage_);
    }
    const T& value() const& noexcept {
        assert(ok());
        return std::get<0>(storage_);
    }
    T&& value() && noexcept {
        assert(ok());
        return std::move(std::get<0>(storage_));
    }

    T* operator->() noexcept { return &value(); }
    const T* operator->() const noexcept { return &value(); }
    T& operator*() & noexcept { return value(); }
    const T& operator*() const& noexcept { return value(); }

    T valueOr(T fallback) const {
        return ok() ? std::get<0>(storage_) : std::move(fallback);
    }

private:
    std::variant<T, Error> storage_;
};

template <>
class [[nodiscard]] Result<void> {
public:
    using value_type = void;

    Result() = default;
    Result(Error error) : error_(std::move(error)) {}

    static Result success() { return Result(); }
    static Result failure(ErrorCode code, std::string detail = {}) {
        return Result(Error{code, std::move(detail)});
    }

    bool ok() const noexcept { return !error_.has_value(); }
    explicit operator bool() const noexcept { return ok(); }

    const Error& error() const noexcept {
        assert(!ok());
        return *error_;
    }

    ErrorCode code() const noexcept { return ok() ? ErrorCode::none : error_->code; }

private:
    std::optional<Error> error_;
};

using Status = Result<void>;

inline Status ok() { return Status::success(); }

inline Status fail(ErrorCode code, std::string detail = {}) {
    return Status::failure(code, std::move(detail));
}

// Propagates the error of `expr` out of the enclosing function. The macro keeps
// the call sites of the layered parsers readable without exceptions.
#define PHPVPN_TRY(expr)                                     \
    do {                                                     \
        auto phpvpn_try_result_ = (expr);                    \
        if (!phpvpn_try_result_.ok()) {                      \
            return ::phpvpn::Error(phpvpn_try_result_.error()); \
        }                                                    \
    } while (false)

#define PHPVPN_TRY_ASSIGN(target, expr)                      \
    do {                                                     \
        auto phpvpn_try_result_ = (expr);                    \
        if (!phpvpn_try_result_.ok()) {                      \
            return ::phpvpn::Error(phpvpn_try_result_.error()); \
        }                                                    \
        (target) = std::move(phpvpn_try_result_).value();    \
    } while (false)

}  // namespace phpvpn
