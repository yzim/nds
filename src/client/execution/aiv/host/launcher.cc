#include "launcher.hh"

#include <utility>

namespace nds {
namespace {
const char *aiv_storage_operator_name(StorageOperation operation) {
    switch (operation) {
        case StorageOperation::Read:
            return "NdsAivStorageRead";
        case StorageOperation::Write:
            return "NdsAivStorageWrite";
        case StorageOperation::BatchRead:
            return "NdsAivStorageBatchRead";
        case StorageOperation::BatchWrite:
            return "NdsAivStorageBatchWrite";
    }
    return nullptr;
}
}  // namespace

AivEntrypointLauncher::~AivEntrypointLauncher() {
    reset();
}

Result<void> AivEntrypointLauncher::load(NdsAclApi *acl, const std::string &kernel_path) {
    NdsAclBinaryLoadOption option{};
    NdsAclBinaryLoadOptions options{};
    int result;

    if (acl == nullptr || loaded() || kernel_path.empty()) {
        return unexpected(ErrorCode::kInvalidArgument, loaded() ? "NDS AIV launcher is already loaded"
                                                                : "NDS AIV requires an NDS-built kernel binary path");
    }
    if (acl->binary_load_from_file == nullptr || acl->binary_unload == nullptr || acl->binary_get_function == nullptr ||
        acl->launch_kernel_with_host_args == nullptr || acl->create_stream == nullptr ||
        acl->destroy_stream == nullptr || acl->synchronize_stream_with_timeout == nullptr) {
        return unexpected(ErrorCode::kRuntime,
                          "AscendCL is missing a required AIV binary, host-argument launch, or stream symbol");
    }
    acl_ = acl;
    option.type = NDS_ACL_BINARY_LOAD_OPT_LAZY_LOAD;
    option.value.lazy_load = 1U;
    options.options = &option;
    options.num_options = 1U;
    result = acl_->binary_load_from_file(kernel_path.c_str(), &options, &binary_);
    if (result != 0 || binary_ == nullptr) {
        const std::string error = "aclrtBinaryLoadFromFile(NDS AIV binary) failed: " + std::to_string(result);
        reset();
        return unexpected(ErrorCode::kRuntime, error);
    }
    result = acl_->create_stream(&stream_);
    if (result != 0 || stream_ == nullptr) {
        const std::string error = "aclrtCreateStream for NDS AIV launch failed: " + std::to_string(result);
        reset();
        return unexpected(ErrorCode::kRuntime, error);
    }
    return {};
}

Result<void> AivEntrypointLauncher::launch_post_send_and_wait(std::uint64_t args_address,
                                                              std::int32_t completion_timeout_ms) {
    if (!loaded() || args_address == 0U || completion_timeout_ms <= 0) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "NDS AIV PostSend launch requires a loaded binary, args address, and positive timeout");
    }
    if (acl_->binary_get_function(binary_, "NdsAivPostSend", &function_) != 0 || function_ == nullptr) {
        return unexpected(ErrorCode::kRuntime, "NDS AIV binary does not expose NdsAivPostSend");
    }
    NdsAclLaunchKernelAttr attributes[2]{};
    attributes[0].id = NDS_ACL_LAUNCH_KERNEL_ATTR_SCHEM_MODE;
    attributes[0].value.schem_mode = 1U;
    attributes[1].id = NDS_ACL_LAUNCH_KERNEL_ATTR_ENGINE_TYPE;
    attributes[1].value.engine_type = NDS_ACL_ENGINE_TYPE_AIV;
    NdsAclLaunchKernelConfig config{attributes, 2U};
    if (const int result = acl_->launch_kernel_with_host_args(function_, 1U, stream_, &config, &args_address,
                                                              sizeof(args_address), nullptr, 0U);
        result != 0) {
        return unexpected(ErrorCode::kRuntime,
                          "aclrtLaunchKernelWithHostArgs(NdsAivPostSend) failed: " + std::to_string(result));
    }
    if (const int result = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms); result != 0) {
        return unexpected(ErrorCode::kRuntime,
                          "aclrtSynchronizeStreamWithTimeout after NdsAivPostSend failed: " + std::to_string(result));
    }
    return {};
}

/* Resolves, launches, and stream-synchronizes the one-doorbell AIV batch entrypoint. */
Result<void> AivEntrypointLauncher::launch_post_send_batch_and_wait(std::uint64_t args_address,
                                                                    std::int32_t completion_timeout_ms) {
    if (!loaded() || args_address == 0U || completion_timeout_ms <= 0) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "NDS AIV PostSendBatch launch requires a loaded binary, args address, and positive timeout");
    }
    if (acl_->binary_get_function(binary_, "NdsAivPostSendBatch", &function_) != 0 || function_ == nullptr) {
        return unexpected(ErrorCode::kRuntime, "NDS AIV binary does not expose NdsAivPostSendBatch");
    }
    NdsAclLaunchKernelAttr attributes[2]{};
    attributes[0].id = NDS_ACL_LAUNCH_KERNEL_ATTR_SCHEM_MODE;
    attributes[0].value.schem_mode = 1U;
    attributes[1].id = NDS_ACL_LAUNCH_KERNEL_ATTR_ENGINE_TYPE;
    attributes[1].value.engine_type = NDS_ACL_ENGINE_TYPE_AIV;
    NdsAclLaunchKernelConfig config{attributes, 2U};
    if (const int result = acl_->launch_kernel_with_host_args(function_, 1U, stream_, &config, &args_address,
                                                              sizeof(args_address), nullptr, 0U);
        result != 0) {
        return unexpected(ErrorCode::kRuntime,
                          "aclrtLaunchKernelWithHostArgs(NdsAivPostSendBatch) failed: " + std::to_string(result));
    }
    if (const int result = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms); result != 0) {
        return unexpected(ErrorCode::kRuntime, "aclrtSynchronizeStreamWithTimeout after NdsAivPostSendBatch failed: " +
                                                   std::to_string(result));
    }
    return {};
}

