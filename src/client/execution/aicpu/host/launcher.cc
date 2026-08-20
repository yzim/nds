#include "launcher.hh"

#include <utility>

namespace nds {
namespace {
const char *aicpu_operator_name(std::uint32_t operation) {
    switch (operation) {
        case NDS_DEVICE_RDMA_SEND:
            return "NdsAicpuRdmaSend";
        case NDS_DEVICE_RDMA_RECV:
            return "NdsAicpuRdmaRecv";
        case NDS_DEVICE_RDMA_READ:
            return "NdsAicpuRdmaRead";
        case NDS_DEVICE_RDMA_WRITE:
            return "NdsAicpuRdmaWrite";
        case NDS_DEVICE_POLL_CQ:
            return "NdsAicpuPollCq";
        default:
            return nullptr;
    }
}

}  // namespace

AicpuEntrypointLauncher::~AicpuEntrypointLauncher() {
    reset();
}

Result<void> AicpuEntrypointLauncher::load(NdsAclApi *acl, const std::string &kernel_config_path) {
    NdsAclBinaryLoadOption option{};
    NdsAclBinaryLoadOptions options{};
    int result;

    if (acl == nullptr) {
        return unexpected(ErrorCode::kInvalidArgument, "NDS AICPU RDMA post launcher requires AscendCL API storage");
    }
    if (loaded()) {
        return unexpected(ErrorCode::kInvalidArgument, "NDS AICPU RDMA post launcher is already loaded");
    }
    if (kernel_config_path.empty()) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "NDS AICPU launch requires the NDS-built nds_aicpu_standard.json path");
    }
    if (acl->binary_load_from_file == nullptr || acl->binary_unload == nullptr || acl->binary_get_function == nullptr ||
        acl->kernel_args_init == nullptr || acl->kernel_args_append == nullptr ||
        acl->kernel_args_finalize == nullptr || acl->launch_kernel_with_config == nullptr ||
        acl->create_stream_with_config == nullptr || acl->destroy_stream == nullptr ||
        acl->synchronize_stream_with_timeout == nullptr) {
        return unexpected(ErrorCode::kRuntime,
                          "AscendCL is missing a required AICPU binary, argument, launch, or stream symbol");
    }

    acl_ = acl;
    option.type = NDS_ACL_BINARY_LOAD_OPT_CPU_KERNEL_MODE;
    option.value.cpu_kernel_mode = NDS_ACL_CPU_KERNEL_REGISTER_JSON;
    options.options = &option;
    options.num_options = 1U;
    result = acl_->binary_load_from_file(kernel_config_path.c_str(), &options, &binary_);
    if (result != 0 || binary_ == nullptr) {
        const std::string error =
            "aclrtBinaryLoadFromFile(NDS AICPU package configuration) failed: " + std::to_string(result);
        reset();
        return unexpected(ErrorCode::kRuntime, error);
    }
    result = acl_->create_stream_with_config(&stream_, 0U, NDS_ACL_STREAM_FAST_LAUNCH | NDS_ACL_STREAM_FAST_SYNC);
    if (result != 0 || stream_ == nullptr) {
        const std::string error =
            "aclrtCreateStreamWithConfig for NDS AICPU RDMA post failed: " + std::to_string(result);
        reset();
        return unexpected(ErrorCode::kRuntime, error);
    }
    return {};
}

Result<void> AicpuEntrypointLauncher::launch_and_wait(NdsDeviceOperationRequest *request,
                                                      std::int32_t completion_timeout_ms) {
    NdsAclArgsHandle arguments{};
    NdsAclParamHandle parameter_handle{};
    NdsAclLaunchKernelAttr attribute{};
    NdsAclLaunchKernelConfig config{};
    int result;

    if (!loaded()) {
        return unexpected(ErrorCode::kRuntime, "NDS AICPU request launch requires a loaded launcher");
    }
    const char *operator_name = request == nullptr ? nullptr : aicpu_operator_name(request->operation);
    if (request == nullptr || completion_timeout_ms <= 0 || operator_name == nullptr ||
        request->transport.abi_version != NDS_DEVICE_TRANSPORT_ABI_VERSION ||
        request->transport.control_qp.abi_version != NDS_DEVICE_QP_ABI_VERSION) {
        return unexpected(ErrorCode::kInvalidArgument, "NDS AICPU request has invalid device transport metadata");
    }
    request->abi_version = NDS_DEVICE_OPERATIONS_ABI_VERSION;
    request->size = sizeof(*request);
    result = acl_->binary_get_function(binary_, operator_name, &function_);
    if (result != 0 || function_ == nullptr) {
        return unexpected(ErrorCode::kRuntime, "NDS AICPU package does not expose " + std::string(operator_name) +
                                                   ": " + std::to_string(result));
    }

    result = acl_->kernel_args_init(function_, &arguments);
    if (result != 0 || arguments == nullptr) {
        return unexpected(ErrorCode::kRuntime, "aclrtKernelArgsInit failed: " + std::to_string(result));
    }
    result = acl_->kernel_args_append(arguments, request, sizeof(*request), &parameter_handle);
    if (result != 0) {
        (void)acl_->kernel_args_finalize(arguments);
        return unexpected(ErrorCode::kRuntime,
                          "aclrtKernelArgsAppend(NDS AICPU request) failed: " + std::to_string(result));
    }
    result = acl_->kernel_args_finalize(arguments);
    if (result != 0) {
        return unexpected(ErrorCode::kRuntime, "aclrtKernelArgsFinalize failed: " + std::to_string(result));
    }

    attribute.id = NDS_ACL_LAUNCH_KERNEL_ATTR_TIMEOUT;
    attribute.value.timeout_seconds = 5U;
    config.num_attrs = 1U;
    config.attrs = &attribute;
    result = acl_->launch_kernel_with_config(function_, 1U, stream_, &config, arguments, nullptr);
    if (result != 0) {
        return unexpected(ErrorCode::kRuntime, "aclrtLaunchKernelWithConfig(" + std::string(operator_name) +
                                                   ") failed: " + std::to_string(result));
    }
    result = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms);
    if (result != 0) {
        return unexpected(ErrorCode::kRuntime, "aclrtSynchronizeStreamWithTimeout after " + std::string(operator_name) +
                                                   " failed: " + std::to_string(result));
    }
    return {};
}

