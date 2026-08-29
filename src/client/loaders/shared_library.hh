#ifndef NDS_CLIENT_LOADERS_SHARED_LIBRARY_HH
#define NDS_CLIENT_LOADERS_SHARED_LIBRARY_HH

#include "result.hh"

#include <cstddef>
#include <cstring>
#include <string_view>

namespace nds::client {

class SharedLibrary {
public:
    static Result<SharedLibrary> open(std::string_view path);

    SharedLibrary() = default;
    explicit SharedLibrary(void *handle) noexcept;
    ~SharedLibrary();
    SharedLibrary(const SharedLibrary &) = delete;
    SharedLibrary &operator=(const SharedLibrary &) = delete;
    SharedLibrary(SharedLibrary &&other) noexcept;
    SharedLibrary &operator=(SharedLibrary &&other) noexcept;

    void close() noexcept;
    void *release() noexcept;

    template <typename Function>
    Result<Function> resolve_required(const char *name) const {
        static_assert(sizeof(Function) == sizeof(void *), "function pointer and dlsym result must have equal size");
        const auto symbol = resolve(name);
        if (!symbol)
            return unexpected(symbol.error());
        Function function{};
        std::memcpy(&function, &*symbol, sizeof(function));
        return function;
    }

    template <typename Function>
    Result<void> resolve_required(const char *name, Function *destination) const {
        if (destination == nullptr)
            return unexpected(ErrorCode::kInvalidArgument, "shared-library symbol destination is required");
        const auto function = resolve_required<Function>(name);
        if (!function)
            return unexpected(function.error());
        *destination = *function;
        return {};
    }

    template <typename Function>
    Function resolve_optional(const char *name) const noexcept {
        static_assert(sizeof(Function) == sizeof(void *), "function pointer and dlsym result must have equal size");
        const auto symbol = resolve_optional_symbol(name);
        Function function{};
        if (symbol != nullptr)
            std::memcpy(&function, &symbol, sizeof(function));
        return function;
    }

    template <typename Function>
    bool resolve_optional(const char *name, Function *destination) const noexcept {
        if (destination == nullptr)
            return false;
        *destination = resolve_optional<Function>(name);
        return *destination != nullptr;
    }

private:
    Result<void *> resolve(const char *name) const;
    void *resolve_optional_symbol(const char *name) const noexcept;

    void *handle_{};
};

}  // namespace nds::client

#endif
