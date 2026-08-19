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

const char *aicpu_storage_operator_name(std::uint16_t operation) {
    switch (operation) {
        case NDS_PROTOCOL_READ:
            return "NdsAicpuStorageRead";
        case NDS_PROTOCOL_WRITE:
            return "NdsAicpuStorageWrite";
        default:
            return nullptr;
    }
}
}  // namespace

AicpuEntrypointLauncher::~AicpuEntrypointLauncher() {
    reset();
}

Result<void> AicpuEntrypointLauncher::load(nds_acl_api *acl, const std::string &kernel_config_path) {
    nds_acl_binary_load_option option{};
    nds_acl_binary_load_options options{};
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

Result<void> AicpuEntrypointLauncher::launch_and_wait(nds_device_operation_request *request,
                                                      std::int32_t completion_timeout_ms) {
    nds_acl_args_handle arguments{};
    nds_acl_param_handle parameter_handle{};
    nds_acl_launch_kernel_attr attribute{};
    nds_acl_launch_kernel_config config{};
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

Result<void> AicpuEntrypointLauncher::launch_post_send_and_wait(nds_device_post_send_request *request,
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
    nds_acl_args_handle arguments{};
    nds_acl_param_handle parameter_handle{};
    if (acl_->kernel_args_init(function_, &arguments) != 0 || arguments == nullptr ||
        acl_->kernel_args_append(arguments, request, sizeof(*request), &parameter_handle) != 0 ||
        acl_->kernel_args_finalize(arguments) != 0) {
        if (arguments != nullptr)
            (void)acl_->kernel_args_finalize(arguments);
        return unexpected(ErrorCode::kRuntime, "failed to construct NDS AICPU PostSend arguments");
    }
    nds_acl_launch_kernel_attr attribute{};
    attribute.id = NDS_ACL_LAUNCH_KERNEL_ATTR_TIMEOUT;
    attribute.value.timeout_seconds = 5U;
    nds_acl_launch_kernel_config config{&attribute, 1U};
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

Result<void> AicpuEntrypointLauncher::launch_storage_and_wait(nds_device_storage_request *request,
                                                              std::int32_t completion_timeout_ms) {
    if (request == nullptr || request->storage.transport.abi_version != NDS_DEVICE_TRANSPORT_ABI_VERSION ||
        request->storage.transport.control_qp.abi_version != NDS_DEVICE_QP_ABI_VERSION ||
        request->operation_result_address == 0U || completion_timeout_ms <= 0) {
        return unexpected(ErrorCode::kInvalidArgument, "NDS AICPU storage request has invalid metadata");
    }
    const char *operator_name = aicpu_storage_operator_name(request->io.operation);
    if (!loaded() || operator_name == nullptr) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "NDS AICPU storage launch requires a loaded launcher and valid operation");
    }
    request->abi_version = NDS_DEVICE_STORAGE_ABI_VERSION;
    request->size = sizeof(*request);
    nds_acl_args_handle arguments{};
    nds_acl_param_handle parameter_handle{};
    nds_acl_launch_kernel_attr attribute{};
    nds_acl_launch_kernel_config config{};
    int result = acl_->binary_get_function(binary_, operator_name, &function_);
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
                          "aclrtKernelArgsAppend(NDS AICPU storage request) failed: " + std::to_string(result));
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
