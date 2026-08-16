#include "nds/aicpu_launcher.hh"

#include <utility>

namespace nds {
namespace {
constexpr const char *kNdsAicpuConnectionOp = "NdsAicpuConnectionOp";
}

AicpuConnectionLauncher::~AicpuConnectionLauncher() {
    reset();
}

void AicpuConnectionLauncher::set_error(std::string message) {
    error_ = std::move(message);
}

bool AicpuConnectionLauncher::load(nds_acl_api *acl, const std::string &kernel_config_path) {
    nds_acl_binary_load_option option{};
    nds_acl_binary_load_options options{};
    int result;

    if (acl == nullptr) {
        set_error("NDS AICPU RDMA post launcher requires AscendCL API storage");
        return false;
    }
    if (loaded()) {
        set_error("NDS AICPU RDMA post launcher is already loaded");
        return false;
    }
    if (kernel_config_path.empty()) {
        set_error("NDS AICPU RDMA post requires the NDS-built nds_aicpu_standard.json path");
        return false;
    }
    if (acl->binary_load_from_file == nullptr || acl->binary_unload == nullptr || acl->binary_get_function == nullptr ||
        acl->kernel_args_init == nullptr || acl->kernel_args_append == nullptr ||
        acl->kernel_args_finalize == nullptr || acl->launch_kernel_with_config == nullptr ||
        acl->create_stream_with_config == nullptr || acl->destroy_stream == nullptr ||
        acl->synchronize_stream_with_timeout == nullptr) {
        set_error("AscendCL is missing a required AICPU binary, argument, launch, or stream symbol");
        return false;
    }

    acl_ = acl;
    option.type = NDS_ACL_BINARY_LOAD_OPT_CPU_KERNEL_MODE;
    option.value.cpu_kernel_mode = 0;
    options.options = &option;
    options.num_options = 1U;
    result = acl_->binary_load_from_file(kernel_config_path.c_str(), &options, &binary_);
    if (result != 0 || binary_ == nullptr) {
        set_error("aclrtBinaryLoadFromFile(NDS AICPU package) failed: " + std::to_string(result));
        reset();
        return false;
    }
    result = acl_->binary_get_function(binary_, kNdsAicpuConnectionOp, &function_);
    if (result != 0 || function_ == nullptr) {
        set_error("NDS AICPU package does not expose NdsAicpuConnectionOp: " + std::to_string(result));
        reset();
        return false;
    }
    result = acl_->create_stream_with_config(&stream_, 0U, NDS_ACL_STREAM_FAST_LAUNCH | NDS_ACL_STREAM_FAST_SYNC);
    if (result != 0 || stream_ == nullptr) {
        set_error("aclrtCreateStreamWithConfig for NDS AICPU RDMA post failed: " + std::to_string(result));
        reset();
        return false;
    }
    error_.clear();
    return true;
}

bool AicpuConnectionLauncher::launch_and_wait(nds_device_operation_request *request,
                                              std::int32_t completion_timeout_ms) {
    nds_acl_args_handle arguments{};
    nds_acl_param_handle parameter_handle{};
    nds_acl_launch_kernel_attr attribute{};
    nds_acl_launch_kernel_config config{};
    int result;

    if (!loaded()) {
        set_error("NDS AICPU request launch requires a loaded launcher");
        return false;
    }
    if (request == nullptr || completion_timeout_ms <= 0 ||
        request->connection.abi_version != NDS_DEVICE_CONNECTION_ABI_VERSION ||
        request->connection.qp.abi_version != NDS_DEVICE_QP_ABI_VERSION) {
        set_error("NDS AICPU request has invalid device connection metadata");
        return false;
    }
    request->abi_version = NDS_DEVICE_OPERATIONS_ABI_VERSION;
    request->size = sizeof(*request);

    result = acl_->kernel_args_init(function_, &arguments);
    if (result != 0 || arguments == nullptr) {
        set_error("aclrtKernelArgsInit failed: " + std::to_string(result));
        return false;
    }
    result = acl_->kernel_args_append(arguments, request, sizeof(*request), &parameter_handle);
    if (result != 0) {
        (void)acl_->kernel_args_finalize(arguments);
        set_error("aclrtKernelArgsAppend(NDS AICPU request) failed: " + std::to_string(result));
        return false;
    }
    result = acl_->kernel_args_finalize(arguments);
    if (result != 0) {
        set_error("aclrtKernelArgsFinalize failed: " + std::to_string(result));
        return false;
    }

    attribute.id = NDS_ACL_LAUNCH_KERNEL_ATTR_TIMEOUT;
    attribute.value.timeout_seconds = 5U;
    config.num_attrs = 1U;
    config.attrs = &attribute;
    result = acl_->launch_kernel_with_config(function_, 1U, stream_, &config, arguments, nullptr);
    if (result != 0) {
        set_error("aclrtLaunchKernelWithConfig(NdsAicpuConnectionOp) failed: " + std::to_string(result));
        return false;
    }
    result = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms);
    if (result != 0) {
        set_error("aclrtSynchronizeStreamWithTimeout after NdsAicpuConnectionOp failed: " + std::to_string(result));
        return false;
    }
    error_.clear();
    return true;
}

void AicpuConnectionLauncher::reset() noexcept {
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

bool AicpuConnectionLauncher::loaded() const noexcept {
    return acl_ != nullptr && binary_ != nullptr && function_ != nullptr && stream_ != nullptr;
}

const std::string &AicpuConnectionLauncher::error() const noexcept {
    return error_;
}

}  // namespace nds
