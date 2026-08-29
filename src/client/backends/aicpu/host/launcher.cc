#include "launcher.hh"

#include <utility>

namespace nds {

AicpuLauncher::~AicpuLauncher() {
    reset();
}

Result<void> AicpuLauncher::load(const std::string &kernel_config_path) {
    aclrtBinaryLoadOption option{};
    aclrtBinaryLoadOptions options{};

    if (loaded() || kernel_config_path.empty()) {
        return unexpected(ErrorCode::kInvalidArgument,
                          loaded() ? "NDS AICPU launcher is already loaded"
                                   : "NDS AICPU launch requires the NDS-built nds_aicpu_standard.json path");
    }
    option.type = ACL_RT_BINARY_LOAD_OPT_CPU_KERNEL_MODE;
    option.value.cpuKernelMode = 0;
    options.options = &option;
    options.numOpt = 1U;
    const int load_result = aclrtBinaryLoadFromFile(kernel_config_path.c_str(), &options, &binary_);
    if (load_result != ACL_SUCCESS || binary_ == nullptr) {
        const std::string error =
            "aclrtBinaryLoadFromFile(NDS AICPU package configuration) failed: " + std::to_string(load_result);
        reset();
        return unexpected(ErrorCode::kRuntime, error);
    }
    const int stream_result = aclrtCreateStreamWithConfig(&stream_, 0U, ACL_STREAM_FAST_LAUNCH | ACL_STREAM_FAST_SYNC);
    if (stream_result != ACL_SUCCESS || stream_ == nullptr) {
        const std::string error =
            "aclrtCreateStreamWithConfig for NDS AICPU launch failed: " + std::to_string(stream_result);
        reset();
        return unexpected(ErrorCode::kRuntime, error);
    }
    return {};
}

Result<void> AicpuLauncher::launch_and_wait(const char *kernel_name, std::uint64_t args_gm_addr,
                                            std::int32_t completion_timeout_ms) {
    if (!loaded() || kernel_name == nullptr || args_gm_addr == 0U || completion_timeout_ms <= 0) {
        return unexpected(
            ErrorCode::kInvalidArgument,
            "NDS AICPU launch requires a loaded package, kernel name, args address, and positive timeout");
    }

    const auto [entry, inserted] = functions_.try_emplace(kernel_name, nullptr);
    if (inserted) {
        const int function_result = aclrtBinaryGetFunction(binary_, kernel_name, &entry->second);
        if (function_result != ACL_SUCCESS || entry->second == nullptr) {
            functions_.erase(entry);
            return unexpected(ErrorCode::kRuntime, "NDS AICPU package does not expose " + std::string(kernel_name) +
                                                       ": " + std::to_string(function_result));
        }
    }

    aclrtLaunchKernelAttr attribute{};
    attribute.id = ACL_RT_LAUNCH_KERNEL_ATTR_TIMEOUT;
    attribute.value.timeout = 5U;
    aclrtLaunchKernelCfg config{&attribute, 1U};
    const int launch_result = aclrtLaunchKernelWithHostArgs(entry->second, 1U, stream_, &config, &args_gm_addr,
                                                            sizeof(args_gm_addr), nullptr, 0U);
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

void AicpuLauncher::reset() noexcept {
    if (stream_ != nullptr)
        (void)aclrtDestroyStream(stream_);
    stream_ = nullptr;
    functions_.clear();
    if (binary_ != nullptr)
        (void)aclrtBinaryUnLoad(binary_);
    binary_ = nullptr;
}

bool AicpuLauncher::loaded() const noexcept {
    return binary_ != nullptr && stream_ != nullptr;
}

}  // namespace nds
