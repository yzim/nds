#include "launcher.hh"

#include <utility>

namespace nds {

AicpuLauncher::~AicpuLauncher() {
    reset();
}

Result<void> AicpuLauncher::load(NdsAclApi *acl, const std::string &kernel_config_path) {
    NdsAclBinaryLoadOption option{};
    NdsAclBinaryLoadOptions options{};

    if (acl == nullptr || loaded() || kernel_config_path.empty()) {
        return unexpected(ErrorCode::kInvalidArgument,
                          loaded() ? "NDS AICPU launcher is already loaded"
                                   : "NDS AICPU launch requires the NDS-built nds_aicpu_standard.json path");
    }
    if (acl->binary_load_from_file == nullptr || acl->binary_unload == nullptr || acl->binary_get_function == nullptr ||
        acl->launch_kernel_with_host_args == nullptr || acl->create_stream_with_config == nullptr ||
        acl->destroy_stream == nullptr || acl->synchronize_stream_with_timeout == nullptr) {
        return unexpected(ErrorCode::kRuntime,
                          "AscendCL is missing a required AICPU binary, host-argument launch, or stream symbol");
    }

    acl_ = acl;
    option.type = NDS_ACL_BINARY_LOAD_OPT_CPU_KERNEL_MODE;
    option.value.cpu_kernel_mode = NDS_ACL_CPU_KERNEL_REGISTER_JSON;
    options.options = &option;
    options.num_options = 1U;
    const int load_result = acl_->binary_load_from_file(kernel_config_path.c_str(), &options, &binary_);
    if (load_result != 0 || binary_ == nullptr) {
        const std::string error =
            "aclrtBinaryLoadFromFile(NDS AICPU package configuration) failed: " + std::to_string(load_result);
        reset();
        return unexpected(ErrorCode::kRuntime, error);
    }
    const int stream_result =
        acl_->create_stream_with_config(&stream_, 0U, NDS_ACL_STREAM_FAST_LAUNCH | NDS_ACL_STREAM_FAST_SYNC);
    if (stream_result != 0 || stream_ == nullptr) {
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
        const int function_result = acl_->binary_get_function(binary_, kernel_name, &entry->second);
        if (function_result != 0 || entry->second == nullptr) {
            functions_.erase(entry);
            return unexpected(ErrorCode::kRuntime, "NDS AICPU package does not expose " + std::string(kernel_name) +
                                                       ": " + std::to_string(function_result));
        }
    }

    NdsAclLaunchKernelAttr attribute{};
    attribute.id = NDS_ACL_LAUNCH_KERNEL_ATTR_TIMEOUT;
    attribute.value.timeout_seconds = 5U;
    NdsAclLaunchKernelConfig config{&attribute, 1U};
    const int launch_result = acl_->launch_kernel_with_host_args(entry->second, 1U, stream_, &config, &args_gm_addr,
                                                                 sizeof(args_gm_addr), nullptr, 0U);
    if (launch_result != 0) {
        return unexpected(ErrorCode::kRuntime, "aclrtLaunchKernelWithHostArgs(" + std::string(kernel_name) +
                                                   ") failed: " + std::to_string(launch_result));
    }
    const int sync_result = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms);
    if (sync_result != 0) {
        return unexpected(ErrorCode::kRuntime, "aclrtSynchronizeStreamWithTimeout after " + std::string(kernel_name) +
                                                   " failed: " + std::to_string(sync_result));
    }
    return {};
}

void AicpuLauncher::reset() noexcept {
    if (acl_ != nullptr && stream_ != nullptr && acl_->destroy_stream != nullptr)
        (void)acl_->destroy_stream(stream_);
    stream_ = nullptr;
    functions_.clear();
    if (acl_ != nullptr && binary_ != nullptr && acl_->binary_unload != nullptr)
        (void)acl_->binary_unload(binary_);
    binary_ = nullptr;
    acl_ = nullptr;
}

bool AicpuLauncher::loaded() const noexcept {
    return acl_ != nullptr && binary_ != nullptr && stream_ != nullptr;
}

}  // namespace nds
