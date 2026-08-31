#include "shared_library.hh"

#include <dlfcn.h>

#include <string>
#include <utility>

namespace nds::client {

Result<SharedLibrary> SharedLibrary::open(std::string_view path) {
    if (path.empty())
        return Error{ErrorCode::kInvalidArgument, "shared-library path is required"};
    const std::string path_copy(path);
    void *handle = dlopen(path_copy.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char *loader_error = dlerror();
        return Error{ErrorCode::kRuntime,
                     "unable to load " + path_copy + ": " +
                         (loader_error == nullptr ? "unknown dynamic-loader error" : loader_error)};
    }
    return SharedLibrary(handle);
}

SharedLibrary::SharedLibrary(void *handle) noexcept : handle_(handle) {}

SharedLibrary::~SharedLibrary() {
    close();
}

SharedLibrary::SharedLibrary(SharedLibrary &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

SharedLibrary &SharedLibrary::operator=(SharedLibrary &&other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

void SharedLibrary::close() noexcept {
    if (handle_ != nullptr)
        (void)dlclose(handle_);
    handle_ = nullptr;
}

void *SharedLibrary::release() noexcept {
    return std::exchange(handle_, nullptr);
}

Result<void *> SharedLibrary::resolve(const char *name) const {
    if (handle_ == nullptr || name == nullptr)
        return Error{ErrorCode::kInvalidArgument, "shared-library symbol resolution requires a handle and name"};
    (void)dlerror();
    void *symbol = dlsym(handle_, name);
    const char *loader_error = dlerror();
    if (loader_error != nullptr || symbol == nullptr) {
        return Error{ErrorCode::kRuntime, "required symbol " + std::string(name) + " is unavailable: " +
                                              (loader_error == nullptr ? "returned null" : loader_error)};
    }
    return symbol;
}

void *SharedLibrary::resolve_optional_symbol(const char *name) const noexcept {
    if (handle_ == nullptr || name == nullptr)
        return nullptr;
    (void)dlerror();
    void *symbol = dlsym(handle_, name);
    return dlerror() == nullptr ? symbol : nullptr;
}

}  // namespace nds::client
