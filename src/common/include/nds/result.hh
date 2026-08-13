#ifndef NDS_RESULT_HH
#define NDS_RESULT_HH

#include <tl/expected.hpp>

#include <string>
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

inline tl::unexpected<Error> failure(ErrorCode code, std::string message) {
    return tl::make_unexpected(Error{code, std::move(message)});
}

}  // namespace nds

#endif
