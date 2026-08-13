#include "nds/aicpu_roce.hpp"

#include <utility>

namespace nds {
namespace {
constexpr const char *kNdsAicpuRdmaPost = "NdsAicpuRdmaPost";
}

AicpuRdmaPostLauncher::~AicpuRdmaPostLauncher()
{
    reset();
}

void AicpuRdmaPostLauncher::set_error(std::string message)
{
    error_ = std::move(message);
}

bool AicpuRdmaPostLauncher::load(nds_acl_api &acl, const std::string &kernel_config_path)
{
    nds_acl_binary_load_option option{};
    nds_acl_binary_load_options options{};
    int result;

    if (loaded()) {
        set_error("NDS AICPU RDMA post launcher is already loaded");
        return false;
    }
    if (kernel_config_path.empty()) {
        set_error("NDS AICPU RDMA post requires the NDS-built nds_aicpu_standard.json path");
        return false;
    }
    if (acl.binary_load_from_file == nullptr || acl.binary_unload == nullptr || acl.binary_get_function == nullptr ||
        acl.kernel_args_init == nullptr || acl.kernel_args_append == nullptr || acl.kernel_args_finalize == nullptr ||
        acl.launch_kernel_with_config == nullptr || acl.create_stream_with_config == nullptr || acl.destroy_stream == nullptr ||
        acl.synchronize_stream_with_timeout == nullptr) {
        set_error("AscendCL is missing a required AICPU binary, argument, launch, or stream symbol");
        return false;
    }

    acl_ = &acl;
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
    result = acl_->binary_get_function(binary_, kNdsAicpuRdmaPost, &function_);
    if (result != 0 || function_ == nullptr) {
        set_error("NDS AICPU package does not expose NdsAicpuRdmaPost: " + std::to_string(result));
        reset();
        return false;
    }
    result = acl_->create_stream_with_config(&stream_, 0U,
                                              NDS_ACL_STREAM_FAST_LAUNCH | NDS_ACL_STREAM_FAST_SYNC);
    if (result != 0 || stream_ == nullptr) {
        set_error("aclrtCreateStreamWithConfig for NDS AICPU RDMA post failed: " + std::to_string(result));
        reset();
        return false;
    }
    error_.clear();
    return true;
}

bool AicpuRdmaPostLauncher::launch_and_wait(const AicpuRdmaPostRequest &request,
                                              std::int32_t completion_timeout_ms)
{
    nds_aicpu_rdma_post_request_v2 parameters{};
    nds_acl_args_handle arguments{};
    nds_acl_param_handle parameter_handle{};
    nds_acl_launch_kernel_attr attribute{};
    nds_acl_launch_kernel_config config{};
    int result;

    if (!loaded()) {
        set_error("NDS AICPU request launch requires a loaded launcher");
        return false;
    }
    const bool is_send = request.opcode == NDS_AICPU_SEND;
    const bool is_rdma = request.opcode == NDS_AICPU_RDMA_WRITE || request.opcode == NDS_AICPU_RDMA_READ;
    if (!is_rdma && !is_send) {
        set_error("NDS AICPU request opcode is unsupported");
        return false;
    }
    if (request.ai_qp_address == 0U || request.local_key == 0U ||
        request.local_address == 0U || request.data_size == 0U ||
        request.data_size > NDS_AICPU_ROCE_MAX_BYTES || completion_timeout_ms <= 0 ||
        (is_rdma && (request.remote_key == 0U || request.remote_address == 0U)) ||
        (is_send && (request.remote_key != 0U || request.remote_address != 0U))) {
        set_error("NDS AICPU request has invalid QP, memory, operation, or timeout metadata");
        return false;
    }

    parameters.abi_version = NDS_AICPU_ROCE_ABI_VERSION;
    parameters.size = sizeof(parameters);
    parameters.opcode = request.opcode;
    parameters.logical_device_id = request.logical_device_id;
    parameters.ai_qp_address = request.ai_qp_address;
    parameters.local_lkey = request.local_key;
    parameters.remote_rkey = request.remote_key;
    parameters.local_address = request.local_address;
    parameters.remote_address = request.remote_address;
    parameters.length = request.data_size;
    parameters.wr_id = request.wr_id;
    parameters.reserved_0 = 0U;
    parameters.reserved = 0U;

    result = acl_->kernel_args_init(function_, &arguments);
    if (result != 0 || arguments == nullptr) {
        set_error("aclrtKernelArgsInit failed: " + std::to_string(result));
        return false;
    }
    result = acl_->kernel_args_append(arguments, &parameters, sizeof(parameters), &parameter_handle);
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
    attribute.value.timeout_seconds = request.launch_timeout_seconds;
    config.num_attrs = 1U;
    config.attrs = &attribute;
    result = acl_->launch_kernel_with_config(function_, 1U, stream_, &config, arguments, nullptr);
    if (result != 0) {
        set_error("aclrtLaunchKernelWithConfig(NdsAicpuRdmaPost) failed: " + std::to_string(result));
        return false;
    }
    result = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms);
    if (result != 0) {
        set_error("aclrtSynchronizeStreamWithTimeout after NdsAicpuRdmaPost failed: " + std::to_string(result));
        return false;
    }
    error_.clear();
    return true;
}

void AicpuRdmaPostLauncher::reset() noexcept
{
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

bool AicpuRdmaPostLauncher::loaded() const noexcept
{
    return acl_ != nullptr && binary_ != nullptr && function_ != nullptr && stream_ != nullptr;
}

const std::string &AicpuRdmaPostLauncher::error() const noexcept
{
    return error_;
}

} // namespace nds
