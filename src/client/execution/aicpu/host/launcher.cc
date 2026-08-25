#include "launcher.hh"

#include <utility>

namespace nds {
namespace {
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

Result<void> AicpuEntrypointLauncher::launch_operator_and_wait(void *args, std::size_t size,
                                                                const char *operator_name,
                                                                std::int32_t completion_timeout_ms) {
    if (!loaded() || args == nullptr || size == 0U || operator_name == nullptr ||
        completion_timeout_ms <= 0) {
        return unexpected(ErrorCode::kInvalidArgument, "NDS AICPU operator launch has invalid metadata");
    }
    if (const int result = acl_->binary_get_function(binary_, operator_name, &function_);
        result != 0 || function_ == nullptr) {
        return unexpected(ErrorCode::kRuntime,
                          "NDS AICPU package does not expose " + std::string(operator_name) + ": " +
                              std::to_string(result));
    }
    NdsAclArgsHandle arguments{};
    NdsAclParamHandle parameter_handle{};
    const int initialized = acl_->kernel_args_init(function_, &arguments);
    if (initialized != 0 || arguments == nullptr) {
        return unexpected(ErrorCode::kRuntime,
                          "aclrtKernelArgsInit(" + std::string(operator_name) + ") failed: " +
                              std::to_string(initialized));
    }
    const int appended = acl_->kernel_args_append(arguments, args, size, &parameter_handle);
    if (appended != 0) {
        (void)acl_->kernel_args_finalize(arguments);
        return unexpected(ErrorCode::kRuntime,
                          "aclrtKernelArgsAppend(" + std::string(operator_name) + ", bytes=" +
                              std::to_string(size) + ") failed: " + std::to_string(appended));
    }
    const int finalized = acl_->kernel_args_finalize(arguments);
    if (finalized != 0) {
        return unexpected(ErrorCode::kRuntime,
                          "aclrtKernelArgsFinalize(" + std::string(operator_name) + ") failed: " +
                              std::to_string(finalized));
    }
    NdsAclLaunchKernelAttr attribute{};
    attribute.id = NDS_ACL_LAUNCH_KERNEL_ATTR_TIMEOUT;
    attribute.value.timeout_seconds = 5U;
    NdsAclLaunchKernelConfig config{&attribute, 1U};
    const int launched = acl_->launch_kernel_with_config(function_, 1U, stream_, &config, arguments, nullptr);
    if (launched != 0)
        return unexpected(ErrorCode::kRuntime,
                          "aclrtLaunchKernelWithConfig(" + std::string(operator_name) + ") failed: " +
                              std::to_string(launched));
    const int synchronized = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms);
    if (synchronized != 0)
        return unexpected(ErrorCode::kRuntime, "aclrtSynchronizeStreamWithTimeout after " + std::string(operator_name) +
                                                   " failed: " + std::to_string(synchronized));
    return {};
}

Result<void> AicpuEntrypointLauncher::launch_post_send_and_wait(NdsDevicePostSendArgs *args,
                                                                std::int32_t completion_timeout_ms) {
    if (args == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "NDS AICPU PostSend args have invalid metadata");
    return launch_operator_and_wait(args, sizeof(*args), "NdsAicpuPostSend",
                                    completion_timeout_ms);
}

Result<void> AicpuEntrypointLauncher::launch_post_send_batch_and_wait(NdsDevicePostSendBatchArgs *args,
                                                                      std::int32_t completion_timeout_ms) {
    if (args == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "NDS AICPU batch post args have invalid metadata");
    return launch_operator_and_wait(args, sizeof(*args), "NdsAicpuPostSendBatch", completion_timeout_ms);
}

Result<void> AicpuEntrypointLauncher::launch_post_recv_and_wait(NdsDevicePostRecvArgs *args,
                                                                std::int32_t completion_timeout_ms) {
    if (args == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "NDS AICPU PostRecv args have invalid metadata");
    return launch_operator_and_wait(args, sizeof(*args), "NdsAicpuPostRecv",
                                    completion_timeout_ms);
}

Result<void> AicpuEntrypointLauncher::launch_poll_cq_and_wait(NdsDevicePollCqArgs *args,
                                                              std::int32_t completion_timeout_ms) {
    if (args == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "NDS AICPU PollCq args have invalid metadata");
    return launch_operator_and_wait(args, sizeof(*args), "NdsAicpuPollCq",
                                    completion_timeout_ms);
}

Result<void> AicpuEntrypointLauncher::launch_rdma_send_and_wait(NdsDeviceRdmaSendArgs *args,
                                                                 std::int32_t completion_timeout_ms) {
    if (args == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "NDS AICPU RDMA send args have invalid metadata");
    return launch_operator_and_wait(args, sizeof(*args), "NdsAicpuRdmaSend",
                                    completion_timeout_ms);
}

Result<void> AicpuEntrypointLauncher::launch_rdma_recv_and_wait(NdsDeviceRdmaRecvArgs *args,
                                                                 std::int32_t completion_timeout_ms) {
    if (args == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "NDS AICPU RDMA receive args have invalid metadata");
    return launch_operator_and_wait(args, sizeof(*args), "NdsAicpuRdmaRecv",
                                    completion_timeout_ms);
}

Result<void> AicpuEntrypointLauncher::launch_rdma_read_and_wait(NdsDeviceRdmaReadArgs *args,
                                                                 std::int32_t completion_timeout_ms) {
    if (args == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "NDS AICPU RDMA read args have invalid metadata");
    return launch_operator_and_wait(args, sizeof(*args), "NdsAicpuRdmaRead",
                                    completion_timeout_ms);
}

Result<void> AicpuEntrypointLauncher::launch_rdma_write_and_wait(NdsDeviceRdmaWriteArgs *args,
                                                                  std::int32_t completion_timeout_ms) {
    if (args == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "NDS AICPU RDMA write args have invalid metadata");
    return launch_operator_and_wait(args, sizeof(*args), "NdsAicpuRdmaWrite",
                                    completion_timeout_ms);
}

Result<void> AicpuEntrypointLauncher::launch_rdma_benchmark_and_wait(NdsDeviceRdmaBenchmarkArgs *args,
                                                                       std::int32_t completion_timeout_ms) {
    if (args == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "NDS AICPU RDMA benchmark args have invalid metadata");
    return launch_operator_and_wait(args, sizeof(*args), "NdsAicpuRdmaBenchmark", completion_timeout_ms);
}

Result<void> AicpuEntrypointLauncher::launch_storage_and_wait(
    void *args, std::size_t size, const NdsDeviceStorageContext *context, const char *operator_name,
    std::int32_t completion_timeout_ms) {
    if (args == nullptr || context == nullptr ||
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
    return launch_storage_and_wait(args, sizeof(*args), args == nullptr ? nullptr : &args->context,
                                   "NdsAicpuStorageRead", timeout_ms);
}

Result<void> AicpuEntrypointLauncher::launch_storage_write_and_wait(NdsDeviceStorageWriteArgs *args,
                                                                    std::int32_t timeout_ms) {
    return launch_storage_and_wait(args, sizeof(*args), args == nullptr ? nullptr : &args->context,
                                   "NdsAicpuStorageWrite", timeout_ms);
}

Result<void> AicpuEntrypointLauncher::launch_storage_batch_read_and_wait(NdsDeviceStorageBatchReadArgs *args,
                                                                         std::int32_t timeout_ms) {
    return launch_storage_and_wait(args, sizeof(*args), args == nullptr ? nullptr : &args->context,
                                   "NdsAicpuStorageBatchRead", timeout_ms);
}

Result<void> AicpuEntrypointLauncher::launch_storage_batch_write_and_wait(NdsDeviceStorageBatchWriteArgs *args,
                                                                          std::int32_t timeout_ms) {
    return launch_storage_and_wait(args, sizeof(*args), args == nullptr ? nullptr : &args->context,
                                   "NdsAicpuStorageBatchWrite", timeout_ms);
}

Result<void> AicpuEntrypointLauncher::launch_storage_wait_and_wait(NdsDeviceStorageWaitArgs *args,
                                                                    std::int32_t timeout_ms) {
    return launch_storage_and_wait(args, sizeof(*args), args == nullptr ? nullptr : &args->context,
                                   "NdsAicpuStorageWait", timeout_ms);
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
