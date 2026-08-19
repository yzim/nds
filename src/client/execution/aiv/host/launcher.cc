#include "launcher.hh"

#include <utility>

namespace nds {
namespace {
const char *aiv_operator_name(std::uint32_t operation) {
    switch (operation) {
    case NDS_DEVICE_RDMA_SEND: return "NdsAivRdmaSend";
    case NDS_DEVICE_RDMA_RECV: return "NdsAivRdmaRecv";
    case NDS_DEVICE_RDMA_READ: return "NdsAivRdmaRead";
    case NDS_DEVICE_RDMA_WRITE: return "NdsAivRdmaWrite";
    case NDS_DEVICE_POLL_CQ: return "NdsAivPollCq";
    default: return nullptr;
    }
}

const char *aiv_storage_operator_name(std::uint16_t operation) {
    switch (operation) {
    case NDS_PROTOCOL_READ: return "NdsAivStorageRead";
    case NDS_PROTOCOL_WRITE: return "NdsAivStorageWrite";
    default: return nullptr;
    }
}
}

AivEntrypointLauncher::~AivEntrypointLauncher() {
    reset();
}

void AivEntrypointLauncher::set_error(std::string message) {
    error_ = std::move(message);
}

bool AivEntrypointLauncher::load(nds_acl_api *acl, const std::string &kernel_path) {
    nds_acl_binary_load_option option{};
    nds_acl_binary_load_options options{};
    int result;

    if (acl == nullptr || loaded() || kernel_path.empty()) {
        set_error(loaded() ? "NDS AIV launcher is already loaded" : "NDS AIV requires an NDS-built kernel binary path");
        return false;
    }
    if (acl->binary_load_from_file == nullptr || acl->binary_unload == nullptr || acl->binary_get_function == nullptr ||
        acl->launch_kernel_with_host_args == nullptr || acl->create_stream == nullptr ||
        acl->destroy_stream == nullptr || acl->synchronize_stream_with_timeout == nullptr) {
        set_error("AscendCL is missing a required AIV binary, host-argument launch, or stream symbol");
        return false;
    }
    acl_ = acl;
    option.type = NDS_ACL_BINARY_LOAD_OPT_LAZY_LOAD;
    option.value.lazy_load = 1U;
    options.options = &option;
    options.num_options = 1U;
    result = acl_->binary_load_from_file(kernel_path.c_str(), &options, &binary_);
    if (result != 0 || binary_ == nullptr) {
        set_error("aclrtBinaryLoadFromFile(NDS AIV binary) failed: " + std::to_string(result));
        reset();
        return false;
    }
    result = acl_->create_stream(&stream_);
    if (result != 0 || stream_ == nullptr) {
        set_error("aclrtCreateStream for NDS AIV launch failed: " + std::to_string(result));
        reset();
        return false;
    }
    error_.clear();
    return true;
}

bool AivEntrypointLauncher::make_device_request(const nds_device_operation_request &request,
                                                 nds_device_operation_request *output) {
    if (output == nullptr || request.transport.abi_version != NDS_DEVICE_TRANSPORT_ABI_VERSION ||
        request.transport.size != sizeof(request.transport) ||
        request.transport.control_qp.abi_version != NDS_DEVICE_QP_ABI_VERSION ||
        request.operation < NDS_DEVICE_RDMA_SEND || request.operation > NDS_DEVICE_POLL_CQ) {
        set_error("NDS AIV request has invalid device transport metadata");
        return false;
    }
    *output = request;
    output->abi_version = NDS_DEVICE_OPERATIONS_ABI_VERSION;
    output->size = sizeof(*output);
    error_.clear();
    return true;
}

bool AivEntrypointLauncher::launch_and_wait(std::uint64_t device_request_address, std::uint32_t operation,
                                             std::int32_t completion_timeout_ms) {
    nds_acl_launch_kernel_attr attributes[2]{};
    nds_acl_launch_kernel_config config{};
    int result;

    const char *operator_name = aiv_operator_name(operation);
    if (!loaded() || device_request_address == 0U || completion_timeout_ms <= 0 || operator_name == nullptr) {
        set_error("NDS AIV launch requires a loaded binary, a device request address, and a positive timeout");
        return false;
    }
    result = acl_->binary_get_function(binary_, operator_name, &function_);
    if (result != 0 || function_ == nullptr) {
        set_error("NDS AIV binary does not expose " + std::string(operator_name) + ": " + std::to_string(result));
        return false;
    }
    attributes[0].id = NDS_ACL_LAUNCH_KERNEL_ATTR_SCHEM_MODE;
    attributes[0].value.schem_mode = 1U;
    attributes[1].id = NDS_ACL_LAUNCH_KERNEL_ATTR_ENGINE_TYPE;
    attributes[1].value.engine_type = NDS_ACL_ENGINE_TYPE_AIV;
    config.attrs = attributes;
    config.num_attrs = 2U;
    result = acl_->launch_kernel_with_host_args(function_, 1U, stream_, &config, &device_request_address,
                                                sizeof(device_request_address), nullptr, 0U);
    if (result != 0) {
        set_error("aclrtLaunchKernelWithHostArgs(" + std::string(operator_name) + ") failed: " + std::to_string(result));
        return false;
    }
    result = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms);
    if (result != 0) {
        set_error("aclrtSynchronizeStreamWithTimeout after " + std::string(operator_name) + " failed: " + std::to_string(result));
        return false;
    }
    error_.clear();
    return true;
}

