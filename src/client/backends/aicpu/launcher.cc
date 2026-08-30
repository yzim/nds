#include "launcher.hh"

namespace nds {

AicpuLauncher::~AicpuLauncher() {
    reset();
}

Result<void> AicpuLauncher::load(const std::string &kernel_path) {
    aclrtBinaryLoadOption option{};
    aclrtBinaryLoadOptions options{};
    if (loaded() || kernel_path.empty())
        return unexpected(ErrorCode::kInvalidArgument, loaded() ? "NDS AICPU launcher is already loaded"
                                                                : "NDS AICPU requires an NDS kernel artifact path");

    option.type = ACL_RT_BINARY_LOAD_OPT_CPU_KERNEL_MODE;
    option.value.cpuKernelMode = 0;
    options.options = &option;
    options.numOpt = 1U;
    const int load_result = aclrtBinaryLoadFromFile(kernel_path.c_str(), &options, &binary_);
    if (load_result != ACL_SUCCESS || binary_ == nullptr) {
        reset();
        return unexpected(ErrorCode::kRuntime,
                          "aclrtBinaryLoadFromFile(AICPU kernel artifact) failed: " + std::to_string(load_result));
    }

    const int stream_result = aclrtCreateStreamWithConfig(&stream_, 0U, ACL_STREAM_FAST_LAUNCH | ACL_STREAM_FAST_SYNC);
    if (stream_result != ACL_SUCCESS || stream_ == nullptr) {
        reset();
        return unexpected(ErrorCode::kRuntime, "AICPU launch stream creation failed: " + std::to_string(stream_result));
    }
    return {};
}

Result<void> AicpuLauncher::launch(const char *kernel_name, std::uint64_t args_gm_addr) {
    if (!loaded() || kernel_name == nullptr || args_gm_addr == 0U)
        return unexpected(ErrorCode::kInvalidArgument, "invalid AICPU launch arguments");

    const auto [entry, inserted] = functions_.try_emplace(kernel_name, nullptr);
    if (inserted) {
        const int result = aclrtBinaryGetFunction(binary_, kernel_name, &entry->second);
        if (result != ACL_SUCCESS || entry->second == nullptr) {
            functions_.erase(entry);
            return unexpected(ErrorCode::kRuntime, "AICPU kernel entry lookup failed: " + std::string(kernel_name));
        }
    }

    aclrtLaunchKernelAttr attribute{};
    attribute.id = ACL_RT_LAUNCH_KERNEL_ATTR_TIMEOUT;
    attribute.value.timeout = 5U;
    aclrtLaunchKernelCfg config{&attribute, 1U};
    const int result = aclrtLaunchKernelWithHostArgs(entry->second, 1U, stream_, &config, &args_gm_addr,
                                                     sizeof(args_gm_addr), nullptr, 0U);
    return result == ACL_SUCCESS
               ? Result<void>{}
               : unexpected(ErrorCode::kRuntime, "AICPU kernel launch failed: " + std::string(kernel_name));
}

Result<void> AicpuLauncher::synchronize(std::int32_t completion_timeout_ms) {
    if (!loaded() || completion_timeout_ms <= 0)
        return unexpected(ErrorCode::kInvalidArgument, "invalid AICPU synchronization timeout");
    const int result = aclrtSynchronizeStreamWithTimeout(stream_, completion_timeout_ms);
    return result == ACL_SUCCESS ? Result<void>{}
                                 : unexpected(ErrorCode::kRuntime, "AICPU kernel synchronization failed");
}

Result<void> AicpuLauncher::launch_and_wait(const char *kernel_name, std::uint64_t args_gm_addr,
                                            std::int32_t completion_timeout_ms) {
    if (const auto submitted = launch(kernel_name, args_gm_addr); !submitted)
        return unexpected(submitted.error());
    return synchronize(completion_timeout_ms);
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