Result<void> AivEntrypointLauncher::launch_storage_and_wait(std::uint64_t args_address, StorageOperation operation,
                                                            std::int32_t completion_timeout_ms) {
    NdsAclLaunchKernelAttr attributes[2]{};
    NdsAclLaunchKernelConfig config{};
    const char *operator_name = aiv_storage_operator_name(operation);
    if (!loaded() || args_address == 0U || completion_timeout_ms <= 0 || operator_name == nullptr) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "NDS AIV storage launch requires a loaded binary, args address, and valid operation");
    }
    const int function_result = acl_->binary_get_function(binary_, operator_name, &function_);
    if (function_result != 0 || function_ == nullptr) {
        return unexpected(ErrorCode::kRuntime, "NDS AIV binary does not expose " + std::string(operator_name) + ": " +
                                                   std::to_string(function_result));
    }
    attributes[0].id = NDS_ACL_LAUNCH_KERNEL_ATTR_SCHEM_MODE;
    attributes[0].value.schem_mode = 1U;
    attributes[1].id = NDS_ACL_LAUNCH_KERNEL_ATTR_ENGINE_TYPE;
    attributes[1].value.engine_type = NDS_ACL_ENGINE_TYPE_AIV;
    config.attrs = attributes;
    config.num_attrs = 2U;
    const int launch_result = acl_->launch_kernel_with_host_args(function_, 1U, stream_, &config, &args_address,
                                                                 sizeof(args_address), nullptr, 0U);
    if (launch_result != 0) {
        return unexpected(ErrorCode::kRuntime, "aclrtLaunchKernelWithHostArgs(" + std::string(operator_name) +
                                                   ") failed: " + std::to_string(launch_result));
    }
    const int sync_result = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms);
    if (sync_result != 0) {
        return unexpected(ErrorCode::kRuntime, "aclrtSynchronizeStreamWithTimeout after " + std::string(operator_name) +
                                                   " failed: " + std::to_string(sync_result));
    }
    return {};
}

Result<void> AivEntrypointLauncher::launch_storage_wait_and_wait(std::uint64_t args_address,
                                                                 std::int32_t completion_timeout_ms) {
    if (!loaded() || args_address == 0U || completion_timeout_ms <= 0)
        return unexpected(ErrorCode::kInvalidArgument,
                          "NDS AIV storage wait requires a loaded binary, args address, and positive timeout");
    if (acl_->binary_get_function(binary_, "NdsAivStorageWait", &function_) != 0 || function_ == nullptr)
        return unexpected(ErrorCode::kRuntime, "NDS AIV binary does not expose NdsAivStorageWait");
    NdsAclLaunchKernelAttr attributes[2]{};
    attributes[0].id = NDS_ACL_LAUNCH_KERNEL_ATTR_SCHEM_MODE;
    attributes[0].value.schem_mode = 1U;
    attributes[1].id = NDS_ACL_LAUNCH_KERNEL_ATTR_ENGINE_TYPE;
    attributes[1].value.engine_type = NDS_ACL_ENGINE_TYPE_AIV;
    NdsAclLaunchKernelConfig config{attributes, 2U};
    if (const int result = acl_->launch_kernel_with_host_args(function_, 1U, stream_, &config, &args_address,
                                                              sizeof(args_address), nullptr, 0U);
        result != 0)
        return unexpected(ErrorCode::kRuntime,
                          "aclrtLaunchKernelWithHostArgs(NdsAivStorageWait) failed: " + std::to_string(result));
    if (const int result = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms); result != 0)
        return unexpected(ErrorCode::kRuntime, "aclrtSynchronizeStreamWithTimeout after NdsAivStorageWait failed: " +
                                                   std::to_string(result));
    return {};
}

void AivEntrypointLauncher::reset() noexcept {
    if (acl_ != nullptr && stream_ != nullptr && acl_->destroy_stream != nullptr)
        (void)acl_->destroy_stream(stream_);
    stream_ = nullptr;
    function_ = nullptr;
    if (acl_ != nullptr && binary_ != nullptr && acl_->binary_unload != nullptr)
        (void)acl_->binary_unload(binary_);
    binary_ = nullptr;
    acl_ = nullptr;
}

bool AivEntrypointLauncher::loaded() const noexcept {
    return acl_ != nullptr && binary_ != nullptr && stream_ != nullptr;
}
}  // namespace nds
