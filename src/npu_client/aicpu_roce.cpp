#include "nds/aicpu_roce.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace nds {
namespace {
constexpr const char *kNdsAicpuRdmaPost = "NdsAicpuRdmaPost";
constexpr std::uint32_t kDeviceStatusHostUninitialized = NDS_AICPU_RDMA_POST_STATUS_NONE;

const char *device_status_name(std::uint32_t status)
{
    switch (status) {
    case kDeviceStatusHostUninitialized: return "not written (kernel did not reach its status checkpoint)";
    case NDS_AICPU_RDMA_POST_STATUS_ENTERED: return "entered kernel";
    case NDS_AICPU_RDMA_POST_STATUS_INVALID_ARGUMENT: return "kernel rejected request";
    case NDS_AICPU_RDMA_POST_STATUS_PROVIDER_UNAVAILABLE: return "HNS provider/symbol unavailable";
    case NDS_AICPU_RDMA_POST_STATUS_DOORBELL_UNAVAILABLE: return "AICPU doorbell wrapper/symbol unavailable";
    case NDS_AICPU_RDMA_POST_STATUS_POST_FAILED: return "ibv_exp_post_send failed";
    case NDS_AICPU_RDMA_POST_STATUS_POSTED: return "provider post completed; doorbell pending";
    case NDS_AICPU_RDMA_POST_STATUS_DOORBELL_FAILED: return "hrtRDMADBSend failed";
    case NDS_AICPU_RDMA_POST_STATUS_SUCCESS: return "post and doorbell completed";
    default: return "unknown checkpoint";
    }
}

std::string describe_device_status(std::uint32_t status)
{
    return "AICPU checkpoint=" + std::to_string(status) + " (" + device_status_name(status) + ")";
}

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
    nds_acl_func_handle function{};
    int result;