Result<void> AicpuEntrypointLauncher::launch_post_send_and_wait(NdsDevicePostSendRequest *request,
                                                                std::int32_t completion_timeout_ms) {
    if (!loaded() || request == nullptr || request->qp.abi_version != NDS_DEVICE_QP_ABI_VERSION ||
        request->operation_result_address == 0U || completion_timeout_ms <= 0) {
        return unexpected(ErrorCode::kInvalidArgument, "NDS AICPU PostSend request has invalid metadata");
    }
    request->abi_version = NDS_DEVICE_OPERATOR_ARGS_ABI_VERSION;
    request->size = sizeof(*request);
    if (acl_->binary_get_function(binary_, "NdsAicpuPostSend", &function_) != 0 || function_ == nullptr) {
        return unexpected(ErrorCode::kRuntime, "NDS AICPU package does not expose NdsAicpuPostSend");
    }
    NdsAclArgsHandle arguments{};
    NdsAclParamHandle parameter_handle{};
    if (acl_->kernel_args_init(function_, &arguments) != 0 || arguments == nullptr ||
        acl_->kernel_args_append(arguments, request, sizeof(*request), &parameter_handle) != 0 ||
        acl_->kernel_args_finalize(arguments) != 0) {
        if (arguments != nullptr)
            (void)acl_->kernel_args_finalize(arguments);
        return unexpected(ErrorCode::kRuntime, "failed to construct NDS AICPU PostSend arguments");
    }
    NdsAclLaunchKernelAttr attribute{};
    attribute.id = NDS_ACL_LAUNCH_KERNEL_ATTR_TIMEOUT;
    attribute.value.timeout_seconds = 5U;
    NdsAclLaunchKernelConfig config{&attribute, 1U};
    const int launched = acl_->launch_kernel_with_config(function_, 1U, stream_, &config, arguments, nullptr);
    if (launched != 0) {
        return unexpected(ErrorCode::kRuntime,
                          "aclrtLaunchKernelWithConfig(NdsAicpuPostSend) failed: " + std::to_string(launched));
    }
    const int synchronized = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms);
    if (synchronized != 0) {
        return unexpected(ErrorCode::kRuntime, "aclrtSynchronizeStreamWithTimeout after NdsAicpuPostSend failed: " +
                                                   std::to_string(synchronized));
    }
    return {};
}

