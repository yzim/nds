#include "launcher.hh"

#include <utility>

namespace nds {

AivLauncher::~AivLauncher() {
    reset();
}

Result<void> AivLauncher::load(const std::string &kernel_path) {
    aclrtBinaryLoadOption option{};
    aclrtBinaryLoadOptions options{};

    if (loaded() || kernel_path.empty()) {
        return unexpected(ErrorCode::kInvalidArgument, loaded() ? "NDS AIV launcher is already loaded"
                                                                : "NDS AIV requires an NDS-built kernel binary path");
    }
    option.type = ACL_RT_BINARY_LOAD_OPT_LAZY_LOAD;
    option.value.isLazyLoad = 1U;
    options.options = &option;
    options.numOpt = 1U;
    const int load_result = aclrtBinaryLoadFromFile(kernel_path.c_str(), &options, &binary_);
    if (load_result != ACL_SUCCESS || binary_ == nullptr) {
        const std::string error = "aclrtBinaryLoadFromFile(NDS AIV binary) failed: " + std::to_string(load_result);
        reset();
        return unexpected(ErrorCode::kRuntime, error);
    }
    const int stream_result = aclrtCreateStream(&stream_);
    if (stream_result != ACL_SUCCESS || stream_ == nullptr) {
        const std::string error = "aclrtCreateStream for NDS AIV launch failed: " + std::to_string(stream_result);
        reset();
        return unexpected(ErrorCode::kRuntime, error);
    }
    return {};
}

Result<void> AivLauncher::launch_and_wait(const char *kernel_name, void *arguments, std::size_t argument_size,
                                          std::int32_t completion_timeout_ms) {
    if (!loaded() || kernel_name == nullptr || arguments == nullptr || argument_size == 0U ||
        completion_timeout_ms <= 0) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "NDS AIV launch requires a loaded binary, kernel name, arguments, and positive timeout");
    }

    const auto [entry, inserted] = functions_.try_emplace(kernel_name, nullptr);
    if (inserted) {
        const int function_result = aclrtBinaryGetFunction(binary_, kernel_name, &entry->second);
        if (function_result != ACL_SUCCESS || entry->second == nullptr) {
            functions_.erase(entry);
            return unexpected(ErrorCode::kRuntime, "NDS AIV binary does not expose " + std::string(kernel_name) + ": " +
                                                       std::to_string(function_result));
        }
    }

    aclrtLaunchKernelAttr attributes[2]{};
    attributes[0].id = ACL_RT_LAUNCH_KERNEL_ATTR_SCHEM_MODE;
    attributes[0].value.schemMode = 1U;
    attributes[1].id = ACL_RT_LAUNCH_KERNEL_ATTR_ENGINE_TYPE;
    attributes[1].value.engineType = ACL_RT_ENGINE_TYPE_AIV;
    aclrtLaunchKernelCfg config{attributes, 2U};
    const int launch_result =
        aclrtLaunchKernelWithHostArgs(entry->second, 1U, stream_, &config, arguments, argument_size, nullptr, 0U);
    if (launch_result != ACL_SUCCESS) {
        return unexpected(ErrorCode::kRuntime, "aclrtLaunchKernelWithHostArgs(" + std::string(kernel_name) +
                                                   ") failed: " + std::to_string(launch_result));
    }
    const int sync_result = aclrtSynchronizeStreamWithTimeout(stream_, completion_timeout_ms);
    if (sync_result != ACL_SUCCESS) {
        return unexpected(ErrorCode::kRuntime, "aclrtSynchronizeStreamWithTimeout after " + std::string(kernel_name) +
                                                   " failed: " + std::to_string(sync_result));
    }
    return {};
}

void AivLauncher::reset() noexcept {
    if (stream_ != nullptr)
        (void)aclrtDestroyStream(stream_);
    stream_ = nullptr;
    functions_.clear();
    if (binary_ != nullptr)
        (void)aclrtBinaryUnLoad(binary_);
    binary_ = nullptr;
}

bool AivLauncher::loaded() const noexcept {
    return binary_ != nullptr && stream_ != nullptr;
}

}  // namespace nds