bool AivEntrypointLauncher::launch_post_send_and_wait(std::uint64_t device_request_address,
                                                       std::int32_t completion_timeout_ms) {
    if (!loaded() || device_request_address == 0U || completion_timeout_ms <= 0) {
        set_error("NDS AIV PostSend launch requires a loaded binary, request address, and positive timeout");
        return false;
    }
    if (acl_->binary_get_function(binary_, "NdsAivPostSend", &function_) != 0 || function_ == nullptr) {
        set_error("NDS AIV binary does not expose NdsAivPostSend");
        return false;
    }
    nds_acl_launch_kernel_attr attributes[2]{};
    attributes[0].id = NDS_ACL_LAUNCH_KERNEL_ATTR_SCHEM_MODE;
    attributes[0].value.schem_mode = 1U;
    attributes[1].id = NDS_ACL_LAUNCH_KERNEL_ATTR_ENGINE_TYPE;
    attributes[1].value.engine_type = NDS_ACL_ENGINE_TYPE_AIV;
    nds_acl_launch_kernel_config config{attributes, 2U};
    if (const int result = acl_->launch_kernel_with_host_args(function_, 1U, stream_, &config, &device_request_address,
                                                               sizeof(device_request_address), nullptr, 0U);
        result != 0) {
        set_error("aclrtLaunchKernelWithHostArgs(NdsAivPostSend) failed: " + std::to_string(result));
        return false;
    }
    if (const int result = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms); result != 0) {
        set_error("aclrtSynchronizeStreamWithTimeout after NdsAivPostSend failed: " + std::to_string(result));
        return false;
    }
    error_.clear();
    return true;
}

bool AivEntrypointLauncher::launch_storage_and_wait(std::uint64_t device_request_address, std::uint16_t operation,
                                                     std::int32_t completion_timeout_ms) {
    nds_acl_launch_kernel_attr attributes[2]{};
    nds_acl_launch_kernel_config config{};
    const char *operator_name = aiv_storage_operator_name(operation);
    if (!loaded() || device_request_address == 0U || completion_timeout_ms <= 0 || operator_name == nullptr) {
        set_error("NDS AIV storage launch requires a loaded binary, request address, and valid operation");
        return false;
    }
    const int function_result = acl_->binary_get_function(binary_, operator_name, &function_);
    if (function_result != 0 || function_ == nullptr) {
        set_error("NDS AIV binary does not expose " + std::string(operator_name) + ": " + std::to_string(function_result));
        return false;
    }
    attributes[0].id = NDS_ACL_LAUNCH_KERNEL_ATTR_SCHEM_MODE;
    attributes[0].value.schem_mode = 1U;
    attributes[1].id = NDS_ACL_LAUNCH_KERNEL_ATTR_ENGINE_TYPE;
    attributes[1].value.engine_type = NDS_ACL_ENGINE_TYPE_AIV;
    config.attrs = attributes;
    config.num_attrs = 2U;
    const int launch_result = acl_->launch_kernel_with_host_args(function_, 1U, stream_, &config,
                                                                 &device_request_address, sizeof(device_request_address),
                                                                 nullptr, 0U);
    if (launch_result != 0) {
        set_error("aclrtLaunchKernelWithHostArgs(" + std::string(operator_name) + ") failed: " +
                  std::to_string(launch_result));
        return false;
    }
    const int sync_result = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms);
    if (sync_result != 0) {
        set_error("aclrtSynchronizeStreamWithTimeout after " + std::string(operator_name) + " failed: " +
                  std::to_string(sync_result));
        return false;
    }
    error_.clear();
    return true;
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
const std::string &AivEntrypointLauncher::error() const noexcept {
    return error_;
}

}  // namespace nds
