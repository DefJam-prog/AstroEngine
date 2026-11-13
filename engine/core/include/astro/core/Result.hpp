#pragma once

#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace astro::core
{
enum class ErrorCode
{
    InvalidArgument,
    NotFound,
    IoError,
    ParseError,
    AlreadyExists,
    DependencyError,
    InvalidState,
    InternalError,
    ValidationError,
    VersionMismatch,
    TypeMismatch
};

struct Error
{
    ErrorCode code;
    std::string message;

    friend bool operator==(const Error&, const Error&) = default;
};

template <typename T>
class [[nodiscard]] Result
{
    static_assert(!std::is_reference_v<T>);

public:
    static Result success(T value) { return Result(std::move(value)); }
    static Result failure(Error error) { return Result(std::move(error)); }

    [[nodiscard]] bool hasValue() const noexcept { return std::holds_alternative<T>(state_); }
    explicit operator bool() const noexcept { return hasValue(); }

    T& value() & { return std::get<T>(state_); }
    const T& value() const& { return std::get<T>(state_); }
    T&& value() && { return std::get<T>(std::move(state_)); }
    const Error& error() const& { return std::get<Error>(state_); }
    Error&& error() && { return std::get<Error>(std::move(state_)); }

private:
    explicit Result(T value) : state_(std::in_place_type<T>, std::move(value)) {}
    explicit Result(Error error) : state_(std::in_place_type<Error>, std::move(error)) {}
    std::variant<T, Error> state_;
};

template <>
class [[nodiscard]] Result<void>
{
public:
    static Result success() { return Result(std::monostate{}); }
    static Result failure(Error error) { return Result(std::move(error)); }
    [[nodiscard]] bool hasValue() const noexcept { return std::holds_alternative<std::monostate>(state_); }
    explicit operator bool() const noexcept { return hasValue(); }
    const Error& error() const& { return std::get<Error>(state_); }
    Error&& error() && { return std::get<Error>(std::move(state_)); }

private:
    explicit Result(std::monostate state) : state_(state) {}
    explicit Result(Error error) : state_(std::move(error)) {}
    std::variant<std::monostate, Error> state_;
};
}