Result<void> AicpuEntrypointLauncher::launch_storage_and_wait(
    void *args, std::size_t size, const NdsDeviceStorageContext *context, std::uint64_t result_address,
    const char *operator_name, std::int32_t completion_timeout_ms) {
    if (args == nullptr || context == nullptr || context->transport.abi_version != NDS_DEVICE_TRANSPORT_ABI_VERSION ||
        context->transport.control_qp.abi_version != NDS_DEVICE_QP_ABI_VERSION || result_address == 0U ||
        operator_name == nullptr || completion_timeout_ms <= 0) {
        return unexpected(ErrorCode::kInvalidArgument, "NDS AICPU storage args have invalid metadata");
    }
    if (!loaded()) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "NDS AICPU storage launch requires a loaded launcher and valid operation");
    }
    NdsAclArgsHandle arguments{};
    NdsAclParamHandle parameter_handle{};
    NdsAclLaunchKernelAttr attribute{};
    NdsAclLaunchKernelConfig config{};
    int result = acl_->binary_get_function(binary_, operator_name, &function_);
    if (result != 0 || function_ == nullptr) {
        return unexpected(ErrorCode::kRuntime, "NDS AICPU package does not expose " + std::string(operator_name) +
                                                   ": " + std::to_string(result));
    }
    result = acl_->kernel_args_init(function_, &arguments);
    if (result != 0 || arguments == nullptr) {
        return unexpected(ErrorCode::kRuntime, "aclrtKernelArgsInit failed: " + std::to_string(result));
    }
    result = acl_->kernel_args_append(arguments, args, size, &parameter_handle);
    if (result != 0) {
        (void)acl_->kernel_args_finalize(arguments);
        return unexpected(ErrorCode::kRuntime,
                          "aclrtKernelArgsAppend(NDS AICPU storage args) failed: " + std::to_string(result));
    }
    result = acl_->kernel_args_finalize(arguments);
    if (result != 0) {
        return unexpected(ErrorCode::kRuntime, "aclrtKernelArgsFinalize failed: " + std::to_string(result));
    }
    attribute.id = NDS_ACL_LAUNCH_KERNEL_ATTR_TIMEOUT;
    attribute.value.timeout_seconds = 5U;
    config.num_attrs = 1U;
    config.attrs = &attribute;
    result = acl_->launch_kernel_with_config(function_, 1U, stream_, &config, arguments, nullptr);
    if (result != 0) {
        return unexpected(ErrorCode::kRuntime, "aclrtLaunchKernelWithConfig(" + std::string(operator_name) +
                                                   ") failed: " + std::to_string(result));
    }
    result = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms);
    if (result != 0) {
        return unexpected(ErrorCode::kRuntime, "aclrtSynchronizeStreamWithTimeout after " + std::string(operator_name) +
                                                   " failed: " + std::to_string(result));
    }
    return {};
}

Result<void> AicpuEntrypointLauncher::launch_storage_read_and_wait(NdsDeviceStorageReadArgs *args,
                                                                   std::int32_t timeout_ms) {
    if (args != nullptr) {
        args->abi_version = NDS_DEVICE_STORAGE_ABI_VERSION;
        args->size = sizeof(*args);
    }
    return launch_storage_and_wait(args, sizeof(*args), args == nullptr ? nullptr : &args->context,
                                   args == nullptr ? 0U : args->operation_result_address, "NdsAicpuStorageRead",
                                   timeout_ms);
}

Result<void> AicpuEntrypointLauncher::launch_storage_write_and_wait(NdsDeviceStorageWriteArgs *args,
                                                                    std::int32_t timeout_ms) {
    if (args != nullptr) {
        args->abi_version = NDS_DEVICE_STORAGE_ABI_VERSION;
        args->size = sizeof(*args);
    }
    return launch_storage_and_wait(args, sizeof(*args), args == nullptr ? nullptr : &args->context,
                                   args == nullptr ? 0U : args->operation_result_address, "NdsAicpuStorageWrite",
                                   timeout_ms);
}

Result<void> AicpuEntrypointLauncher::launch_storage_batch_read_and_wait(NdsDeviceStorageBatchReadArgs *args,
                                                                         std::int32_t timeout_ms) {
    if (args != nullptr) {
        args->abi_version = NDS_DEVICE_STORAGE_ABI_VERSION;
        args->size = sizeof(*args);
    }
    return launch_storage_and_wait(args, sizeof(*args), args == nullptr ? nullptr : &args->context,
                                   args == nullptr ? 0U : args->operation_result_address,
                                   "NdsAicpuStorageBatchRead", timeout_ms);
}

Result<void> AicpuEntrypointLauncher::launch_storage_batch_write_and_wait(NdsDeviceStorageBatchWriteArgs *args,
                                                                          std::int32_t timeout_ms) {
    if (args != nullptr) {
        args->abi_version = NDS_DEVICE_STORAGE_ABI_VERSION;
        args->size = sizeof(*args);
    }
    return launch_storage_and_wait(args, sizeof(*args), args == nullptr ? nullptr : &args->context,
                                   args == nullptr ? 0U : args->operation_result_address,
                                   "NdsAicpuStorageBatchWrite", timeout_ms);
}

void AicpuEntrypointLauncher::reset() noexcept {
    if (acl_ != nullptr && stream_ != nullptr && acl_->destroy_stream != nullptr) {
        (void)acl_->destroy_stream(stream_);
    }
    stream_ = nullptr;
    if (acl_ != nullptr && binary_ != nullptr && acl_->binary_unload != nullptr) {
        (void)acl_->binary_unload(binary_);
    }
    binary_ = nullptr;
    function_ = nullptr;
    acl_ = nullptr;
}

bool AicpuEntrypointLauncher::loaded() const noexcept {
    return acl_ != nullptr && binary_ != nullptr && stream_ != nullptr;
}

}  // namespace nds