    if (loaded()) {
        set_error("NDS AICPU RDMA post launcher is already loaded");
        return false;
    }
    if (kernel_config_path.empty()) {
        set_error("NDS AICPU RDMA post requires the NDS-built nds_aicpu_roce.json path");
        return false;
    }
    if (acl.binary_load_from_file == nullptr || acl.binary_unload == nullptr || acl.binary_get_function == nullptr ||
        acl.kernel_args_init == nullptr || acl.kernel_args_append == nullptr || acl.kernel_args_finalize == nullptr ||
        acl.launch_kernel_with_config == nullptr || acl.create_stream == nullptr || acl.destroy_stream == nullptr ||
        acl.synchronize_stream_with_timeout == nullptr || acl.malloc_device == nullptr ||
        acl.free_device == nullptr || acl.memcpy == nullptr || acl.memset_device == nullptr) {
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
    result = acl_->binary_get_function(binary_, kNdsAicpuRdmaPost, &function);
    if (result != 0 || function == nullptr) {
        set_error("NDS AICPU package does not expose NdsAicpuRdmaPost: " + std::to_string(result));
        reset();
        return false;
    }
    result = acl_->malloc_device(&status_device_buffer_, sizeof(std::uint32_t), NDS_ACL_MEM_MALLOC_DIRECT_NPU);
    if (result != 0 || status_device_buffer_ == nullptr) {
        set_error("aclrtMalloc(AICPU status buffer) failed: " + std::to_string(result));
        reset();
        return false;
    }
    result = acl_->create_stream(&stream_);
    if (result != 0 || stream_ == nullptr) {
        set_error("aclrtCreateStream for NDS AICPU RDMA post failed: " + std::to_string(result));
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
    nds_acl_func_handle function{};
    nds_acl_args_handle arguments{};
    nds_acl_param_handle parameter_handle{};
    nds_acl_launch_kernel_attr attribute{};
    nds_acl_launch_kernel_config config{};
    std::uint32_t device_status = kDeviceStatusHostUninitialized;
    int result;

    if (!loaded()) {
        set_error("NDS AICPU RDMA post launcher is not loaded");
        return false;
    }
    const bool is_send = request.opcode == NDS_AICPU_SEND;
    const bool is_rdma = request.opcode == NDS_AICPU_RDMA_WRITE || request.opcode == NDS_AICPU_RDMA_READ;
    if (!is_rdma && !is_send) {
        set_error("NDS AICPU RDMA post opcode is unsupported");
        return false;
    }
    if (request.db_index == 0U || request.ai_qp_address == 0U || request.local_key == 0U ||
        request.local_address == 0U || request.data_size == 0U ||
        request.data_size > NDS_AICPU_ROCE_MAX_BYTES || completion_timeout_ms <= 0 ||
        (is_rdma && (request.remote_key == 0U || request.remote_address == 0U)) ||
        (is_send && (request.remote_key != 0U || request.remote_address != 0U))) {
        set_error("NDS AICPU RDMA post has invalid QP, memory, operation, or timeout metadata");
        return false;
    }

    parameters.abi_version = NDS_AICPU_ROCE_ABI_VERSION;
    parameters.size = sizeof(parameters);
    parameters.opcode = request.opcode;
    parameters.db_index = request.db_index;
    parameters.ai_qp_address = request.ai_qp_address;
    parameters.local_lkey = request.local_key;
    parameters.remote_rkey = request.remote_key;
    parameters.local_address = request.local_address;
    parameters.remote_address = request.remote_address;
    parameters.length = request.data_size;
    parameters.wr_id = request.wr_id;
    parameters.status_device_address = reinterpret_cast<std::uint64_t>(status_device_buffer_);

    result = acl_->memset_device(status_device_buffer_, sizeof(device_status), 0, sizeof(device_status));
    if (result != 0) {
        set_error("aclrtMemset(AICPU status buffer) failed: " + std::to_string(result));
        return false;
    }
    last_device_status_ = kDeviceStatusHostUninitialized;

    result = acl_->binary_get_function(binary_, kNdsAicpuRdmaPost, &function);
    if (result != 0 || function == nullptr) {
        set_error("aclrtBinaryGetFunction(NdsAicpuRdmaPost) failed: " + std::to_string(result));
        return false;
    }
    result = acl_->kernel_args_init(function, &arguments);
    if (result != 0 || arguments == nullptr) {
        set_error("aclrtKernelArgsInit failed: " + std::to_string(result));
        return false;
    }
    result = acl_->kernel_args_append(arguments, &parameters, sizeof(parameters), &parameter_handle);
    if (result != 0) {
        (void)acl_->kernel_args_finalize(arguments);
        set_error("aclrtKernelArgsAppend(NDS AICPU RDMA post request) failed: " + std::to_string(result));
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
    result = acl_->launch_kernel_with_config(function, 1U, stream_, &config, arguments, nullptr);
    if (result != 0) {
        set_error("aclrtLaunchKernelWithConfig(NdsAicpuRdmaPost) failed: " + std::to_string(result));
        return false;
    }
    result = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms);
    if (result != 0) {
        const int copy_result = acl_->memcpy(&device_status, sizeof(device_status), status_device_buffer_,
                                             sizeof(device_status), NDS_ACL_MEMCPY_DEVICE_TO_HOST);
        if (copy_result == 0) last_device_status_ = device_status;
        set_error("aclrtSynchronizeStreamWithTimeout after NdsAicpuRdmaPost failed: " + std::to_string(result) +
                  "; " + (copy_result == 0 ? describe_device_status(last_device_status_)
                                            : "aclrtMemcpy(AICPU status buffer) failed: " + std::to_string(copy_result)));
        return false;
    }
    result = acl_->memcpy(&device_status, sizeof(device_status), status_device_buffer_, sizeof(device_status),
                          NDS_ACL_MEMCPY_DEVICE_TO_HOST);
    if (result != 0) {
        set_error("aclrtMemcpy(AICPU status buffer) failed: " + std::to_string(result));
        return false;
    }
    last_device_status_ = device_status;
    if (last_device_status_ != NDS_AICPU_RDMA_POST_STATUS_SUCCESS) {
        set_error("NdsAicpuRdmaPost completed without success checkpoint; " + describe_device_status(last_device_status_));
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
    if (acl_ != nullptr && status_device_buffer_ != nullptr && acl_->free_device != nullptr) {
        (void)acl_->free_device(status_device_buffer_);
    }
    status_device_buffer_ = nullptr;
    last_device_status_ = kDeviceStatusHostUninitialized;
    if (acl_ != nullptr && binary_ != nullptr && acl_->binary_unload != nullptr) {
        (void)acl_->binary_unload(binary_);
    }
    binary_ = nullptr;
    acl_ = nullptr;
}

bool AicpuRdmaPostLauncher::loaded() const noexcept
{
    return acl_ != nullptr && binary_ != nullptr && stream_ != nullptr;
}

const std::string &AicpuRdmaPostLauncher::error() const noexcept
{
    return error_;
}

std::uint32_t AicpuRdmaPostLauncher::last_device_status() const noexcept
{
    return last_device_status_;
}

} // namespace nds
