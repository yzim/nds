#ifndef NDS_RESULT_HH
#define NDS_RESULT_HH

#include <tl/expected.hpp>

#include <string>
#include <utility>

namespace nds {

enum class ErrorCode {
    kInvalidArgument,
    kUnsupported,
    kRuntime,
    kRa,
    kVerbs,
    kTransport,
    kProtocol,
};

struct Error {
    ErrorCode code;
    std::string message;

    Error(ErrorCode code_value, std::string message_value) : code(code_value), message(std::move(message_value)) {}
};

/* Keep the expected implementation private. Result deliberately exposes
 * status-like access only: ok(), value(), and error(). Pointer-like expected
 * operators are not part of the NDS interface. */
template <typename T>
class Result {
public:
    Result(const T &value) : storage_(value) {}
    Result(T &&value) : storage_(std::move(value)) {}
    Result(const Error &error) : storage_(tl::make_unexpected(error)) {}
    Result(Error &&error) : storage_(tl::make_unexpected(std::move(error))) {}

    bool ok() const noexcept {
        return storage_.has_value();
    }
    explicit operator bool() const noexcept {
        return ok();
    }

    T &value() & {
        return storage_.value();
    }
    const T &value() const & {
        return storage_.value();
    }
    T &&value() && {
        return std::move(storage_).value();
    }
    const T &&value() const && {
        return std::move(storage_).value();
    }

    Error &error() & {
        return storage_.error();
    }
    const Error &error() const & {
        return storage_.error();
    }
    Error &&error() && {
        return std::move(storage_).error();
    }

private:
    tl::expected<T, Error> storage_;
};

template <>
class Result<void> {
public:
    Result() = default;
    Result(const Error &error) : storage_(tl::make_unexpected(error)) {}
    Result(Error &&error) : storage_(tl::make_unexpected(std::move(error))) {}

    bool ok() const noexcept {
        return storage_.has_value();
    }
    explicit operator bool() const noexcept {
        return ok();
    }

    Error &error() & {
        return storage_.error();
    }
    const Error &error() const & {
        return storage_.error();
    }
    Error &&error() && {
        return std::move(storage_).error();
    }

private:
    tl::expected<void, Error> storage_;
};

}  // namespace nds

#define NDS_RETURN_IF_ERROR(expression)                       \
    do {                                                      \
        const decltype(expression) nds_result = (expression); \
        if (!nds_result.ok())                                 \
            return nds_result.error();                        \
    } while (false)

#endif
