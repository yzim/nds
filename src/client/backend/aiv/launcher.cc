#include "nds/aiv_roce.hh"

#include <utility>

namespace nds {
namespace {
constexpr const char *kNdsAivRdmaPost = "NdsAivRdmaPost";
}

AivRdmaPostLauncher::~AivRdmaPostLauncher() {
    reset();
}

void AivRdmaPostLauncher::set_error(std::string message) {
    error_ = std::move(message);
}

bool AivRdmaPostLauncher::load(nds_acl_api *acl, const std::string &kernel_path) {
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
    result = acl_->binary_get_function(binary_, kNdsAivRdmaPost, &post_function_);
    if (result != 0 || post_function_ == nullptr) {
        set_error("NDS AIV binary does not expose NdsAivRdmaPost: " + std::to_string(result));
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

bool AivRdmaPostLauncher::make_device_request(const AivRdmaPostRequest &request, nds_aiv_rdma_post_request *output) {
    if (output == nullptr || request.send_wq.buffer_address == 0U || request.send_wq.wqebb_size == 0U ||
        request.send_wq.depth < 2U || request.send_wq.head_address == 0U || request.send_wq.tail_address == 0U ||
        request.send_wq.doorbell_register_address == 0U || request.local_key == 0U || request.local_address == 0U ||
        request.data_size == 0U || request.post_count == 0U ||
        (request.opcode != NDS_AIV_SEND && request.opcode != NDS_AIV_RDMA_WRITE) ||
        (request.opcode == NDS_AIV_SEND && (request.remote_key != 0U || request.remote_address != 0U)) ||
        (request.opcode == NDS_AIV_RDMA_WRITE && (request.remote_key == 0U || request.remote_address == 0U))) {
        set_error("NDS AIV request has invalid AI-SQ or post metadata");
        return false;
    }
    *output = {};
    output->abi_version = NDS_AIV_ROCE_ABI_VERSION;
    output->size = sizeof(*output);
    output->send_queue.wqn = request.send_wq.wqn;
    output->send_queue.buffer_address = request.send_wq.buffer_address;
    output->send_queue.wqebb_size = request.send_wq.wqebb_size;
    output->send_queue.depth = request.send_wq.depth;
    output->send_queue.head_address = request.send_wq.head_address;
    output->send_queue.tail_address = request.send_wq.tail_address;
    output->send_queue.doorbell_address = request.send_wq.doorbell_register_address;
    output->send_queue.service_level = request.service_level;
    output->opcode = request.opcode;
    output->local_lkey = request.local_key;
    output->remote_rkey = request.remote_key;
    output->local_address = request.local_address;
    output->remote_address = request.remote_address;
    output->length = request.data_size;
    output->post_count = request.post_count;
    error_.clear();
    return true;
}

bool AivRdmaPostLauncher::launch_post_and_wait(std::uint64_t device_request_address,
                                               std::int32_t completion_timeout_ms) {
    return launch_and_wait(device_request_address, completion_timeout_ms);
}

bool AivRdmaPostLauncher::launch_and_wait(std::uint64_t device_request_address, std::int32_t completion_timeout_ms) {
    nds_acl_launch_kernel_attr attributes[2]{};
    nds_acl_launch_kernel_config config{};
    int result;

    if (!loaded() || device_request_address == 0U || completion_timeout_ms <= 0) {
        set_error("NDS AIV launch requires a loaded binary, a device request address, and a positive timeout");
        return false;
    }
    attributes[0].id = NDS_ACL_LAUNCH_KERNEL_ATTR_SCHEM_MODE;
    attributes[0].value.schem_mode = 1U;
    attributes[1].id = NDS_ACL_LAUNCH_KERNEL_ATTR_ENGINE_TYPE;
    attributes[1].value.engine_type = NDS_ACL_ENGINE_TYPE_AIV;
    config.attrs = attributes;
    config.num_attrs = 2U;
    result = acl_->launch_kernel_with_host_args(post_function_, 1U, stream_, &config, &device_request_address,
                                                sizeof(device_request_address), nullptr, 0U);
    if (result != 0) {
        set_error("aclrtLaunchKernelWithHostArgs(NdsAivRdmaPost) failed: " + std::to_string(result));
        return false;
    }
    result = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms);
    if (result != 0) {
        set_error("aclrtSynchronizeStreamWithTimeout after NdsAivRdmaPost failed: " + std::to_string(result));
        return false;
    }
    error_.clear();
    return true;
}

void AivRdmaPostLauncher::reset() noexcept {
    if (acl_ != nullptr && stream_ != nullptr && acl_->destroy_stream != nullptr)
        (void)acl_->destroy_stream(stream_);
    stream_ = nullptr;
    post_function_ = nullptr;
    if (acl_ != nullptr && binary_ != nullptr && acl_->binary_unload != nullptr)
        (void)acl_->binary_unload(binary_);
    binary_ = nullptr;
    acl_ = nullptr;
}

bool AivRdmaPostLauncher::loaded() const noexcept {
    return acl_ != nullptr && binary_ != nullptr && post_function_ != nullptr && stream_ != nullptr;
}
const std::string &AivRdmaPostLauncher::error() const noexcept {
    return error_;
}

}  // namespace nds
