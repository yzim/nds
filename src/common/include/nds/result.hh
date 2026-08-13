#ifndef NDS_RESULT_HH
#define NDS_RESULT_HH

#include <tl/expected.hpp>

#include <string>
#include <type_traits>
#include <utility>

namespace nds {

enum class ErrorCode {
    kInvalidArgument,
    kRuntime,
    kRa,
    kVerbs,
    kTransport,
    kProtocol,
};

struct Error {
    ErrorCode code;
    std::string message;
};

template <typename T>
using Result = tl::expected<T, Error>;

class Failure {
public:
    explicit Failure(Error error) : error_(std::move(error)) {}

    template <typename T>
    operator Result<T>() && {
        return tl::make_unexpected(std::move(error_));
    }

private:
    Error error_;
};

inline Failure failure(ErrorCode code, std::string message) {
    return Failure{{code, std::move(message)}};
}

inline Failure propagate(const Error &error) {
    return Failure{error};
}

inline Result<void> success() {
    return {};
}

template <typename T>
Result<std::decay_t<T>> success(T &&value) {
    return std::forward<T>(value);
}

}  // namespace nds

#endif
