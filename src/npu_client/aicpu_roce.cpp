#include "nds/aicpu_roce.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace nds {
namespace {
constexpr const char *kRunTransportRoceTx = "RunTransportRoceTx";
}

AicpuRoceTxLauncher::~AicpuRoceTxLauncher()
{
    reset();
}

void AicpuRoceTxLauncher::set_error(std::string message)
{
    error_ = std::move(message);
}

bool AicpuRoceTxLauncher::load(nds_acl_api &acl, const std::string &kernel_config_path)
{
    nds_acl_binary_load_option option{};
    nds_acl_binary_load_options options{};
    nds_acl_func_handle function{};
    int result;

    if (loaded()) {
        set_error("AICPU RoCE Tx launcher is already loaded");
        return false;
    }
    if (kernel_config_path.empty()) {
        set_error("AICPU RoCE Tx requires an explicit CANN ccl_kernel.json path");
        return false;
    }
    if (acl.binary_load_from_file == nullptr || acl.binary_unload == nullptr || acl.binary_get_function == nullptr ||
        acl.kernel_args_init == nullptr || acl.kernel_args_append == nullptr || acl.kernel_args_finalize == nullptr ||
        acl.launch_kernel_with_config == nullptr || acl.create_stream == nullptr || acl.destroy_stream == nullptr ||
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
        set_error("aclrtBinaryLoadFromFile(ccl_kernel.json) failed: " + std::to_string(result));
        reset();
        return false;
    }
    result = acl_->binary_get_function(binary_, kRunTransportRoceTx, &function);
    if (result != 0 || function == nullptr) {
        set_error("CANN ccl kernel package does not expose RunTransportRoceTx: " + std::to_string(result));
        reset();
        return false;
    }
    result = acl_->create_stream(&stream_);
    if (result != 0 || stream_ == nullptr) {
        set_error("aclrtCreateStream for AICPU RoCE Tx failed: " + std::to_string(result));
        reset();
        return false;
    }
    error_.clear();
    return true;
}

bool AicpuRoceTxLauncher::launch_and_wait(const AicpuRoceTxRequest &request, std::int32_t completion_timeout_ms)
{
    AicpuRoceTxParameters parameters{};
    nds_acl_func_handle function{};
    nds_acl_args_handle arguments{};
    nds_acl_param_handle parameter_handle{};
    nds_acl_launch_kernel_attr attribute{};
    nds_acl_launch_kernel_config config{};
    int result;

    if (!loaded()) {
        set_error("AICPU RoCE Tx launcher is not loaded");
        return false;
    }
    if (request.local_key == 0U || request.remote_key == 0U || request.qp.qp_ptr == 0U ||
        request.local_address == 0U || request.remote_address == 0U || request.data_size == 0U ||
        request.local_flag_address == 0U || request.remote_flag_address == 0U || request.local_flag_key == 0U ||
        request.remote_flag_key == 0U || completion_timeout_ms <= 0) {
        set_error("AICPU RoCE Tx requires nonzero QP, memory, flag-MR metadata, and completion timeout");
        return false;
    }

    parameters.local_key = request.local_key;
    parameters.remote_key = request.remote_key;
    parameters.qp = request.qp;
    parameters.remote_address = request.remote_address;
    parameters.local_address = request.local_address;
    parameters.data_size = request.data_size;
    parameters.timeout = request.transport_timeout;
    parameters.local_flag_address = request.local_flag_address;
    parameters.remote_flag_address = request.remote_flag_address;
    parameters.local_flag_key = request.local_flag_key;
    parameters.remote_flag_key = request.remote_flag_key;

    result = acl_->binary_get_function(binary_, kRunTransportRoceTx, &function);
    if (result != 0 || function == nullptr) {
        set_error("aclrtBinaryGetFunction(RunTransportRoceTx) failed: " + std::to_string(result));
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
        set_error("aclrtKernelArgsAppend(RunTransportRoceTx parameters) failed: " + std::to_string(result));
        return false;
    }
    result = acl_->kernel_args_finalize(arguments);
    if (result != 0) {
        set_error("aclrtKernelArgsFinalize failed: " + std::to_string(result));
        return false;
    }

    attribute.id = NDS_ACL_LAUNCH_KERNEL_ATTR_TIMEOUT;
    attribute.value.timeout_seconds = request.launch_timeout_seconds;
    config.attrs = &attribute;
    config.num_attrs = 1U;
    result = acl_->launch_kernel_with_config(function, 1U, stream_, &config, arguments, nullptr);
    if (result != 0) {
        set_error("aclrtLaunchKernelWithConfig(RunTransportRoceTx) failed: " + std::to_string(result));
        return false;
    }
    result = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms);
    if (result != 0) {
        set_error("aclrtSynchronizeStreamWithTimeout after RunTransportRoceTx failed: " + std::to_string(result));
        return false;
    }
    error_.clear();
    return true;
}

void AicpuRoceTxLauncher::reset() noexcept
{
    if (acl_ != nullptr && stream_ != nullptr && acl_->destroy_stream != nullptr) {
        (void)acl_->destroy_stream(stream_);
    }
    if (acl_ != nullptr && binary_ != nullptr && acl_->binary_unload != nullptr) {
        (void)acl_->binary_unload(binary_);
    }
    acl_ = nullptr;
    binary_ = nullptr;
    stream_ = nullptr;
}

bool AicpuRoceTxLauncher::loaded() const noexcept
{
    return acl_ != nullptr && binary_ != nullptr && stream_ != nullptr;
}

const std::string &AicpuRoceTxLauncher::error() const noexcept
{
    return error_;
}

} // namespace nds
