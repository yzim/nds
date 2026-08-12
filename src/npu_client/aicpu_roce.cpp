#include "nds/aicpu_roce.hpp"

#include <algorithm>
#include <dlfcn.h>
#include <limits>
#include <utility>

namespace nds {
namespace {
constexpr const char *kNdsAicpuRoceWrite = "NdsAicpuRoceWrite";
constexpr const char *kHnsProviderLibrary = "libhns-rdmav25.so";
constexpr const char *kHnsPostSendSymbol = "ibv_exp_post_send";

bool verify_hns_provider(std::string &error)
{
    void *provider = dlopen(kHnsProviderLibrary, RTLD_NOW | RTLD_LOCAL);
    if (provider == nullptr) {
        const char *detail = dlerror();
        error = "NDS AICPU RoCE Write requires the CANN-9.0.0 HNS provider "
                "libhns-rdmav25.so, but the selected runtime cannot load it";
        if (detail != nullptr) error += std::string(": ") + detail;
        return false;
    }
    dlerror();
    void *post_send = dlsym(provider, kHnsPostSendSymbol);
    const char *detail = dlerror();
    (void)dlclose(provider);
    if (post_send == nullptr || detail != nullptr) {
        error = "NDS AICPU RoCE Write requires libhns-rdmav25.so:ibv_exp_post_send";
        if (detail != nullptr) error += std::string(": ") + detail;
        return false;
    }
    return true;
}
}

AicpuRoceWriteLauncher::~AicpuRoceWriteLauncher()
{
    reset();
}

void AicpuRoceWriteLauncher::set_error(std::string message)
{
    error_ = std::move(message);
}

bool AicpuRoceWriteLauncher::load(nds_acl_api &acl, const std::string &kernel_config_path)
{
    nds_acl_binary_load_option option{};
    nds_acl_binary_load_options options{};
    nds_acl_func_handle function{};
    int result;

    if (loaded()) {
        set_error("NDS AICPU RoCE Write launcher is already loaded");
        return false;
    }
    if (kernel_config_path.empty()) {
        set_error("NDS AICPU RoCE Write requires the NDS-built nds_aicpu_roce.json path");
        return false;
    }
    std::string provider_error;
    if (!verify_hns_provider(provider_error)) {
        set_error(std::move(provider_error));
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
        set_error("aclrtBinaryLoadFromFile(NDS AICPU package) failed: " + std::to_string(result));
        reset();
        return false;
    }
    result = acl_->binary_get_function(binary_, kNdsAicpuRoceWrite, &function);
    if (result != 0 || function == nullptr) {
        set_error("NDS AICPU package does not expose NdsAicpuRoceWrite: " + std::to_string(result));
        reset();
        return false;
    }
    result = acl_->create_stream(&stream_);
    if (result != 0 || stream_ == nullptr) {
        set_error("aclrtCreateStream for NDS AICPU RoCE Write failed: " + std::to_string(result));
        reset();
        return false;
    }
    error_.clear();
    return true;
}

bool AicpuRoceWriteLauncher::launch_and_wait(const AicpuRoceWriteRequest &request,
                                              std::int32_t completion_timeout_ms)
{
    nds_aicpu_roce_write_request_v1 parameters{};
    nds_acl_func_handle function{};
    nds_acl_args_handle arguments{};
    nds_acl_param_handle parameter_handle{};
    nds_acl_launch_kernel_attr attribute{};
    nds_acl_launch_kernel_config config{};
    int result;

    if (!loaded()) {
        set_error("NDS AICPU RoCE Write launcher is not loaded");
        return false;
    }
    if (request.ai_qp_address == 0U || request.local_key == 0U || request.remote_key == 0U ||
        request.local_address == 0U || request.remote_address == 0U || request.data_size == 0U ||
        request.data_size > NDS_AICPU_ROCE_MAX_BYTES || completion_timeout_ms <= 0) {
        set_error("NDS AICPU RoCE Write requires nonzero QP/MR metadata, a bounded transfer, and completion timeout");
        return false;
    }

    parameters.abi_version = NDS_AICPU_ROCE_ABI_VERSION;
    parameters.size = sizeof(parameters);
    parameters.ai_qp_address = request.ai_qp_address;
    parameters.local_lkey = request.local_key;
    parameters.remote_rkey = request.remote_key;
    parameters.local_address = request.local_address;
    parameters.remote_address = request.remote_address;
    parameters.length = request.data_size;
    parameters.wr_id = request.wr_id;

    result = acl_->binary_get_function(binary_, kNdsAicpuRoceWrite, &function);
    if (result != 0 || function == nullptr) {
        set_error("aclrtBinaryGetFunction(NdsAicpuRoceWrite) failed: " + std::to_string(result));
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
        set_error("aclrtKernelArgsAppend(NDS AICPU RoCE Write request) failed: " + std::to_string(result));
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
        set_error("aclrtLaunchKernelWithConfig(NdsAicpuRoceWrite) failed: " + std::to_string(result));
        return false;
    }
    result = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms);
    if (result != 0) {
        set_error("aclrtSynchronizeStreamWithTimeout after NdsAicpuRoceWrite failed: " + std::to_string(result));
        return false;
    }
    error_.clear();
    return true;
}

void AicpuRoceWriteLauncher::reset() noexcept
{
    if (acl_ != nullptr && stream_ != nullptr && acl_->destroy_stream != nullptr) {
        (void)acl_->destroy_stream(stream_);
    }
    stream_ = nullptr;
    if (acl_ != nullptr && binary_ != nullptr && acl_->binary_unload != nullptr) {
        (void)acl_->binary_unload(binary_);
    }
    binary_ = nullptr;
    acl_ = nullptr;
}

bool AicpuRoceWriteLauncher::loaded() const noexcept
{
    return acl_ != nullptr && binary_ != nullptr && stream_ != nullptr;
}

const std::string &AicpuRoceWriteLauncher::error() const noexcept
{
    return error_;
}

} // namespace nds
