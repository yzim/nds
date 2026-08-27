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
};

// Keep the expected implementation private here. Replace tl::expected with
// std::expected when NDS moves to C++23 without changing NDS call sites.
template <typename T>
using Result = tl::expected<T, Error>;

class Unexpected {
public:
    explicit Unexpected(Error error) : error_(std::move(error)) {}

    template <typename T>
    operator Result<T>() && {
        return tl::make_unexpected(std::move(error_));
    }

private:
    Error error_;
};

inline Unexpected unexpected(ErrorCode code, std::string message) {
    return Unexpected{{code, std::move(message)}};
}

inline Unexpected unexpected(const Error &error) {
    return Unexpected{error};
}

}  // namespace nds

#define NDS_RETURN_IF_ERROR(expression)                       \
    do {                                                      \
        const decltype(expression) nds_result = (expression); \
        if (!nds_result)                                      \
            return nds::unexpected(nds_result.error());       \
    } while (false)

#endif
